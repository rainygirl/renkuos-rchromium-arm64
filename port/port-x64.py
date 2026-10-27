#!/usr/bin/env python3
"""The x86_64 half of the Haiku port.

Everything else in port/ came from the arm64 port and is architecture-neutral
or guarded by current_cpu. What is here is what x86_64 needs on top: the
places where that port only ever answered for arm64.

Run after apply-haiku-port.sh. Each entry is a (file, old, new) triple,
applied only if `old` occurs exactly once; an entry whose `new` is already in
the file counts as done. The arm64 build in the same tree is left alone --
every edit adds an x64 branch rather than changing the arm64 one.
"""
import os
import sys

EDITS = [
    # The clang target triple, as port-feature-flags.py does for arm64:
    # Chromium would otherwise compile x64 against x86_64-unknown-linux-gnu.
    ("build/config/compiler_cpu_abi.gn",
     '  if (current_cpu == "x64") {\n'
     '    if (is_clang && !is_android && !is_fuchsia && !is_chromeos_device) {\n'
     '      cpu_abi_cflags += [ "--target=x86_64-unknown-linux-gnu" ]\n'
     '      cpu_abi_ldflags += [ "--target=x86_64-unknown-linux-gnu" ]\n',
     '  if (current_cpu == "x64") {\n'
     '    if (is_haiku) {\n'
     '      cpu_abi_cflags += [ "--target=x86_64-unknown-haiku" ]\n'
     '      cpu_abi_ldflags += [ "--target=x86_64-unknown-haiku" ]\n'
     '    } else if (is_clang && !is_android && !is_fuchsia &&\n'
     '               !is_chromeos_device) {\n'
     '      cpu_abi_cflags += [ "--target=x86_64-unknown-linux-gnu" ]\n'
     '      cpu_abi_ldflags += [ "--target=x86_64-unknown-linux-gnu" ]\n'),

    # The build host is arm64 Linux. v8/gni/snapshot_toolchain.gni already
    # answers arm64 -> x64 with clang_arm64_v8_x64 under ${host_os}, but only
    # mac/BUILD.gn defines one; Linux needs the same toolchain.
    ("build/toolchain/linux/BUILD.gn",
     'clang_v8_toolchain("clang_x64_v8_arm64") {\n',
     'clang_v8_toolchain("clang_arm64_v8_x64") {\n'
     '  toolchain_args = {\n'
     '    current_cpu = "arm64"\n'
     '    v8_current_cpu = "x64"\n'
     '  }\n'
     '}\n'
     '\n'
     'clang_v8_toolchain("clang_x64_v8_arm64") {\n'),

    # The V8 snapshot is made by a real x64 mksnapshot, not an arm64 one
    # targeting x64: V8 has no x64 simulator, so an arm64 host binary would
    # have to run the x64 code it generates. clang_x64 builds it for x64
    # Linux against the Debian amd64 sysroot, and the arm64 host runs it
    # through Docker Desktop's Rosetta binfmt handler (setup-x64-host.sh).
    # qemu-user 8.2 cannot: it faults inside its own /proc/self/maps
    # emulation as soon as mksnapshot has reserved its pointer cage.
    ("v8/gni/snapshot_toolchain.gni",
     '  } else if (host_cpu == "arm64" && current_cpu == "x64") {\n'
     '    # Cross-build from arm64 to intel (likely on an Apple Silicon mac).\n'
     '    v8_snapshot_toolchain =\n'
     '        "//build/toolchain/${host_os}:clang_arm64_v8_$v8_current_cpu"\n',
     '  } else if (host_cpu == "arm64" && current_cpu == "x64" &&\n'
     '             target_os == "haiku") {\n'
     '    # Haiku x86_64 from arm64 Linux: an x64 mksnapshot under Rosetta.\n'
     '    # target_os, not is_haiku: inside clang_x64 this has to name itself.\n'
     '    v8_snapshot_toolchain = "//build/toolchain/linux:clang_x64"\n'
     '  } else if (host_cpu == "arm64" && current_cpu == "x64") {\n'
     '    # Cross-build from arm64 to intel (likely on an Apple Silicon mac).\n'
     '    v8_snapshot_toolchain =\n'
     '        "//build/toolchain/${host_os}:clang_arm64_v8_$v8_current_cpu"\n'),

    # Haiku's x86_64 <signal.h> (pulled in through <xmmintrin.h>'s malloc)
    # already has a struct fpu_state -- the FXSAVE area. pthreadpool's own
    # takes another name.
    ("third_party/pthreadpool/src/src/threadpool-utils.h",
     '#endif\n'
     '\n'
     'struct fpu_state {\n',
     '#endif\n'
     '\n'
     '#if defined(__HAIKU__)\n'
     '#define fpu_state pthreadpool_fpu_state\n'
     '#endif\n'
     '\n'
     'struct fpu_state {\n'),

    # The crash handler's register dump reads arm64's struct vregs. x86_64's
    # (headers/posix/arch/x86_64/signal.h) names them rip and rsp.
    ("base/debug/stack_trace_posix.cc",
     '    PrintToStderr("[haiku] elr=");\n'
     '    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.elr), 16, 16,\n'
     '                     reg_buf);\n'
     '    PrintToStderr(reg_buf);\n'
     '    PrintToStderr(" lr=");\n'
     '    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.lr), 16, 16,\n'
     '                     reg_buf);\n',
     '#if defined(__x86_64__)\n'
     '    PrintToStderr("[haiku] rip=");\n'
     '    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.rip), 16, 16,\n'
     '                     reg_buf);\n'
     '    PrintToStderr(reg_buf);\n'
     '    PrintToStderr(" rsp=");\n'
     '    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.rsp), 16, 16,\n'
     '                     reg_buf);\n'
     '#else\n'
     '    PrintToStderr("[haiku] elr=");\n'
     '    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.elr), 16, 16,\n'
     '                     reg_buf);\n'
     '    PrintToStderr(reg_buf);\n'
     '    PrintToStderr(" lr=");\n'
     '    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.lr), 16, 16,\n'
     '                     reg_buf);\n'
     '#endif\n'),

    # The CPU profiler's sampler, likewise.
    ("v8/src/libsampler/sampler.cc",
     '  state->lr = reinterpret_cast<void*>(mcontext.lr);\n'
     '#else\n'
     '#error Unsupported Haiku architecture for the sampler.\n',
     '  state->lr = reinterpret_cast<void*>(mcontext.lr);\n'
     '#elif V8_HOST_ARCH_X64\n'
     '  state->pc = reinterpret_cast<void*>(mcontext.rip);\n'
     '  state->sp = reinterpret_cast<void*>(mcontext.rsp);\n'
     '  state->fp = reinterpret_cast<void*>(mcontext.rbp);\n'
     '#else\n'
     '#error Unsupported Haiku architecture for the sampler.\n'),

    # libjpeg-turbo's NASM objects are ELF on Haiku too. Without ELF the
    # symbols get a leading underscore and jsimd.c cannot find them.
    ("third_party/libjpeg_turbo/BUILD.gn",
     '    } else if (is_linux || is_android || is_fuchsia || is_chromeos) {\n'
     '      defines += [ "ELF" ]\n',
     '    } else if (is_linux || is_android || is_fuchsia || is_chromeos ||\n'
     '               is_haiku) {\n'
     '      defines += [ "ELF" ]\n'),
]


def main(src):
    applied = skipped = 0
    for rel, old, new in EDITS:
        p = os.path.join(src, rel)
        if not os.path.exists(p):
            print("  MISSING %s" % rel)
            continue
        s = open(p).read()
        if new in s:
            skipped += 1
            continue
        n = s.count(old)
        if n != 1:
            print("  DRIFTED %s (anchor found %d times)" % (rel, n))
            continue
        open(p, "w").write(s.replace(old, new))
        print("  %s" % rel)
        applied += 1
    print("  (%d applied, %d already done)" % (applied, skipped))


if __name__ == "__main__":
    main(sys.argv[1])
