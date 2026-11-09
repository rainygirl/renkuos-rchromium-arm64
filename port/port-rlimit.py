#!/usr/bin/env python3
"""Read an unlimited getrlimit() the way either Haiku ABI writes it.

Haiku's RLIM_INFINITY used to be 0xffffffff on every platform. RenkuOS
picked up upstream's correction (baa65d5b53, "sys/resource.h: Correct
RLIM_INFINITY value"): on 64-bit platforms it is now __HAIKU_ADDR_MAX, all
ones. A binary built against the old header compares rlim_cur with
0xffffffff, gets all ones from the new kernel, and takes it for a limit:

- base::SysInfo::AmountOfVirtualMemory() wraps it in a ByteSize, whose
  CHECK refuses a value above INT64_MAX. Every renderer died on startup
  with SIGILL (brk #1) and no page was ever drawn.
- V8's SysInfo::AmountOfVirtualMemory() returns it as a real limit.

On Haiku both now treat either value as unlimited, so one binary works on
images from before and after the change, whichever header it was built
against.

Usage: port-rlimit.py <chromium src>. Idempotent.
"""
import sys

src = sys.argv[1]
MARK = "RCH rlimit"


def patch(path, old, new):
    full = "%s/%s" % (src, path)
    s = open(full).read()
    if MARK in s:
        print("  %s: already applied" % path)
        return
    n = s.count(old)
    if n != 1:
        print("  %s: DRIFTED (%r found %d times)" % (path, old[:60], n))
        sys.exit(1)
    open(full, "w").write(s.replace(old, new, 1))
    print("  %s: patched" % path)


patch("base/system/sys_info_posix.cc",
      "  return base::ByteSize(limit.rlim_cur == RLIM_INFINITY ? 0 : limit.rlim_cur);\n",
      """#if BUILDFLAG(IS_HAIKU)
  // RCH rlimit: Haiku's 64-bit RLIM_INFINITY changed from 0xffffffff to all
  // ones; accept both, whichever header this was built against.
  if (limit.rlim_cur == static_cast<rlim_t>(0xffffffffUL) ||
      limit.rlim_cur == static_cast<rlim_t>(-1)) {
    return base::ByteSize(0);
  }
#endif
  return base::ByteSize(limit.rlim_cur == RLIM_INFINITY ? 0 : limit.rlim_cur);
""")

patch("v8/src/base/sys-info.cc",
      "  return (rlim.rlim_cur == RLIM_INFINITY) ? 0 : rlim.rlim_cur;\n",
      """#if V8_OS_HAIKU
  // RCH rlimit: Haiku's 64-bit RLIM_INFINITY changed from 0xffffffff to all
  // ones; accept both, whichever header this was built against.
  if (rlim.rlim_cur == static_cast<rlim_t>(0xffffffffUL) ||
      rlim.rlim_cur == static_cast<rlim_t>(-1)) {
    return 0;
  }
#endif
  return (rlim.rlim_cur == RLIM_INFINITY) ? 0 : rlim.rlim_cur;
""")
