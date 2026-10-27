// Copyright 2026 The RENKU project
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Haiku has no seccomp-bpf, no namespaces and no broker, so the renderer
// cannot be sandboxed the way it is on Linux. What it can do is stop being
// root. Every Haiku application runs as uid 0, and the kernel does check
// POSIX credentials, so a renderer that drops to an unprivileged uid before
// it touches web content (measured on R1/beta6 hrev59866):
//
//   can no longer                         can still
//   - write or create the user's files    - read world-readable files, which
//   - signal or kill other teams            on Haiku is most of the disk
//   - list or clone other teams' areas    - write to other teams' ports, so
//   - get root back (setuid(0) fails)       send BMessages to Tracker, the
//                                           registrar or app_server
//                                         - open network sockets
//
// That alone is a mitigation, not a sandbox: a compromised renderer could
// still escape by messaging another application. On a Renku kernel with
// restrict_syscalls() (docs/SANDBOX.md), the renderer then gives up every
// syscall group but BASIC and FILE_READ: no ports, no sockets, no new
// processes, no debugging, nothing system-wide, and no writing by path.
// Stock Haiku has no such call; there the uid drop is all there is, and the
// log says so.
//
// Nothing the renderer does after this needs root or those groups: its pak,
// ICU and V8 files arrive as descriptors or are world-readable, fonts are
// read-only files (HaikuFontMgr), shared memory is allocated by the browser
// (a sandboxed child gets mojo's shared memory hooks, see
// content/app/initialize_mojo_core.cc), IPC runs over the descriptors it
// inherited, and message pumps wake through socket pairs.
// --no-sandbox skips all of this, as on every platform.

#include "content/renderer/renderer_main_platform_delegate.h"

#include <OS.h>
#include <dlfcn.h>
#include <grp.h>
#include <unistd.h>

#include "base/check.h"
#include "base/check_op.h"
#include "base/logging.h"

namespace content {

namespace {

// "nobody" on most systems. Haiku has no user database to look it up in,
// and needs none: the kernel only compares numbers. Nothing on a Haiku
// system is owned by this uid or group, so the renderer owns no file.
constexpr uid_t kRendererUid = 65534;
constexpr gid_t kRendererGid = 65534;

// From Renku's <OS.h>; spelled out so that this builds against a stock Haiku
// sysroot too, where restrict_syscalls() is looked up at run time.
constexpr uint32 kSyscallGroupBasic = 0x0001;     // B_SYSCALL_GROUP_BASIC
constexpr uint32 kSyscallGroupFileRead = 0x0002;  // B_SYSCALL_GROUP_FILE_READ

// Gives up every syscall group but BASIC and FILE_READ, where the kernel can.
void RestrictSyscalls() {
  using RestrictSyscallsFunction = status_t (*)(uint32 allowed_groups);
  auto restrict_syscalls = reinterpret_cast<RestrictSyscallsFunction>(
      dlsym(RTLD_DEFAULT, "restrict_syscalls"));
  if (!restrict_syscalls) {
    LOG(WARNING) << "no restrict_syscalls() on this system: the renderer "
                    "dropped root but can still use ports and sockets";
    return;
  }
  const status_t status =
      restrict_syscalls(kSyscallGroupBasic | kSyscallGroupFileRead);
  if (status == B_NOT_SUPPORTED) {
    LOG(WARNING) << "restrict_syscalls() is not supported on this "
                    "architecture: the renderer dropped root only";
    return;
  }
  // Any other failure is fatal, as for the uid drop.
  CHECK_EQ(status, B_OK) << "restrict_syscalls";
}

}  // namespace

RendererMainPlatformDelegate::RendererMainPlatformDelegate(
    const MainFunctionParams& parameters) {}

RendererMainPlatformDelegate::~RendererMainPlatformDelegate() = default;

void RendererMainPlatformDelegate::PlatformInitialize() {}

void RendererMainPlatformDelegate::PlatformUninitialize() {}

bool RendererMainPlatformDelegate::EnableSandbox() {
  if (geteuid() != 0) {
    // A multi-user Haiku, or a browser started by a non-root user: there is
    // nothing to drop to that would take rights away from the user, and
    // setuid() would fail anyway. Say so rather than claim a sandbox.
    LOG(WARNING) << "renderer not root (euid " << geteuid()
                 << "), keeping the user's rights";
    return true;
  }

  // Credentials are per team on Haiku, so this covers the threads the
  // renderer already has (the IO thread, the thread pool). Groups first:
  // once the uid is gone there is no right to change them. Any failure is
  // fatal -- a renderer that believes it dropped root and did not is worse
  // than none.
  PCHECK(setgroups(0, nullptr) == 0) << "setgroups";
  PCHECK(setgid(kRendererGid) == 0) << "setgid";
  PCHECK(setuid(kRendererUid) == 0) << "setuid";
  CHECK_EQ(getuid(), kRendererUid);
  CHECK_EQ(geteuid(), kRendererUid);
  CHECK_EQ(getgid(), kRendererGid);
  CHECK_EQ(getegid(), kRendererGid);
  CHECK_NE(setuid(0), 0) << "root could be regained";

  // The browser's working directory is the app directory, which the
  // renderer has no reason to keep a handle on.
  PCHECK(chdir("/") == 0);

  RestrictSyscalls();
  return true;
}

}  // namespace content
