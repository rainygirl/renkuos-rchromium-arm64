# Sandboxing R Chromium on Haiku and Renku

## Where it stands

- **Multi-process** (done): renderers, the network service and storage run
  as their own teams; the launchers no longer pass `--single-process`.
- **Stage 1, renderers drop root** (done, not yet built):
  `RendererMainPlatformDelegate::EnableSandbox()` moves each renderer from
  uid 0 to uid/gid 65534 before it touches web content. Measured on
  R1/beta6 (AGENTS.md, "Renderers drop root"), that stops it writing the
  user's files, signalling or killing other teams, reading or cloning their
  areas, and regaining root. It does **not** stop it reading world-readable
  files, opening sockets, or writing to other teams' ports. Port writes
  are the hole that matters: a BMessage to Tracker or the registrar can
  launch or delete anything.
- **Stage 2, a kernel syscall filter** (this document): Renku only, because
  it needs a kernel change. Written, on the `syscall-filter` branch of a
  local Renku checkout (`~/Projects/renku-src`), with its test program, and
  the renderer calls it. The kernel and libroot, built from that branch on
  top of nightly build00062 (aae7d9f7), boot to the desktop on x86_64, and
  `syscall_filter_test` passes all 9 cases, 10 runs out of 10
  (2026-10-26). The change only adds: no syscall is renumbered and no
  kernel structure member moves. R Chromium runs on it sandboxed: its
  renderers drop to uid 65534 and to `BASIC | FILE_READ`, make no denied
  syscall in normal browsing (checked with the filter's denial tracing
  on), and render pages, Brave Shields included. That run found one gap,
  now fixed: `getentropy()` is a generic syscall, which the table had
  left in `SYSTEM`, so every renderer aborted at its first random number.
  Not yet run: the 32-bit compat entry and arm64. (Shutdown on that VM
  panics in the TCP module, as the unmodified kernel does: Renku issue
  #167.)

## Stage 2: what the kernel needs

A team must be able to give up syscalls, irreversibly, for itself and
every team it later creates. That is what seccomp does on Linux and
`pledge()` on OpenBSD. Haiku has neither, and nothing else in its kernel
can stand in: credentials do not cover ports, sockets or most system-wide
syscalls.

### API

```c
/* <OS.h>, Renku only */
#define B_SYSCALL_GROUP_BASIC     0x0001  /* memory, own threads and semaphores,
                                            time, descriptors already open */
#define B_SYSCALL_GROUP_FILE_READ 0x0002  /* open/stat/readdir read-only by path */
#define B_SYSCALL_GROUP_FILE_WRITE 0x0004 /* create, write, rename, unlink */
#define B_SYSCALL_GROUP_SOCKET    0x0008  /* socket(), connect, bind, listen */
#define B_SYSCALL_GROUP_PROCESS   0x0010  /* fork, exec, load_image, wait */
#define B_SYSCALL_GROUP_PORT      0x0020  /* every port syscall */
#define B_SYSCALL_GROUP_AREA_SHARE 0x0040 /* clone/transfer areas across teams */
#define B_SYSCALL_GROUP_DEBUG     0x0080  /* debugger install, debug threads */
#define B_SYSCALL_GROUP_SYSTEM    0x0100  /* everything else: devices, mount,
                                            shutdown, clock, scheduler, ... */

status_t restrict_syscalls(uint32 allowedGroups);
```

- `restrict_syscalls()` takes the intersection with what the team already
  has, so it can only remove groups. There is no way to get one back.
- The restriction is inherited by forked and loaded teams.
- A filtered syscall returns `B_NOT_ALLOWED` (EPERM). It does not kill the
  team: Chromium handles errors, and a kill is only as good as the error
  path anyway. (A later `B_SYSCALL_FILTER_KILL` flag can add Linux's
  SECCOMP_RET_KILL behaviour.)
- Groups, not syscall numbers. Haiku's syscall numbers come from the order
  in `headers/private/system/syscalls.h` and change between builds, so they
  cannot be an ABI. Group membership is a table in the kernel next to the
  syscall list.
- **Fail closed.** A syscall that is not in the table belongs to
  `B_SYSCALL_GROUP_SYSTEM`. A syscall added to Haiku later is denied to
  sandboxed teams until someone decides which group it belongs to.

### Enforcement

The check has to sit on every way into the kernel, or the filter is
decoration:

- x86_64: `x86_64_syscall_entry` (interrupts.S) calls the syscall function
  directly from assembly. A new thread flag, `THREAD_FLAGS_SYSCALL_FILTERED`,
  takes the slow path, as `THREAD_FLAGS_DEBUGGER_INSTALLED` already does,
  into a C check against the team's groups.
- x86_64 compat (32-bit) entry, `entry_compat.S`: the same check. A 64-bit
  team can switch to the 32-bit code segment and enter there, so leaving it
  out would be a bypass.
- arm64: `arch_int.cpp` dispatches syscalls in C, so the check is one call.
- x86 (32-bit), riscv64, arm: the same, or `restrict_syscalls()` returns
  `B_NOT_SUPPORTED` on an architecture that does not have it yet, so
  nothing believes it is filtered when it is not.
- The flag is set on every thread of the team when the restriction is
  applied, and on every thread created afterwards.

The table itself sits in `src/system/kernel/syscall_filter.cpp` (MIT, SPDX
header), with one entry per allowed syscall per group; about 300 syscalls,
most of them `SYSTEM`.

### Children

A team that keeps `PROCESS` can `load_image()` a child, and starting and
waiting for it means `resume_thread()` and `wait_for_thread()` on the
child's main thread, which belongs to another team. So a thread or signal
syscall that names a thread of one of the caller's own child teams needs
`PROCESS`; one that names any other team's thread needs `SYSTEM`.

### Argument checks

Groups decide by syscall alone. Three places need more than that, and are
handled in the syscall itself, behind the same team flag:

- `ioctl`, on descriptors the team already has: allowed only for socket
  and file ioctls that do not reach a device (`FIONBIO`, `FIONREAD`,
  ...). Device ioctls fall under `SYSTEM`.
- `open` with write flags falls under `FILE_WRITE` even though the syscall
  is the same as a read-only open.
- `socketpair()` (AF_UNIX) stays in `BASIC`: Mojo needs it, and the pair
  reaches nothing outside the team.
- Generic syscalls (`_kern_generic_syscall()`) are dispatched by
  subsystem name. Only `getentropy()`'s (`random`, `RANDOM_GET_ENTROPY`)
  is in `BASIC`; the name is checked inside the syscall, on the kernel's
  copy of it, so another thread cannot change it after the check.

### What R Chromium would allow

| Process | Groups |
|---|---|
| renderer | `BASIC`, `FILE_READ` |
| GPU | in-process (in the browser), so not filtered |
| network service | everything but `PROCESS`, `PORT`, `DEBUG`, `SYSTEM` |
| storage, audio, other utilities | later, one at a time |
| browser | unfiltered |

The renderer keeps `FILE_READ` for fonts, which HaikuFontMgr opens lazily
by path. Taking that away too needs fonts handed over by the browser, as
Linux's font service does, or an `unveil()`-style path list (a further
kernel change). With stage 1's uid drop the renderer already cannot read
the profile, whose files Chromium creates 0600.

Chromium side: `EnableSandbox()` calls `restrict_syscalls()` after the uid
drop, looked up with `dlsym()` so that one binary still runs on stock
Haiku and Renku builds without the filter (logging that it has only the
uid drop there).

Two things the renderer did that `BASIC` and `FILE_READ` would have
broken, found while writing this:

- **Shared memory.** Already handled by Chromium: a child whose command
  line does not mark it unsandboxed gets mojo's shared memory hooks
  (`content/app/initialize_mojo_core.cc`), and the browser creates its
  shared memory for it. With `--no-sandbox` gone from the launchers, the
  renderer no longer creates temp files at all.
- **The message pump's wake-up.** Haiku has no `eventfd`, and the port's
  replacement was a UDP socket bound and connected to itself, created by
  every thread with a message pump, including threads that start after the
  sandbox is on. That needs `socket()`, and granting `SOCKET` would let the
  renderer open network connections. The pump now uses a pair of Unix
  datagram sockets (`socketpair()`, in `BASIC`), writing to one end and
  polling the other (port-message-pump.py).

### Testing the kernel change

Renku asks for strong testing of kernel changes. The plan:

- A test program per group: each denied syscall returns `B_NOT_ALLOWED`,
  each allowed one still works, the restriction survives `fork()` and
  `load_image()`, and cannot be widened.
- The bypasses: a thread created before the call, a 32-bit compat entry
  from a 64-bit team, and a syscall number beyond the table.
- Renku's existing test suites and a desktop session with a sandboxed R
  Chromium on x86_64 and arm64 (QEMU), on a VM with the patched kernel.

### Upstream

This goes to RenkuOS/Source as a pull request: one logical change (kernel
filter plus its test), the "How this was tested" section, and a
`Signed-off-by:` from the person submitting it. Under Renku's rules, that
person is responsible for every line.
