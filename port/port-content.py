#!/usr/bin/env python3
"""Make the //content tier build on Haiku.

Third-party libraries under content each carry their own platform detection,
the same way V8 does, so the pattern repeats: a missing branch does not
report itself as "unsupported platform" but as a type error or a missing
symbol somewhere further down.
"""
import os
import sys

EDITS = [
    # Haiku's pthread_t is a pointer, so the fallback here -- meant for nacl
    # and solaris, where it is an integer -- cannot narrow it to an int.
    # Haiku's own thread id is find_thread(nullptr), which is already the
    # int32 the rest of webrtc expects and matches what the kernel and every
    # Haiku tool report for the thread.
    ("third_party/webrtc/rtc_base/platform_thread_types.cc",
     "#elif defined(__EMSCRIPTEN__)\n"
     "  return static_cast<PlatformThreadId>(pthread_self());\n",
     "#elif defined(__HAIKU__)\n"
     "  return find_thread(nullptr);\n"
     "#elif defined(__EMSCRIPTEN__)\n"
     "  return static_cast<PlatformThreadId>(pthread_self());\n"),

    ("third_party/webrtc/rtc_base/platform_thread_types.cc",
     "#if defined(WEBRTC_FUCHSIA)\n"
     "#include <zircon/syscalls.h>\n",
     "#if defined(__HAIKU__)\n"
     "#include <OS.h>  // for find_thread\n"
     "#endif\n"
     "\n"
     "#if defined(WEBRTC_FUCHSIA)\n"
     "#include <zircon/syscalls.h>\n"),

    # ---- webrtc's own socket layer -------------------------------------
    #
    # Only the constants whose Linux numbers are unused by Haiku get defined.
    # Haiku's IPv6 options stop at 37, so 66 and 67 are free. The others --
    # IP_RECVTOS (13 is IP_DROP_MEMBERSHIP here), IP_MTU_DISCOVER (10 is
    # IP_MULTICAST_TTL) and the TCP_KEEP* family (4 is TCP_NOPUSH) -- would
    # alias a real, different option, so those call sites take webrtc's own
    # "not supported" path instead.
    ("third_party/webrtc/rtc_base/physical_socket_server.cc",
     "#include <sys/select.h>\n"
     "#include <unistd.h>\n"
     "#endif\n",
     "#include <sys/select.h>\n"
     "#include <unistd.h>\n"
     "\n"
     "#if defined(__HAIKU__)\n"
     "// RFC 3542 sections 6.5 and 6.4. Haiku's own IPv6 options stop at 37,\n"
     "// so these numbers are free; setsockopt answers ENOPROTOOPT, which the\n"
     "// callers already handle.\n"
     "#if !defined(IPV6_TCLASS)\n"
     "#define IPV6_TCLASS 67\n"
     "#endif\n"
     "#if !defined(IPV6_RECVTCLASS)\n"
     "#define IPV6_RECVTCLASS 66\n"
     "#endif\n"
     "#endif  // defined(__HAIKU__)\n"
     "#endif\n"),

    # Haiku's stack attaches no timestamp control message, so this comparison
    # could never match even if SCM_TIMESTAMP were given a number.
    ("third_party/webrtc/rtc_base/physical_socket_server.cc",
     "      if (timestamp && cmsg->cmsg_type == SCM_TIMESTAMP) {\n"
     "        timeval ts;\n"
     "        std::memcpy(static_cast<void*>(&ts), CMSG_DATA(cmsg), sizeof(ts));\n"
     "        *timestamp =\n"
     "            (Timestamp::Seconds(ts.tv_sec) + TimeDelta::Micros(ts.tv_usec))\n"
     "                .us();\n"
     "      }\n",
     "#if !defined(__HAIKU__)\n"
     "      // Haiku attaches no SCM_TIMESTAMP control message, so drop the\n"
     "      // whole branch rather than leave an unreachable body behind.\n"
     "      if (timestamp && cmsg->cmsg_type == SCM_TIMESTAMP) {\n"
     "        timeval ts;\n"
     "        std::memcpy(static_cast<void*>(&ts), CMSG_DATA(cmsg), sizeof(ts));\n"
     "        *timestamp =\n"
     "            (Timestamp::Seconds(ts.tv_sec) + TimeDelta::Micros(ts.tv_usec))\n"
     "                .us();\n"
     "      }\n"
     "#endif  // !defined(__HAIKU__)\n"),

    ("third_party/webrtc/rtc_base/physical_socket_server.cc",
     "  int value = 1;\n"
     "  // Attempt to get receive packet timestamp from the socket.\n"
     "  if (::setsockopt(s_, SOL_SOCKET, SO_TIMESTAMP, &value, sizeof(value)) != 0) {\n"
     "    RTC_DLOG(LS_ERROR) << \"::setsockopt failed. errno: \" << LAST_SYSTEM_ERROR;\n"
     "  }\n",
     "#if !defined(__HAIKU__)\n"
     "  int value = 1;\n"
     "  // Attempt to get receive packet timestamp from the socket.\n"
     "  if (::setsockopt(s_, SOL_SOCKET, SO_TIMESTAMP, &value, sizeof(value)) != 0) {\n"
     "    RTC_DLOG(LS_ERROR) << \"::setsockopt failed. errno: \" << LAST_SYSTEM_ERROR;\n"
     "  }\n"
     "#endif  // !defined(__HAIKU__)\n"),

    # Path MTU discovery: Haiku has no IP_MTU_DISCOVER. This mirrors what the
    # Mac and BSD arms of the same switch already do.
    ("third_party/webrtc/rtc_base/physical_socket_server.cc",
     "#elif defined(WEBRTC_POSIX)\n"
     "      *slevel = IPPROTO_IP;\n"
     "      *sopt = IP_MTU_DISCOVER;\n"
     "      break;\n"
     "#endif\n",
     "#elif defined(__HAIKU__)\n"
     "      RTC_LOG(LS_WARNING) << \"Socket::OPT_DONTFRAGMENT not supported.\";\n"
     "      return -1;\n"
     "#elif defined(WEBRTC_POSIX)\n"
     "      *slevel = IPPROTO_IP;\n"
     "      *sopt = IP_MTU_DISCOVER;\n"
     "      break;\n"
     "#endif\n"),

    # Receiving the ECN bits needs IP_RECVTOS on IPv4, which Haiku does not
    # have. IPv6 still works, so only the v4 arm gives up.
    ("third_party/webrtc/rtc_base/physical_socket_server.cc",
     "      if (family_ == AF_INET6) {\n"
     "        *slevel = IPPROTO_IPV6;\n"
     "        *sopt = IPV6_RECVTCLASS;\n"
     "      } else {\n"
     "        *slevel = IPPROTO_IP;\n"
     "        *sopt = IP_RECVTOS;\n"
     "      }\n"
     "      break;\n",
     "      if (family_ == AF_INET6) {\n"
     "        *slevel = IPPROTO_IPV6;\n"
     "        *sopt = IPV6_RECVTCLASS;\n"
     "      } else {\n"
     "#if defined(__HAIKU__)\n"
     "        // No IP_RECVTOS on Haiku, and no number that means it.\n"
     "        RTC_LOG(LS_WARNING) << \"Socket::OPT_RECV_ECN not supported.\";\n"
     "        return -1;\n"
     "#else\n"
     "        *slevel = IPPROTO_IP;\n"
     "        *sopt = IP_RECVTOS;\n"
     "#endif\n"
     "      }\n"
     "      break;\n"),

    # Haiku's TCP options are TCP_NODELAY, TCP_MAXSEG, TCP_NOPUSH and
    # TCP_NOOPT -- there is no keepalive tuning at all.
    ("third_party/webrtc/rtc_base/physical_socket_server.cc",
     "    case OPT_TCP_KEEPCNT:\n"
     "      *slevel = IPPROTO_TCP;\n"
     "      *sopt = TCP_KEEPCNT;\n"
     "      break;\n"
     "    case OPT_TCP_KEEPIDLE:\n"
     "      *slevel = IPPROTO_TCP;\n"
     "#if !defined(WEBRTC_MAC)\n"
     "      *sopt = TCP_KEEPIDLE;\n"
     "#else\n"
     "      *sopt = TCP_KEEPALIVE;\n"
     "#endif\n"
     "      break;\n"
     "    case OPT_TCP_KEEPINTVL:\n"
     "      *slevel = IPPROTO_TCP;\n"
     "      *sopt = TCP_KEEPINTVL;\n"
     "      break;\n",
     "    case OPT_TCP_KEEPCNT:\n"
     "    case OPT_TCP_KEEPIDLE:\n"
     "    case OPT_TCP_KEEPINTVL:\n"
     "#if defined(__HAIKU__)\n"
     "      // Haiku exposes no keepalive tuning knobs.\n"
     "      RTC_LOG(LS_WARNING) << \"Socket::OPT_TCP_KEEP* not supported.\";\n"
     "      return -1;\n"
     "#else\n"
     "      *slevel = IPPROTO_TCP;\n"
     "      switch (opt) {\n"
     "        case OPT_TCP_KEEPCNT:\n"
     "          *sopt = TCP_KEEPCNT;\n"
     "          break;\n"
     "        case OPT_TCP_KEEPIDLE:\n"
     "#if !defined(WEBRTC_MAC)\n"
     "          *sopt = TCP_KEEPIDLE;\n"
     "#else\n"
     "          *sopt = TCP_KEEPALIVE;\n"
     "#endif\n"
     "          break;\n"
     "        default:\n"
     "          *sopt = TCP_KEEPINTVL;\n"
     "          break;\n"
     "      }\n"
     "      break;\n"
     "#endif\n"),

    # Same substitution as //net: Haiku has IFF_LINK, not IFF_RUNNING.
    ("third_party/webrtc/rtc_base/network.cc",
     "    if (!(cursor->ifa_flags & IFF_RUNNING)) {\n",
     "#if defined(__HAIKU__)\n"
     "    // Haiku has no IFF_RUNNING; IFF_LINK carries the same sense.\n"
     "    if (!(cursor->ifa_flags & IFF_LINK)) {\n"
     "#else\n"
     "    if (!(cursor->ifa_flags & IFF_RUNNING)) {\n"
     "#endif\n"),

    # ---- platform enumerations missing a Haiku arm ---------------------
    #
    # Variations studies are keyed by platform. BSD and Solaris already ride
    # along with Linux here for exactly this reason, and Haiku is in the same
    # position: unsupported by Chrome, but it has to name something.
    ("components/variations/client_filterable_state.cc",
     "#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_BSD) || BUILDFLAG(IS_SOLARIS)\n",
     "#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_BSD) || BUILDFLAG(IS_SOLARIS) || \\\n"
     "    BUILDFLAG(IS_HAIKU)\n"),

    # The keycode table needs a native scan-code column. Haiku's raw key codes
    # are its own and match none of the existing columns, so take the usb
    # column -- the identity mapping Fuchsia uses for the same reason. The
    # Ozone backend would have to report USB HID usages for DomCode fidelity;
    # until it does, DomCode simply stays kNone, which is the honest result.
    ("ui/events/keycodes/dom/keycode_converter.cc",
     "#elif BUILDFLAG(IS_FUCHSIA)\n"
     "#define DOM_CODE(usb, evdev, xkb, win, mac, code, id) \\\n"
     "  { usb, usb, code }\n",
     "#elif BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_HAIKU)\n"
     "#define DOM_CODE(usb, evdev, xkb, win, mac, code, id) \\\n"
     "  { usb, usb, code }\n"),


    # Haiku has no native pixmaps: NativePixmapPlane carries neither an fd nor
    # a vmo, so handle.planes can never hold anything to clone. Checked once
    # outside the loop, not with a NOTREACHED() inside one -- an earlier
    # version of this fix did that, and the compiler treats NOTREACHED() as
    # noreturn, which makes a ranged-for whose entire body is that call
    # unable to reach its own increment; -Wunreachable-code-loop-increment
    # (rightly) flags it, and drags an "unused variable 'plane'" error along.
    ("ui/gfx/native_pixmap_handle.cc",
     "  NativePixmapHandle clone;\n"
     "  for (auto& plane : handle.planes) {\n"
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n"
     "    DCHECK(plane.fd.is_valid());\n",
     "  NativePixmapHandle clone;\n"
     "#if BUILDFLAG(IS_HAIKU)\n"
     "  // Haiku has no native pixmaps: NativePixmapPlane carries neither an fd nor\n"
     "  // a vmo, so handle.planes can never hold anything to clone. Checked once\n"
     "  // outside the loop rather than with a NOTREACHED() inside one -- the\n"
     "  // compiler treats NOTREACHED() as noreturn, and having it as the entire\n"
     "  // body of a ranged-for makes the loop unable to reach its own increment,\n"
     "  // which -Wunreachable-code-loop-increment (rightly) flags as suspicious.\n"
     "  DCHECK(handle.planes.empty());\n"
     "#else\n"
     "  for (auto& plane : handle.planes) {\n"
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n"
     "    DCHECK(plane.fd.is_valid());\n"),

    ("ui/gfx/native_pixmap_handle.cc",
     "#error Unsupported OS\n"
     "#endif\n"
     "  }\n"
     "\n"
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n"
     "  clone.modifier = handle.modifier;\n",
     "#error Unsupported OS\n"
     "#endif\n"
     "  }\n"
     "#endif  // BUILDFLAG(IS_HAIKU)\n"
     "\n"
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n"
     "  clone.modifier = handle.modifier;\n"),

    # Dawn's Vulkan backend (BackendVk.cpp and friends) gets pulled in even
    # with Chromium's own enable_vulkan off for Haiku, because
    # dawn_enable_vulkan separately ORs in dawn_use_swiftshader, which
    # defaults true on every platform except Android/iOS. Haiku has no
    # Vulkan driver and no SwiftShader Vulkan build here, so it joins that
    # exclusion.
    ("third_party/dawn/scripts/dawn_features.gni",
     "if (is_android || is_ios) {\n"
     "  dawn_use_swiftshader = false\n"
     "} else {\n",
     "if (is_android || is_ios || is_haiku) {\n"
     "  # Haiku has no Vulkan driver and no SwiftShader Vulkan build here, and this\n"
     "  # default is what pulls in Dawn's whole Vulkan backend (BackendVk.cpp and\n"
     "  # friends) even with Chromium's own enable_vulkan already off for Haiku --\n"
     "  # dawn_enable_vulkan below ORs in dawn_use_swiftshader unconditionally.\n"
     "  dawn_use_swiftshader = false\n"
     "} else {\n"),

    # IPC::ParamTraits<long>/<unsigned long>: Chromium's own comment explains
    # why this specialization exists for Linux and Fuchsia -- int64_t is
    # typedef'd to long on their LP64 targets, so code that writes an
    # int64_t through ParamTraits<int64_t> actually instantiates
    # ParamTraits<long>. Haiku's arm64 target follows the same convention.
    ("ipc/param_traits_utils.h",
     "#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \\\n"
     "    BUILDFLAG(IS_FUCHSIA) ||                                              \\\n"
     "    (BUILDFLAG(IS_ANDROID) && defined(ARCH_CPU_64_BITS))\n"
     "template <>\n"
     "struct ParamTraits<long> {\n",
     "#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \\\n"
     "    BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_HAIKU) ||                       \\\n"
     "    (BUILDFLAG(IS_ANDROID) && defined(ARCH_CPU_64_BITS))\n"
     "// Haiku follows the same LP64 convention as Linux and Fuchsia here --\n"
     "// int64_t is long, confirmed against this cross toolchain directly\n"
     "// (static_assert(std::is_same<int64_t, long>::value) passes) -- so it needs\n"
     "// this specialization for exactly the reason the Linux/Fuchsia comment above\n"
     "// gives: code that writes an int64_t through ParamTraits<int64_t> actually\n"
     "// instantiates this one.\n"
     "template <>\n"
     "struct ParamTraits<long> {\n"),

    # ---- Dawn (WebGPU) --------------------------------------------------
    #
    # The fifth independent platform-detection layer in this tree, after
    # build_config.h, PartitionAlloc, perfetto and V8. Dawn only ever asks
    # DAWN_PLATFORM_IS(POSIX) in the code that failed here -- dlopen, file
    # descriptors -- all of which Haiku provides, so naming it POSIX is
    # enough. The 20 errors this produced were one #error plus the members
    # that stopped being declared underneath it.
    ("third_party/dawn/src/utils/platform.h",
     "#elif defined(__Fuchsia__)\n"
     "#define DAWN_PLATFORM_IS_FUCHSIA 1\n"
     "#define DAWN_PLATFORM_IS_POSIX 1\n",
     "#elif defined(__HAIKU__)\n"
     "#define DAWN_PLATFORM_IS_HAIKU 1\n"
     "#define DAWN_PLATFORM_IS_POSIX 1\n"
     "\n"
     "#elif defined(__Fuchsia__)\n"
     "#define DAWN_PLATFORM_IS_FUCHSIA 1\n"
     "#define DAWN_PLATFORM_IS_POSIX 1\n"),

    ("third_party/dawn/src/utils/platform.h",
     "//  - POSIX\n",
     "//  - POSIX\n"
     "//      - HAIKU\n"),


    # Same story one layer up: IS_OZONE is true for Haiku, so this function is
    # compiled, but neither the Linux nor the Fuchsia arm applies and it falls
    # off the end without returning. A plane on Haiku carries no handle at all
    # -- the struct has neither fd nor vmo -- so there is nothing to hand over
    # and nothing should be asking.
    ("ui/gfx/mojom/native_handle_types_mojom_traits.cc",
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n"
     "  return mojo::PlatformHandle(std::move(plane.fd));\n"
     "#elif BUILDFLAG(IS_FUCHSIA)\n"
     "  return mojo::PlatformHandle(std::move(plane.vmo));\n"
     "#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n",
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n"
     "  return mojo::PlatformHandle(std::move(plane.fd));\n"
     "#elif BUILDFLAG(IS_FUCHSIA)\n"
     "  return mojo::PlatformHandle(std::move(plane.vmo));\n"
     "#elif BUILDFLAG(IS_HAIKU)\n"
     "  NOTREACHED();\n"
     "#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n"),

    # Dawn: the executable's own path. Haiku has no /proc/self/exe, but the
    # kernel enumerates loaded images and marks one B_APP_IMAGE -- that is the
    # executable, and its name is the full path.
    ("third_party/dawn/src/dawn/common/SystemUtils.cpp",
     "#elif DAWN_PLATFORM_IS(FUCHSIA)\n"
     "std::optional<std::string> GetExecutablePath() {\n"
     "    // UNIMPLEMENTED\n"
     "    return {};\n"
     "}\n",
     "#elif DAWN_PLATFORM_IS(HAIKU)\n"
     "std::optional<std::string> GetExecutablePath() {\n"
     "    image_info info;\n"
     "    int32 cookie = 0;\n"
     "    while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {\n"
     "        if (info.type == B_APP_IMAGE) {\n"
     "            return info.name;\n"
     "        }\n"
     "    }\n"
     "    return {};\n"
     "}\n"
     "#elif DAWN_PLATFORM_IS(FUCHSIA)\n"
     "std::optional<std::string> GetExecutablePath() {\n"
     "    // UNIMPLEMENTED\n"
     "    return {};\n"
     "}\n"),

    # GetModulePath already has a dladdr implementation, and Haiku provides
    # dladdr as a GNU extension in <dlfcn.h>, so it just needs to be let in.
    ("third_party/dawn/src/dawn/common/SystemUtils.cpp",
     "#if DAWN_PLATFORM_IS(LINUX) || DAWN_PLATFORM_IS(MACOS) || DAWN_PLATFORM_IS(IOS)\n"
     "std::optional<std::string> GetModulePath() {\n",
     "#if DAWN_PLATFORM_IS(LINUX) || DAWN_PLATFORM_IS(MACOS) || \\\n"
     "    DAWN_PLATFORM_IS(IOS) || DAWN_PLATFORM_IS(HAIKU)\n"
     "std::optional<std::string> GetModulePath() {\n"),

    ("third_party/dawn/src/dawn/common/SystemUtils.cpp",
     "#include \"src/dawn/common/SystemUtils.h\"\n",
     "#include \"src/dawn/common/SystemUtils.h\"\n"
     "\n"
     "#if defined(__HAIKU__)\n"
     "#include <OS.h>\n"
     "#include <image.h>\n"
     "#endif\n"),

    # libvpx: no runtime feature detection on Haiku -- there is no getauxval
    # and no sysctl for the ID registers. NEON is mandatory in Armv8.0-A, so
    # report that and nothing more; the optional extensions stay off, which
    # costs speed but never correctness.
    ("third_party/libvpx/source/libvpx/vpx_ports/aarch64_cpudetect.c",
     "#else  // end __Fuchsia__\n"
     "#error \\\n"
     "    \"Runtime CPU detection selected, but no CPU detection method available\" \\\n"
     "\"for your platform. Rerun configure with --disable-runtime-cpu-detect.\"\n"
     "#endif\n",
     "#elif defined(__HAIKU__)  // end __Fuchsia__\n"
     "\n"
     "static int arm_get_cpu_caps(void) {\n"
     "  int flags = 0;\n"
     "#if HAVE_NEON\n"
     "  flags |= HAS_NEON;  // Neon is mandatory in Armv8.0-A.\n"
     "#endif  // HAVE_NEON\n"
     "  // Haiku exposes neither getauxval nor the ID registers, so the\n"
     "  // optional extensions cannot be probed and stay disabled.\n"
     "  return flags;\n"
     "}\n"
     "\n"
     "#else  // end __HAIKU__\n"
     "#error \\\n"
     "    \"Runtime CPU detection selected, but no CPU detection method available\" \\\n"
     "\"for your platform. Rerun configure with --disable-runtime-cpu-detect.\"\n"
     "#endif\n"),

    # ---- bundled libc++ -------------------------------------------------
    #
    # _LIBCPP_ELAST is only needed where strerror cannot cope with an errno
    # outside the known range. Haiku's strerror formats an "unknown" string
    # for anything it does not recognise (src/system/libroot/posix/string/
    # strerror.c), so it needs no ELAST -- the same as Fuchsia and Apple.
    ("third_party/libc++/src/src/include/config_elast.h",
     "#else\n"
     "// Warn here so that the person doing the libcxx port has an easier time:\n"
     "#  warning ELAST for this platform not yet implemented\n"
     "#endif\n",
     "#elif defined(__HAIKU__)\n"
     "// No _LIBCPP_ELAST needed on Haiku: strerror handles unknown values.\n"
     "#else\n"
     "// Warn here so that the person doing the libcxx port has an easier time:\n"
     "#  warning ELAST for this platform not yet implemented\n"
     "#endif\n"),

    # Haiku's ctype table follows the glibc convention exactly: __ctype_b_loc()
    # yields a pointer to the entry for 0, with indices from -128 to 255 valid
    # (see the range check in libroot's ctype.cpp). That is precisely what this
    # function is expected to return, and what the Emscripten arm already does.
    # The matching ctype_base::mask arm in <__locale> already names Haiku.
    ("third_party/libc++/src/src/locale.cpp",
     "#  elif defined(__EMSCRIPTEN__)\n"
     "  return *__ctype_b_loc();\n",
     "#  elif defined(__EMSCRIPTEN__) || defined(__HAIKU__)\n"
     "  return *__ctype_b_loc();\n"),

    # Follow-up to my own GetModulePath change: that arm needs <dlfcn.h>, and
    # the include block only pulls it in for Linux/Apple. Haiku's dlfcn.h
    # declares Dl_info unconditionally, so it only had to be reached.
    ("third_party/dawn/src/dawn/common/SystemUtils.cpp",
     "#elif DAWN_PLATFORM_IS(LINUX)\n"
     "#include <dlfcn.h>\n"
     "#include <limits.h>\n"
     "#include <unistd.h>\n",
     "#elif DAWN_PLATFORM_IS(LINUX) || DAWN_PLATFORM_IS(HAIKU)\n"
     "#include <dlfcn.h>\n"
     "#include <limits.h>\n"
     "#include <unistd.h>\n"),

    # libaom carries its own copy of the same arm64 CPU probe as libvpx, with
    # the same lack of a Haiku path. Same answer: NEON is architectural on
    # Armv8.0-A, and nothing else can be detected without getauxval.
    ("third_party/libaom/source/libaom/aom_ports/aarch64_cpudetect.c",
     "#else  // end __Fuchsia__\n"
     "#error \\\n"
     "    \"Runtime CPU detection selected, but no CPU detection method \" \\\n"
     "\"available for your platform. Rerun cmake with -DCONFIG_RUNTIME_CPU_DETECT=0.\"\n"
     "#endif\n",
     "#elif defined(__HAIKU__)  // end __Fuchsia__\n"
     "\n"
     "static int arm_get_cpu_caps(void) {\n"
     "  int flags = 0;\n"
     "#if HAVE_NEON\n"
     "  flags |= HAS_NEON;  // Neon is mandatory in Armv8.0-A.\n"
     "#endif  // HAVE_NEON\n"
     "  // Haiku exposes neither getauxval nor the ID registers.\n"
     "  return flags;\n"
     "}\n"
     "\n"
     "#else  // end __HAIKU__\n"
     "#error \\\n"
     "    \"Runtime CPU detection selected, but no CPU detection method \" \\\n"
     "\"available for your platform. Rerun cmake with -DCONFIG_RUNTIME_CPU_DETECT=0.\"\n"
     "#endif\n"),

    # dav1d: the checked-in arm64 config says HAVE_GETAUXVAL, which is true
    # for the Linux config it was generated for but not here. Rather than
    # fork the config -- it is shared with the Linux build -- exclude Haiku
    # where the header is pulled in and where the probe runs.
    ("third_party/dav1d/libdav1d/src/cpu.c",
     "#if HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO\n"
     "#include <sys/auxv.h>\n"
     "#endif\n",
     "#if (HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO) && !defined(__HAIKU__)\n"
     "#include <sys/auxv.h>\n"
     "#endif\n"),

    ("third_party/dav1d/libdav1d/src/arm/cpu.c",
     "#if HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO\n",
     "#if (HAVE_GETAUXVAL || HAVE_ELF_AUX_INFO) && !defined(__HAIKU__)\n"),

    # farmhash: no <byteswap.h> on Haiku, and its <endian.h> carries only the
    # byte-order constants, no swap helpers. The compiler builtins are exactly
    # what byteswap.h wraps anyway.
    ("third_party/farmhash/src/src/farmhash.cc",
     "#else\n"
     "\n"
     "#undef bswap_32\n"
     "#undef bswap_64\n"
     "#include <byteswap.h>\n"
     "\n"
     "#endif\n",
     "#elif defined(__HAIKU__)\n"
     "\n"
     "#undef bswap_32\n"
     "#undef bswap_64\n"
     "#define bswap_32(x) __builtin_bswap32(x)\n"
     "#define bswap_64(x) __builtin_bswap64(x)\n"
     "\n"
     "#else\n"
     "\n"
     "#undef bswap_32\n"
     "#undef bswap_64\n"
     "#include <byteswap.h>\n"
     "\n"
     "#endif\n"),

    # SwiftShader uses ptrace(PTRACE_TRACEME) to notice a debugger. Haiku
    # defines __unix__ but ships no <sys/ptrace.h> and has no ptrace at all;
    # leaving PTRACE undefined compiles out the check, which then reports
    # "no debugger attached" -- true, since there is no way to ask.
    ("third_party/swiftshader/src/System/Debug.cpp",
     "#if defined(__unix__)\n"
     "#\tdefine PTRACE\n"
     "#\tinclude <sys/ptrace.h>\n",
     "#if defined(__unix__) && !defined(__HAIKU__)\n"
     "#\tdefine PTRACE\n"
     "#\tinclude <sys/ptrace.h>\n"),

    # The Reactor copy of the same debugger check.
    ("third_party/swiftshader/src/Reactor/Debug.cpp",
     "#if defined(__unix__)\n"
     "#\tdefine PTRACE\n"
     "#\tinclude <sys/ptrace.h>\n",
     "#if defined(__unix__) && !defined(__HAIKU__)\n"
     "#\tdefine PTRACE\n"
     "#\tinclude <sys/ptrace.h>\n"),

    # ---- Vulkan loader --------------------------------------------------
    #
    # ui/gl attaches the Vulkan loader as a data_dep wherever
    # angle_shared_libvulkan is set, so it gets built even though
    # enable_vulkan is already false for Haiku. There is no Vulkan driver on
    # Haiku arm64 and the Ozone backend here renders in software, so the
    # loader would be dead weight -- and porting it means teaching its
    # platform abstraction (vk_loader_platform.h) about Haiku from scratch:
    # dirent, dlopen wrappers, mutexes, the lot.
    #
    # Turning the flag off at the source removes every one of those
    # data_deps at once. The remaining unconditional reference in ui/gl sits
    # inside an is_mac block and never applies here.
    ("third_party/angle/gni/angle.gni",
     "  angle_shared_libvulkan = !is_mac || angle_shared_libvulkan_on_mac\n",
     "  # Haiku has no Vulkan driver, so the loader is never useful here and\n"
     "  # its platform layer has no Haiku port.\n"
     "  angle_shared_libvulkan =\n"
     "      (!is_mac || angle_shared_libvulkan_on_mac) && !is_haiku\n"),

    # ---- ffmpeg ---------------------------------------------------------
    #
    # Chromium ships pre-generated ffmpeg configs per (branding, os, arch) and
    # picks the directory from current_os, so Haiku looks for a
    # chromium/config/Chromium/haiku/arm64 that does not exist -- and every
    # translation unit then fails to find config.h or avconfig.h. That single
    # missing directory accounted for 214 of 239 build failures.
    #
    # ChromeOS and Fuchsia already borrow the linux config for the same
    # reason; the generated config is about codec and feature selection rather
    # than kernel specifics, and Fuchsia is at least as far from Linux as
    # Haiku is.
    ("third_party/ffmpeg/ffmpeg_options.gni",
     "} else if (is_chromeos || is_fuchsia) {\n"
     "  os_config = \"linux\"\n",
     "} else if (is_chromeos || is_fuchsia || is_haiku) {\n"
     "  os_config = \"linux\"\n"),

    # marl's page allocator is plain mmap/sysconf; Haiku has both.
    ("third_party/swiftshader/third_party/marl/src/memory.cpp",
     "#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) || defined(__EMSCRIPTEN__)\n",
     "#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) || \\\n"
     "    defined(__EMSCRIPTEN__) || defined(__HAIKU__)\n"),

    # The other half of the dav1d auxv change: the wrapper itself still calls
    # getauxval when the config says HAVE_GETAUXVAL, and the declaration is
    # gone now that <sys/auxv.h> is excluded. Fall through to the stub.
    ("third_party/dav1d/libdav1d/src/cpu.c",
     "COLD unsigned long dav1d_getauxval(unsigned long type) {\n"
     "#if HAVE_GETAUXVAL\n",
     "COLD unsigned long dav1d_getauxval(unsigned long type) {\n"
     "#if HAVE_GETAUXVAL && !defined(__HAIKU__)\n"),

    # ---- clipboard ------------------------------------------------------
    #
    # clipboard_ozone.cc is compiled for every Ozone platform, Haiku included,
    # and reaches for these names. They are the X11 selection target atoms
    # ("STRING", "UTF8_STRING", "TEXT") plus Chromium's own source-url type --
    # nothing Linux-specific, just the set the Ozone clipboard speaks. The
    # guard simply predates a non-Linux Ozone platform existing.
    ("ui/base/clipboard/clipboard_constants.h",
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n"
     "inline constexpr char kMimeTypeLinuxUtf8String[] = \"UTF8_STRING\";\n",
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \\\n"
     "    BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_HAIKU)\n"
     "inline constexpr char kMimeTypeLinuxUtf8String[] = \"UTF8_STRING\";\n"),

    ("ui/base/clipboard/clipboard_constants.h",
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || \\\n"
     "    BUILDFLAG(IS_ANDROID)\n"
     "inline constexpr char kMimeTypeSourceUrl[] = \"chromium/x-source-url\";\n",
     "#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || \\\n"
     "    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_HAIKU)\n"
     "inline constexpr char kMimeTypeSourceUrl[] = \"chromium/x-source-url\";\n"),

    # ---- SwiftShader's bundled LLVM 10 ----------------------------------
    #
    # SwiftShader picks an LLVM config directory per OS and has no Haiku one,
    # so the build uses the linux config -- which declares HAVE_MALLINFO.
    # Haiku's malloc reports nothing of the sort. Falling through lands on the
    # "cannot get malloc info" warning path, which returns 0; that is only a
    # statistic and nothing depends on it being accurate.
    ("third_party/swiftshader/third_party/llvm-10.0/llvm/lib/Support/Unix/Process.inc",
     "#elif defined(HAVE_MALLINFO)\n"
     "  struct mallinfo mi;\n",
     "#elif defined(HAVE_MALLINFO) && !defined(__HAIKU__)\n"
     "  struct mallinfo mi;\n"),

    # GetMainExecutable falls past every OS arm on Haiku and lands on the
    # dladdr fallback, which the linux config does not enable. Rather than
    # touch a config shared with the Linux build, name Haiku directly and use
    # what the kernel already knows: the image marked B_APP_IMAGE is the
    # executable, and its name is the full path.
    ("third_party/swiftshader/third_party/llvm-10.0/llvm/lib/Support/Unix/Path.inc",
     "#else\n"
     "#error GetMainExecutable is not implemented on this host yet.\n"
     "#endif\n",
     "#elif defined(__HAIKU__)\n"
     "  image_info info;\n"
     "  int32 cookie = 0;\n"
     "  while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {\n"
     "    if (info.type == B_APP_IMAGE)\n"
     "      return info.name;\n"
     "  }\n"
     "#else\n"
     "#error GetMainExecutable is not implemented on this host yet.\n"
     "#endif\n"),

    ("third_party/swiftshader/third_party/llvm-10.0/llvm/lib/Support/Unix/Path.inc",
     "#include <limits.h>\n",
     "#include <limits.h>\n"
     "\n"
     "#if defined(__HAIKU__)\n"
     "#include <OS.h>\n"
     "#include <image.h>\n"
     "#endif\n"),

        # OpenBSD already piggybacks on kOsLinux for GPU test-config bucketing;
    # Haiku joins it for the same reason (no Haiku-specific test-config data).
("gpu/config/gpu_test_config.cc",
     '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_OPENBSD)\n'
     '  return GPUTestConfig::kOsLinux;\n',
     '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_OPENBSD) || BUILDFLAG(IS_HAIKU)\n'
     '  // This enum is a bucket for matching GPU test-config blocklist/allowlist\n'
     '  // entries by OS name, not a claim about what the OS actually is --\n'
     '  // OpenBSD already piggybacks on kOsLinux here for the same reason, and\n'
     '  // there is no Haiku-specific test-config data to bucket separately.\n'
     '  return GPUTestConfig::kOsLinux;\n'),
        # Haiku's Ozone backend has no drag'n'drop provider, so it must take the
    # same NonBacked fallback path Linux/ChromeOS/Fuchsia already define.
("ui/base/dragdrop/os_exchange_data_provider_factory.cc",
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n'
     '#include "ui/base/dragdrop/os_exchange_data_provider_factory_ozone.h"\n',
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || \\\n'
     '    BUILDFLAG(IS_HAIKU)\n'
     '#include "ui/base/dragdrop/os_exchange_data_provider_factory_ozone.h"\n'),
        # Same file, second hunk: the actual fallback logic in CreateProvider().
("ui/base/dragdrop/os_exchange_data_provider_factory.cc",
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
     '  // The instance can be nullptr in tests that do not instantiate the platform,\n'
     "  // or on platforms that do not implement specific drag'n'drop.  For them,\n"
     '  // falling back to the Aura provider should be fine.\n'
     '  if (auto* factory = OSExchangeDataProviderFactoryOzone::Instance()) {\n',
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_HAIKU)\n'
     '  // The instance can be nullptr in tests that do not instantiate the platform,\n'
     "  // or on platforms that do not implement specific drag'n'drop.  For them,\n"
     "  // falling back to the Aura provider should be fine. Haiku's Ozone backend\n"
     '  // is one of those platforms: it has no OSExchangeDataProviderFactoryOzone\n'
     '  // implementation, so this always takes the fallback.\n'
     '  if (auto* factory = OSExchangeDataProviderFactoryOzone::Instance()) {\n'),
        # angle_enable_gl's "!is_linux" clause defaults every non-Linux platform
    # to enabled; Haiku has no GL driver (see haiku_surface_factory.cc's
    # GetAllowedGLImplementations returning empty), so exclude it explicitly.
("third_party/angle/gni/angle.gni",
     '  angle_enable_gl =\n'
     '      (ozone_platform_drm || ozone_platform_wayland || !is_linux ||\n'
     '       ((angle_use_x11 && !is_chromeos) || angle_use_wayland || is_castos)) &&\n'
     '      !is_fuchsia && !angle_is_winuwp && !is_win_arm64 && !is_ios\n',
     '  angle_enable_gl =\n'
     '      (ozone_platform_drm || ozone_platform_wayland || !is_linux ||\n'
     '       ((angle_use_x11 && !is_chromeos) || angle_use_wayland || is_castos)) &&\n'
     '      !is_fuchsia && !angle_is_winuwp && !is_win_arm64 && !is_ios &&\n'
     '      # The "!is_linux" clause above means any non-Linux platform falls\n'
     '      # through to enabled by default, and Haiku has no GL driver: my Ozone\n'
     '      # backend already reports GetAllowedGLImplementations() as empty, so\n'
     "      # ANGLE's GL backend is dead code here, and libANGLE/Display.cpp's own\n"
     '      # #error "Unsupported OpenGL platform." fires once ANGLE_ENABLE_OPENGL\n'
     '      # gets defined for a platform its renderer/gl/ backend has no case for.\n'
     '      !is_haiku\n'),

        # current_os falls through unmatched to Google's variations schema,
    # which has no Haiku Platform enum value; bucket it under linux.
("components/variations/field_trial_config/BUILD.gn",
     '  if (current_os == "win") {\n'
     '    platform = "windows"\n'
     '  } else {\n'
     '    platform = current_os\n'
     '  }\n',
     '  if (current_os == "win") {\n'
     '    platform = "windows"\n'
     '  } else if (current_os == "haiku") {\n'
     "    # Haiku isn't (and won't be) a recognized field-trial platform in\n"
     "    # Google's variations schema -- study.proto's Platform enum only lists\n"
     '    # win/mac/linux/chromeos/android/ios/android_webview/fuchsia. Bucket it\n'
     '    # under linux for field-trial testing-config purposes.\n'
     '    platform = "linux"\n'
     '  } else {\n'
     '    platform = current_os\n'
     '  }\n'),
        # net::AddressFamily has no AF_UNIX member; the comparison is dead code
    # on every platform, but only Haiku's AF_UNIX value falls outside the
    # enum's range for clang's tautological-compare checker to catch.
("services/network/public/cpp/socket_broker_impl.cc",
     '  if (address_family == AF_UNIX) {\n',
     '#if BUILDFLAG(IS_HAIKU)\n'
     '  // net::AddressFamily only enumerates UNSPECIFIED/IPV4/IPV6 -- it has no\n'
     '  // AF_UNIX member on any platform, so this comparison is a static no-op\n'
     "  // everywhere. Other platforms' AF_UNIX value happens to coincide with a\n"
     '  // valid AddressFamily enumerator (e.g. AF_UNIX == 1 on Linux, same as\n'
     "  // ADDRESS_FAMILY_IPV4), so clang's range checker doesn't flag it there;\n"
     "  // Haiku's AF_UNIX (9) falls outside the enum's range and trips\n"
     '  // -Wtautological-constant-out-of-range-compare. Cast to make the\n'
     '  // (already-dead) comparison well-typed instead of changing behavior.\n'
     '  if (static_cast<int>(address_family) == AF_UNIX) {\n'
     '#else\n'
     '  if (address_family == AF_UNIX) {\n'
     '#endif\n'),

        # ALSA and PulseAudio were enabled for "any POSIX that isn't Android or
    # Apple", which swept in Haiku; it has neither library.
("media/media_options.gni",
     '  if (is_posix && !is_android && !is_apple &&\n',
     '  # Haiku is POSIX but has neither ALSA nor PulseAudio; audio goes through\n'
     '  # its own Media Kit (BSoundPlayer), so this enumeration must exclude it\n'
     '  # rather than defaulting both libraries on for "some POSIX system".\n'
     '  if (is_posix && !is_android && !is_apple && !is_haiku &&\n'),
        # No platform branch matched Haiku, so libxslt got no include_dirs at all
    # and libxslt.h's #include "config.h" failed. The linux config is a plain
    # autoconf one whose HAVE_*_H headers Haiku all provides.
("third_party/libxslt/BUILD.gn",
     '  if (is_linux || is_chromeos) {\n',
     '  if (is_linux || is_chromeos || is_haiku) {\n'),
    ("third_party/libxslt/BUILD.gn",
     '  if (is_linux || is_chromeos || is_android || is_fuchsia) {\n',
     '  # "linux/config.h" is a plain autoconf config.h: every HAVE_*_H it sets is a\n'
     "  # standard POSIX header that Haiku's sysroot also provides, so Haiku shares\n"
     '  # it rather than needing a directory of its own. Without a branch here Haiku\n'
     '  # gets no include_dirs at all and libxslt.h\'s #include "config.h" fails.\n'
     '  if (is_linux || is_chromeos || is_android || is_fuchsia || is_haiku) {\n'),
        # The crate ships unix/haiku/aarch64.rs but the generated GN sources list
    # names only x86_64.rs, and Chromium's rust wrapper rejects any input not
    # in sources. This also unblocked the font_format and web_package rust
    # bindgen headers that depend on the libc crate.
("third_party/rust/libc/v0_2/BUILD.gn",
     '    "//third_party/rust/chromium_crates_io/vendor/libc-v0_2/src/unix/cygwin/mod.rs",\n'
     '    "//third_party/rust/chromium_crates_io/vendor/libc-v0_2/src/unix/haiku/b32.rs",\n',
     '    "//third_party/rust/chromium_crates_io/vendor/libc-v0_2/src/unix/cygwin/mod.rs",\n'
     '    "//third_party/rust/chromium_crates_io/vendor/libc-v0_2/src/unix/haiku/aarch64.rs",\n'
     '    "//third_party/rust/chromium_crates_io/vendor/libc-v0_2/src/unix/haiku/b32.rs",\n'),
        # Haiku maps to the shared linux ffmpeg config (HAVE_GETAUXVAL=1) but has
    # no getauxval()/<sys/auxv.h>; ffmpeg's own #else fallbacks are correct.
("third_party/ffmpeg/libavutil/cpu.c",
     '#include "config.h"\n'
     '\n',
     '#include "config.h"\n'
     '\n'
     '#if defined(__HAIKU__)\n'
     '/* Haiku maps to the shared linux ffmpeg config, which sets HAVE_GETAUXVAL=1.\n'
     '   Haiku has neither getauxval() nor elf_aux_info(), and no <sys/auxv.h>.\n'
     "   ffmpeg's own #else fallbacks are already correct for that case\n"
     '   (detect_flags() returns 0; ff_getauxval() sets ENOSYS), so correct the\n'
     '   define here rather than forking a whole Haiku config directory. */\n'
     '#undef HAVE_GETAUXVAL\n'
     '#define HAVE_GETAUXVAL 0\n'
     '#undef HAVE_ELF_AUX_INFO\n'
     '#define HAVE_ELF_AUX_INFO 0\n'
     '#endif\n'
     '\n'),
    ("third_party/ffmpeg/libavutil/aarch64/cpu.c",
     '#include "config.h"\n'
     '\n',
     '#include "config.h"\n'
     '\n'
     '#if defined(__HAIKU__)\n'
     '/* Haiku maps to the shared linux ffmpeg config, which sets HAVE_GETAUXVAL=1.\n'
     '   Haiku has neither getauxval() nor elf_aux_info(), and no <sys/auxv.h>.\n'
     "   ffmpeg's own #else fallbacks are already correct for that case\n"
     '   (detect_flags() returns 0; ff_getauxval() sets ENOSYS), so correct the\n'
     '   define here rather than forking a whole Haiku config directory. */\n'
     '#undef HAVE_GETAUXVAL\n'
     '#define HAVE_GETAUXVAL 0\n'
     '#undef HAVE_ELF_AUX_INFO\n'
     '#define HAVE_ELF_AUX_INFO 0\n'
     '#endif\n'
     '\n'),
        # Haiku has pthread_attr_getstack() but not pthread_getattr_np(), so the
    # POSIX branch cannot be reused; get_thread_info() reports stack bounds.
("third_party/blink/renderer/platform/wtf/stack_util.cc",
     '#include <winnt.h>\n'
     '#elif defined(__GLIBC__)\n',
     '#include <winnt.h>\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '#include <OS.h>\n'
     '#elif defined(__GLIBC__)\n'),
    ("third_party/blink/renderer/platform/wtf/stack_util.cc",
     '  return Threading::ThreadStackSize();\n'
     '#else\n',
     '  return Threading::ThreadStackSize();\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  // Haiku has pthread_attr_getstack() but not pthread_getattr_np(), so the\n'
     '  // POSIX branch above cannot be reused. The kernel reports the live stack\n'
     '  // bounds of any thread directly instead.\n'
     '  thread_info info;\n'
     '  if (get_thread_info(find_thread(nullptr), &info) == B_OK) {\n'
     '    return static_cast<uint8_t*>(info.stack_end) -\n'
     '           static_cast<uint8_t*>(info.stack_base);\n'
     '  }\n'
     '  // Same conservative fallback as the POSIX branch above.\n'
     '  return 512 * 1024;\n'
     '#else\n'),
    ("third_party/blink/renderer/platform/wtf/stack_util.cc",
     '#endif\n'
     '#else\n',
     '#endif\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  // Stacks grow down on every architecture Haiku supports, so the "start"\n'
     "  // of the stack is its high address -- stack_end in Haiku's terms.\n"
     '  thread_info info;\n'
     '  CHECK_EQ(get_thread_info(find_thread(nullptr), &info), B_OK);\n'
     '  return info.stack_end;\n'
     '#else\n'),
        # FontCache::DeviceScaleFactor was declared only for LINUX/CHROMEOS, but
    # font_platform_data.cc's caller is gated on !ANDROID && !FUCHSIA && !IOS,
    # which Haiku satisfies -- so the declaration must cover Haiku too.
("third_party/blink/renderer/platform/fonts/font_cache.h",
     '\n'
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
     '  // These are needed for calling QueryRenderStyleForStrike, since\n',
     '\n'
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_HAIKU)\n'
     '  // These are needed for calling QueryRenderStyleForStrike, since\n'),
    ("third_party/blink/renderer/platform/fonts/font_cache.h",
     '  // gfx::GetFontRenderParams makes distinctions based on DSF.\n'
     '  static float DeviceScaleFactor() { return device_scale_factor_; }\n',
     '  // gfx::GetFontRenderParams makes distinctions based on DSF.\n'
     "  // Haiku is included because font_platform_data.cc's caller is gated on\n"
     '  // !ANDROID && !FUCHSIA && !IOS, which Haiku satisfies. The call itself is\n'
     '  // a runtime no-op there (Haiku has no WebSandboxSupport), but the\n'
     '  // declaration still has to exist for it to compile.\n'
     '  static float DeviceScaleFactor() { return device_scale_factor_; }\n'),
    ("third_party/blink/renderer/platform/fonts/font_cache.h",
     '\n'
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n'
     '  static float device_scale_factor_;\n',
     '\n'
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_HAIKU)\n'
     '  static float device_scale_factor_;\n'),
    ("third_party/blink/renderer/platform/fonts/font_cache.cc",
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)\n',
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_HAIKU)\n'),
        # Haiku is a desktop platform; the AGC #if chain had no arm for it and
    # fell through to #error.
("media/webrtc/helpers.cc",
     '#elif BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n',
     '#elif BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_HAIKU)\n'
     '  // false means the AGC2 input volume controller is enabled unconditionally,\n'
     '  // which is the end state the TODO below is working towards. Haiku has no\n'
     '  // legacy behaviour to preserve here, so it joins that bucket rather than\n'
     '  // the one still gated behind the transitional feature flag.\n'),
    ("media/webrtc/helpers.cc",
     '    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n'
     '  // Use AGC2 digital and input volume controller.\n',
     '    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_HAIKU)\n'
     '  // Use AGC2 digital and input volume controller. Haiku takes the desktop\n'
     '  // configuration: the mobile branch below exists to save power on phones,\n'
     '  // not to work around any capability Haiku lacks.\n'),
        # Haiku has no video capture backend; UNKNOWN is what iOS reports.
("media/capture/video/fake_video_capture_device_factory.cc",
     '#elif BUILDFLAG(IS_IOS)\n',
     '#elif BUILDFLAG(IS_IOS) || BUILDFLAG(IS_HAIKU)\n'
     '        // Haiku has no video capture backend, so there is no real API to\n'
     '        // name here; UNKNOWN is what iOS reports for the same reason.\n'),
        # filter_set is only read under ENABLE_VULKAN or SKIA_USE_DAWN+CHROMEOS,
    # both off for Haiku. Matches the [[maybe_unused]] factory above it.
("gpu/ipc/service/gpu_init.cc",
     '  bool filter_set = false;\n',
     '  [[maybe_unused]] bool filter_set = false;\n'),
        # Haiku joins Fuchsia: no DRM-style native pixmaps, so NativePixmapHandle
    # has neither planes nor a modifier and the #else path cannot compile.
("gpu/command_buffer/service/shared_image/ozone_image_backing.cc",
     '#if BUILDFLAG(IS_FUCHSIA)\n',
     '#if BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_HAIKU)\n'
     '  // Haiku joins Fuchsia here for the same reason: it has no DRM-style native\n'
     '  // pixmaps, so NativePixmapHandle carries neither planes nor a modifier and\n'
     '  // the #else path below cannot be expressed at all.\n'),
        # vulkan_context_provider.h is only included under ENABLE_VULKAN, so the
    # pointer must not be dereferenced when Vulkan is off.
("gpu/command_buffer/service/shared_image/ozone_image_backing_factory.cc",
     '          ->CreateNativePixmap(gpu::kNullSurfaceHandle,\n'
     '                               vulkan_context_provider\n',
     '          ->CreateNativePixmap(gpu::kNullSurfaceHandle,\n'
     '#if BUILDFLAG(ENABLE_VULKAN)\n'
     '                               vulkan_context_provider\n'),
    ("gpu/command_buffer/service/shared_image/ozone_image_backing_factory.cc",
     '                                   : nullptr,\n'
     '                               size, format, usage, size);\n',
     '                                   : nullptr,\n'
     '#else\n'
     '                               // vulkan_context_provider.h is only included\n'
     '                               // under ENABLE_VULKAN, so the type is\n'
     '                               // incomplete here and there is no device queue\n'
     '                               // to hand over anyway.\n'
     '                               nullptr,\n'
     '#endif\n'
     '                               size, format, usage, size);\n'),
        # gfx::NATIVE_PIXMAP is in the enum for every Ozone platform, so the
    # debug-string switch must handle it on Haiku or -Wswitch fires.
("gpu/command_buffer/service/shared_image/shared_image_factory.cc",
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA)\n',
     '#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FUCHSIA) || \\\n'
     '    BUILDFLAG(IS_HAIKU)\n'),

        # navigator.platform and the two user-agent platform strings report Haiku
    # by name rather than borrowing another system's token. Deliberate call:
    # truthful reporting over web-compat/anti-fingerprinting camouflage.
    # These three sites must stay consistent with each other.
("third_party/blink/renderer/core/execution_context/navigator_base.cc",
     '  return "iPhone";\n'
     '#else\n',
     '  return "iPhone";\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  // Haiku names itself rather than hiding in the Linux bucket, the same\n'
     '  // choice Fuchsia makes just above. This is a deliberate call to report\n'
     '  // truthfully: it costs some web compatibility with sites that gate on\n'
     "  // known platform strings, and it makes this port's users more\n"
     '  // identifiable, but it does not misrepresent the system. Keep this\n'
     '  // consistent with GetUserAgentPlatform() and GetUnifiedPlatform() in\n'
     '  // components/embedder_support/user_agent_utils.cc.\n'
     '  return "Haiku";\n'
     '#else\n'),
    ("components/embedder_support/user_agent_utils.cc",
     '             : "iPhone; ";\n'
     '#else\n',
     '             : "iPhone; ";\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  // Haiku is not an X11 system, so it contributes no windowing prefix here.\n'
     "  // BuildOSCpuInfoFromOSVersionAndCpuType()'s generic POSIX branch then\n"
     '  // fills in uname()\'s sysname and machine, giving "Haiku arm64".\n'
     '  return "";\n'
     '#else\n'),
    ("components/embedder_support/user_agent_utils.cc",
     '  return "iPhone; CPU iPhone OS 14_0 like Mac OS X";\n'
     '#else\n',
     '  return "iPhone; CPU iPhone OS 14_0 like Mac OS X";\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  // Haiku names itself here, as Fuchsia does above, rather than adopting\n'
     "  // another system's reduced token. This is the reduced (anti-fingerprinting)\n"
     '  // platform string, so it stays a fixed value carrying no version or CPU\n'
     '  // detail -- it just says Haiku instead of claiming to be Linux. Keep this\n'
     '  // consistent with GetUserAgentPlatform() above and with\n'
     "  // GetReducedNavigatorPlatform() in blink's navigator_base.cc.\n"
     '  return "Haiku";\n'
     '#else\n'),

        # FILE_EXE/FILE_MODULE had no arm for Haiku, so the case body was empty
    # and fell through to DIR_SRC_TEST_DATA_ROOT, which asks for DIR_EXE and
    # recurses straight back into FILE_EXE. Silent: -Wimplicit-fallthrough
    # does not fire on an empty case. This is what locates icudtl.dat via
    # DIR_ASSETS, so getting it wrong breaks startup, not just a test path.
("base/base_paths_posix.cc",
     '#include <stdlib.h>\n'
     '#endif\n',
     '#include <stdlib.h>\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '#include <OS.h>\n'
     '#include <image.h>\n'
     '#endif\n'),
    ("base/base_paths_posix.cc",
     '      return true;\n'
     '#endif\n',
     '      return true;\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '      // Haiku has no /proc/self/exe. The kernel marks exactly one loaded\n'
     '      // image per team as B_APP_IMAGE -- the executable -- and image_info\n'
     '      // carries its full path. FILE_MODULE is answered the same way as\n'
     '      // FILE_EXE, matching what the Linux branch above does.\n'
     '      //\n'
     '      // This must not be left unhandled: an empty case body here falls\n'
     '      // through to DIR_SRC_TEST_DATA_ROOT, which asks for DIR_EXE and so\n'
     '      // comes straight back to FILE_EXE. It also costs no warning, because\n'
     '      // -Wimplicit-fallthrough does not fire on an empty case.\n'
     '      {\n'
     '        image_info info;\n'
     '        int32 cookie = 0;\n'
     '        while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {\n'
     '          if (info.type == B_APP_IMAGE) {\n'
     '            *result = FilePath(info.name);\n'
     '            return true;\n'
     '          }\n'
     '        }\n'
     '      }\n'
     '      NOTREACHED() << "Unable to find the B_APP_IMAGE for this team.";\n'
     '#endif\n'),

    # Keep //chrome out of a content_shell build. gn loads the BUILD.gn of
    # every label a loaded file names, whether or not the target is reachable
    # from the root, and telemetry's build file names //chrome targets. That
    # one dep is what drags all of //chrome in: 121 of its BUILD.gn files
    # assert a platform list, and its own dep graph then has to resolve for a
    # browser this port does not build. The group is a wrapper around
    # telemetry's test scripts and has no part in content_shell.
    # font_list_fontconfig.cc is in the sources for Haiku -- only Windows,
    # macOS, Android, Fuchsia and iOS drop it -- but Haiku has no fontconfig,
    # and building Chromium's bundled copy for it needs a gettext that the
    # sysroot does not have. The Haiku file (port/files/content/common/) takes
    # its place, the way Fuchsia's does.
    ("content/common/BUILD.gn",
     '  if (is_mac || is_win || is_android || is_fuchsia || is_ios) {\n'
     '    sources -= [ "font_list_fontconfig.cc" ]\n'
     '  }\n',
     '  if (is_mac || is_win || is_android || is_fuchsia || is_ios ||\n'
     '      is_haiku) {\n'
     '    sources -= [ "font_list_fontconfig.cc" ]\n'
     '  }\n'
     '\n'
     '  if (is_haiku) {\n'
     '    sources += [ "font_list_haiku.cc" ]\n'
     '  }\n'),

    # Blink's heap picks the same "local-exec" TLS model V8 did, and Haiku
    # takes the same answer: "local-dynamic". initial-exec would set
    # DF_STATIC_TLS, which Haiku's runtime_loader refuses. With
    # local-dynamic the access is a __tls_get_addr call, so the out-of-line
    # getter is the right one -- which is what the second edit selects.
    ("third_party/blink/renderer/platform/heap/thread_local.h",
     '#elif BUILDFLAG(IS_ANDROID)\n'
     '#define BLINK_HEAP_THREAD_LOCAL_MODEL "local-dynamic"\n',
     '#elif BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_HAIKU)\n'
     '#define BLINK_HEAP_THREAD_LOCAL_MODEL "local-dynamic"\n'),
    ("third_party/blink/renderer/platform/heap/thread_local.h",
     '#if !BLINK_HEAP_HIDE_THREAD_LOCAL_IN_LIBRARY && !BUILDFLAG(IS_ANDROID) && \\\n'
     '    !BUILDFLAG(IS_APPLE)\n',
     '#if !BLINK_HEAP_HIDE_THREAD_LOCAL_IN_LIBRARY && !BUILDFLAG(IS_ANDROID) && \\\n'
     '    !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_HAIKU)\n'),

    # The per-OS pieces that Haiku brings its own file for. Each is named
    # in port/files/ and copied in by apply-haiku-port.sh.
    ("content/browser/BUILD.gn",
     '  if (is_linux || is_chromeos) {\n'
     '    sources -=\n'
     '        [ "file_system_access/file_path_watcher/file_path_watcher_stub.cc" ]\n',
     '  if (is_haiku) {\n'
     '    sources += [ "child_process_launcher_helper_haiku.cc" ]\n'
     '  }\n'
     '\n'
     '  if (is_linux || is_chromeos) {\n'
     '    sources -=\n'
     '        [ "file_system_access/file_path_watcher/file_path_watcher_stub.cc" ]\n'),
    ("ui/gfx/BUILD.gn",
     '  if (is_apple) {\n'
     '    sources += [ "font_render_params_mac.cc" ]\n',
     '  if (is_haiku) {\n'
     '    sources += [\n'
     '      "animation/animation_haiku.cc",\n'
     '      "font_fallback_skia.cc",\n'
     '      "font_fallback_skia_impl.cc",\n'
     '      "font_fallback_skia_impl.h",\n'
     '      "font_render_params_haiku.cc",\n'
     '    ]\n'
     '  }\n'
     '\n'
     '  if (is_apple) {\n'
     '    sources += [ "font_render_params_mac.cc" ]\n'),
    ("media/audio/BUILD.gn",
     '  if (is_linux || is_chromeos) {\n'
     '    sources += [ "linux/audio_manager_linux.cc" ]\n'
     '  }\n',
     '  if (is_linux || is_chromeos) {\n'
     '    sources += [ "linux/audio_manager_linux.cc" ]\n'
     '  }\n'
     '\n'
     '  if (is_haiku) {\n'
     '    sources += [ "haiku/audio_manager_haiku.cc" ]\n'
     '  }\n'),

    # libc++abi defines __cxa_thread_atexit only for Linux and Fuchsia, and
    # Haiku's libroot does not have it either (nor the _impl form), so every
    # thread_local with a destructor -- V8's histograms, LLVM's ThreadLocal --
    # is left undefined at link time. The fallback implementation in this very
    # file is what Haiku needs; it is only the guard that excludes it.
    ("third_party/libc++abi/src/src/cxa_thread_atexit.cpp",
     "#if defined(__linux__) || defined(__Fuchsia__)\n",
     "#if defined(__linux__) || defined(__Fuchsia__) || defined(__HAIKU__)\n"),

    # ANGLE's POSIX system utilities. system_utils_linux.cpp is the /proc and
    # dl_iterate_phdr half, which Haiku has no use for; system_utils_posix.cpp
    # holds GetEnvironmentVar, GetTempDirectory and the rest.
    ("third_party/angle/src/libGLESv2.gni",
     'if (is_linux || is_chromeos || is_android || is_fuchsia) {\n'
     '  libangle_common_sources += [\n'
     '    "src/common/system_utils_linux.cpp",\n'
     '    "src/common/system_utils_posix.cpp",\n'
     '  ]\n'
     '}\n',
     'if (is_linux || is_chromeos || is_android || is_fuchsia) {\n'
     '  libangle_common_sources += [\n'
     '    "src/common/system_utils_linux.cpp",\n'
     '    "src/common/system_utils_posix.cpp",\n'
     '  ]\n'
     '}\n'
     '\n'
     'if (is_haiku) {\n'
     '  libangle_common_sources += [\n'
     '    "src/common/system_utils_haiku.cpp",\n'
     '    "src/common/system_utils_posix.cpp",\n'
     '  ]\n'
     '}\n'),

    # ANGLE's WebGPU backend stubs out CreateWgpuWindowSurface when there is
    # no window system to create one for, but tests that with a Linux-only
    # condition. Haiku has no wgpu window surface either, so it takes the stub
    # rather than leaving the symbol undefined.
    ("third_party/angle/src/libANGLE/renderer/wgpu/DisplayWgpu.cpp",
     "#if defined(ANGLE_PLATFORM_LINUX) && !defined(ANGLE_USE_X11) && !defined(ANGLE_USE_WAYLAND)\n",
     "#if (defined(ANGLE_PLATFORM_LINUX) && !defined(ANGLE_USE_X11) && \\\n"
     "     !defined(ANGLE_USE_WAYLAND)) ||                              \\\n"
     "    defined(__HAIKU__)\n"),

    # cpuinfo: see port/files/third_party/cpuinfo/haiku_isa.c.
    ("third_party/cpuinfo/BUILD.gn",
     '  if ((is_linux || is_chromeos) && current_cpu == "arm64") {\n',
     '  if (is_haiku && (current_cpu == "arm64" || current_cpu == "arm")) {\n'
     '    sources = [ "haiku_isa.c" ]\n'
     '  }\n'
     '\n'
     '  if ((is_linux || is_chromeos) && current_cpu == "arm64") {\n'),

    # The GPU channel host the video capture code names unconditionally. Its
    # own flag's platform list has no Haiku (there is no GPU process here),
    # but media/capture references media::VideoCaptureGpuChannelHost from
    # mappable_shared_image_utils.cc regardless, so the file is built.
    ("media/capture/BUILD.gn",
     '  # Establish GPU Channel\n'
     '  if (enable_gpu_channel_media_capture) {\n',
     '  # Establish GPU Channel\n'
     '  if (enable_gpu_channel_media_capture || is_haiku) {\n'),

    # Text-to-speech, the system animation preferences, the memory-dump
    # service's per-process figures: each is a per-OS file, and each of
    # Haiku's is in port/files/.
    ("content/browser/BUILD.gn",
     '  if (is_linux) {\n'
     '    sources += [ "speech/tts_linux.cc" ]\n',
     '  if (is_haiku) {\n'
     '    sources += [\n'
     '      "scheduler/responsiveness/native_event_observer_haiku.cc",\n'
     '      "speech/tts_haiku.cc",\n'
     '    ]\n'
     '  }\n'
     '\n'
     '  if (is_linux) {\n'
     '    sources += [ "speech/tts_linux.cc" ]\n'),
    ("services/resource_coordinator/public/cpp/memory_instrumentation/BUILD.gn",
     '    sources += [ "os_metrics_fuchsia.cc" ]\n',
     '    sources += [ "os_metrics_fuchsia.cc" ]\n'
     '  }\n'
     '\n'
     '  if (is_haiku) {\n'
     '    sources += [ "os_metrics_haiku.cc" ]\n'),

    # Extension service workers' USB bridge: the file is platform-neutral, it
    # is only the list of platforms that names desktops one by one.
    ("content/browser/BUILD.gn",
     '  if (is_win || is_apple || is_linux || is_chromeos || is_desktop_android ||\n'
     '      is_fuchsia) {\n'
     '    sources += [\n'
     '      "service_worker/service_worker_usb_delegate_observer.cc",\n',
     '  if (is_win || is_apple || is_linux || is_chromeos || is_desktop_android ||\n'
     '      is_fuchsia || is_haiku) {\n'
     '    sources += [\n'
     '      "service_worker/service_worker_usb_delegate_observer.cc",\n'),

    # The system trust store. The chain of platform branches ends at
    # Android with no #else, so a platform outside it has no
    # CreateSslSystemTrustStoreChromeRoot at all. Haiku has no system
    # certificate store to consult -- which is why the port already sets
    # chrome_root_store_only -- so the Chrome root store alone is the whole
    # answer, and there is a constructor for exactly that.
    ("net/cert/internal/system_trust_store.cc",
     "#elif BUILDFLAG(IS_ANDROID)\n"
     "\n"
     "#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)\n",
     "#elif BUILDFLAG(IS_HAIKU)\n"
     "\n"
     "std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStoreChromeRoot(\n"
     "    std::unique_ptr<TrustStoreChrome> chrome_root) {\n"
     "  return CreateChromeOnlySystemTrustStore(std::move(chrome_root));\n"
     "}\n"
     "\n"
     "#elif BUILDFLAG(IS_ANDROID)\n"
     "\n"
     "#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)\n"),

    # The last of the per-OS files: a time-zone monitor that never fires, a
    # responsiveness observer with no native-event source to hook, and the
    # aura resource bundle (GetNativeImageNamed), which is not Linux-specific
    # despite its name -- it just turns a resource into a gfx::Image.
    ("services/device/time_zone_monitor/BUILD.gn",
     '  if (is_fuchsia) {\n'
     '    sources += [ "time_zone_monitor_fuchsia.cc" ]\n'
     '  }\n',
     '  if (is_fuchsia) {\n'
     '    sources += [ "time_zone_monitor_fuchsia.cc" ]\n'
     '  }\n'
     '\n'
     '  if (is_haiku) {\n'
     '    sources += [ "time_zone_monitor_haiku.cc" ]\n'
     '  }\n'),
    ("ui/base/BUILD.gn",
     '  if (use_aura && (is_linux || is_chromeos)) {\n'
     '    sources += [ "resource/resource_bundle_auralinux.cc" ]\n'
     '  }\n',
     '  if (use_aura && (is_linux || is_chromeos || is_haiku)) {\n'
     '    sources += [ "resource/resource_bundle_auralinux.cc" ]\n'
     '  }\n'),

    # Skia's directory font manager. HaikuFontMgr (skia/ext/font_utils.cc)
    # composes one per font location, and Chromium's skia target compiles the
    # "custom" font manager sources but not the directory variant, which only
    # this port asks for.
    ("skia/BUILD.gn",
     '      sources += skia_ports_fontmgr_custom_sources\n',
     '      sources += skia_ports_fontmgr_custom_sources\n'
     '      if (is_haiku) {\n'
     '        sources += [ "//third_party/skia/src/ports/SkFontMgr_custom_directory.cpp" ]\n'
     '      }\n'),

    # GPU info, platform MIME types: one Haiku file each, in port/files/.
    ("gpu/config/BUILD.gn",
     '  if (is_fuchsia) {\n'
     '    sources += [ "gpu_info_collector_fuchsia.cc" ]\n'
     '  }\n',
     '  if (is_fuchsia) {\n'
     '    sources += [ "gpu_info_collector_fuchsia.cc" ]\n'
     '  }\n'
     '\n'
     '  if (is_haiku) {\n'
     '    sources += [ "gpu_info_collector_haiku.cc" ]\n'
     '  }\n'),
    ("net/BUILD.gn",
     '      "base/platform_mime_util_linux.cc",\n'
     '    ]\n'
     '  }\n',
     '      "base/platform_mime_util_linux.cc",\n'
     '    ]\n'
     '  }\n'
     '\n'
     '  if (is_haiku) {\n'
     '    sources += [\n'
     '      "base/platform_mime_util_haiku.cc",\n'
     '      # No system certificate store to add a test root to.\n'
     '      "cert/test_root_certs_builtin.cc",\n'
     '    ]\n'
     '  }\n'),

    # BoringSSL's ARM feature detection. Every cpu_aarch64_*.cc is for an OS
    # it knows, so a build for any other is left without
    # OPENSSL_cpuid_setup(). OPENSSL_STATIC_ARMCAP is the switch for exactly
    # that case: no runtime detection, baseline features only. Chromium
    # already builds BoringSSL with OPENSSL_NO_ASM here, so the accelerated
    # paths the detection would unlock are not compiled in anyway.

    # Stack traces. backtrace() lives in glibc and in Apple's libc; Haiku
    # keeps no equivalent in its base libraries (libexecinfo is a HaikuPorts
    # package), so HAVE_BACKTRACE stays unset and every crash printed
    # "[end of stack trace]" with nothing above it.
    #
    # The unwinder libc++abi already links is enough: _Unwind_Backtrace walks
    # the .eh_frame this build emits, with no extra package and no frame
    # pointers. See base/debug/unwind_backtrace_haiku.h, which also prints the
    # image map -- a Haiku executable is ET_DYN, so a bare runtime address
    # cannot be handed to addr2line without it.
    ("base/debug/stack_trace_posix.cc",
     "// Controls whether to include code to demangle C++ symbols.\n",
     "#if defined(__HAIKU__)\n"
     "#include \"base/debug/unwind_backtrace_haiku.h\"\n"
     "#endif\n"
     "\n"
     "// Controls whether to include code to demangle C++ symbols.\n"),

    ("base/debug/stack_trace_posix.cc",
     "                base::saturated_cast<int>(trace.size())));\n"
     "#else\n"
     "  return 0;\n"
     "#endif\n",
     "                base::saturated_cast<int>(trace.size())));\n"
     "#elif defined(__HAIKU__)\n"
     "  return base::debug::haiku::CollectBacktrace(trace);\n"
     "#else\n"
     "  return 0;\n"
     "#endif\n"),

    # The faulting instruction. _Unwind_Backtrace cannot walk through a Haiku
    # signal frame, so the trace below the handler is empty and a crash says
    # nothing about where it happened. elr (the faulting PC) and lr from the
    # context are the whole answer in that case -- this is what located a null
    # dereference in the media capture factory that the backtrace could not.
    # The x64 branch is written here rather than by port-x64.py: when that
    # script rewrote this block, this edit no longer found its own text on an
    # x64 tree and added the arm64 block a second time.
    ("base/debug/stack_trace_posix.cc",
     "  debug::StackTrace().Print();\n",
     "#if defined(__HAIKU__)\n"
     "  {\n"
     "    // mcontext_t is struct vregs (arch/arm64/signal.h); <signal.h>,\n"
     "    // included above, declares it -- Haiku has no <ucontext.h>.\n"
     "    const ucontext_t* uc = static_cast<const ucontext_t*>(void_context);\n"
     "    char reg_buf[32];\n"
     "#if defined(__x86_64__)\n"
     "    PrintToStderr(\"[haiku] rip=\");\n"
     "    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.rip), 16, 16,\n"
     "                     reg_buf);\n"
     "    PrintToStderr(reg_buf);\n"
     "    PrintToStderr(\" rsp=\");\n"
     "    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.rsp), 16, 16,\n"
     "                     reg_buf);\n"
     "#else\n"
     "    PrintToStderr(\"[haiku] elr=\");\n"
     "    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.elr), 16, 16,\n"
     "                     reg_buf);\n"
     "    PrintToStderr(reg_buf);\n"
     "    PrintToStderr(\" lr=\");\n"
     "    internal::itoa_r(static_cast<intptr_t>(uc->uc_mcontext.lr), 16, 16,\n"
     "                     reg_buf);\n"
     "#endif\n"
     "    PrintToStderr(reg_buf);\n"
     "    PrintToStderr(\"\\n\");\n"
     "  }\n"
     "#endif\n"
     "  debug::StackTrace().Print();\n"),

    # The signal handler's path. It has to stay async-signal safe, which is
    # why the helper writes with write() rather than through a std::ostream.
    ("base/debug/stack_trace_posix.cc",
     "#if defined(HAVE_BACKTRACE)\n"
     "  PrintBacktraceOutputHandler handler;\n"
     "  ProcessBacktrace(addresses(), prefix_string, &handler);\n"
     "#endif\n",
     "#if defined(HAVE_BACKTRACE)\n"
     "  PrintBacktraceOutputHandler handler;\n"
     "  ProcessBacktrace(addresses(), prefix_string, &handler);\n"
     "#elif defined(__HAIKU__)\n"
     "  base::debug::haiku::PrintBacktrace(addresses(), prefix_string);\n"
     "#endif\n"),

    ("base/debug/stack_trace_posix.cc",
     "#if defined(HAVE_BACKTRACE)\n"
     "void StackTrace::OutputToStreamWithPrefixImpl(\n",
     "#if !defined(HAVE_BACKTRACE) && defined(__HAIKU__)\n"
     "void StackTrace::OutputToStreamWithPrefixImpl(\n"
     "    std::ostream* os,\n"
     "    cstring_view prefix_string) const {\n"
     "  base::debug::haiku::PrintImageMapToStream(os, prefix_string);\n"
     "  for (const void* address : addresses()) {\n"
     "    *os << prefix_string << address << \"\\n\";\n"
     "  }\n"
     "}\n"
     "#endif\n"
     "\n"
     "#if defined(HAVE_BACKTRACE)\n"
     "void StackTrace::OutputToStreamWithPrefixImpl(\n"),

    # Video capture. Haiku has no backend, so the factory came out null --
    # and VideoCaptureSystemImpl::GetDeviceInfosAsync dereferences it without
    # checking, so a page that enumerates cameras (x.com does, on load)
    # crashed the renderer with a null dereference four seconds in. Report an
    # empty device list instead: the fake factory with an empty config is
    # what says "this machine has no cameras" without inventing one.
    ("media/capture/video/create_video_capture_device_factory.cc",
     "#elif BUILDFLAG(IS_IOS)\n"
     "  return CreateFakeVideoCaptureDeviceFactory();\n"
     "#else\n"
     "  NOTIMPLEMENTED();\n"
     "  return nullptr;\n"
     "#endif\n",
     "#elif BUILDFLAG(IS_IOS)\n"
     "  return CreateFakeVideoCaptureDeviceFactory();\n"
     "#elif BUILDFLAG(IS_HAIKU)\n"
     "  {\n"
     "    auto factory = std::make_unique<FakeVideoCaptureDeviceFactory>();\n"
     "    factory->SetToCustomDevicesConfig({});\n"
     "    return factory;\n"
     "  }\n"
     "#else\n"
     "  NOTIMPLEMENTED();\n"
     "  return nullptr;\n"
     "#endif\n"),

    # base/trace_event's system-allocator reporter. It is compiled only when
    # PartitionAlloc is not the malloc -- which is how the allocator is
    # swapped out for a build -- and it reads mallinfo(), which Haiku's
    # libroot does not have. Leave the totals at zero; nothing but the
    # memory-infra trace reads them.
    ("base/trace_event/malloc_dump_provider.cc",
     "    (!PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC) && !BUILDFLAG(IS_WIN) &&    \\\n"
     "     !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_FUCHSIA))\n",
     "    (!PA_BUILDFLAG(USE_PARTITION_ALLOC_AS_MALLOC) && !BUILDFLAG(IS_WIN) &&    \\\n"
     "     !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_FUCHSIA) && !BUILDFLAG(IS_HAIKU))\n"),

    ("base/trace_event/malloc_dump_provider.cc",
     "#elif BUILDFLAG(IS_FUCHSIA)\n"
     "// TODO(fuchsia): Port, see https://crbug.com/706592.\n"
     "#else\n"
     "  ReportMallinfoStats(/*pmd=*/nullptr, &total_virtual_size, &resident_size,\n"
     "                      &allocated_objects_size, &allocated_objects_count);\n"
     "#endif\n",
     "#elif BUILDFLAG(IS_FUCHSIA)\n"
     "// TODO(fuchsia): Port, see https://crbug.com/706592.\n"
     "#elif BUILDFLAG(IS_HAIKU)\n"
     "// Haiku has no mallinfo(); see the guard on ReportMallinfoStats above.\n"
     "#else\n"
     "  ReportMallinfoStats(/*pmd=*/nullptr, &total_virtual_size, &resident_size,\n"
     "                      &allocated_objects_size, &allocated_objects_count);\n"
     "#endif\n"),

    # The last per-OS files, each in port/files/: Blink's per-character font
    # fallback (which goes through HaikuFontMgr), the default form-control
    # theme, the renderer's sandbox hook (there is no sandbox), the GPU's
    # native surface (there is no GL), ANGLE's thread naming, PartitionAlloc's
    # stack collector, and the test root store.
    ("third_party/blink/renderer/platform/BUILD.gn",
     '  if (is_fuchsia) {\n'
     '    sources += [ "fonts/fuchsia/font_cache_fuchsia.cc" ]\n'
     '  }\n',
     '  if (is_fuchsia) {\n'
     '    sources += [ "fonts/fuchsia/font_cache_fuchsia.cc" ]\n'
     '  }\n'
     '\n'
     '  if (is_haiku) {\n'
     '    sources += [ "fonts/haiku/font_cache_haiku.cc" ]\n'
     '  }\n'),
    ("third_party/blink/renderer/core/layout/build.gni",
     '  blink_core_sources_layout += [ "layout_theme_fuchsia.cc" ]\n',
     '  blink_core_sources_layout += [ "layout_theme_fuchsia.cc" ]\n'
     '}\n'
     '\n'
     'if (is_haiku) {\n'
     '  blink_core_sources_layout += [ "layout_theme_haiku.cc" ]\n'),
    ("content/renderer/BUILD.gn",
     '  if (is_linux || is_chromeos) {\n'
     '    sources += [ "renderer_main_platform_delegate_linux.cc" ]\n',
     '  if (is_haiku) {\n'
     '    sources += [ "renderer_main_platform_delegate_haiku.cc" ]\n'
     '  }\n'
     '\n'
     '  if (is_linux || is_chromeos) {\n'
     '    sources += [ "renderer_main_platform_delegate_linux.cc" ]\n'),
    ("gpu/ipc/service/BUILD.gn",
     '  if (is_fuchsia) {\n'
     '    sources += [ "image_transport_surface_fuchsia.cc" ]\n',
     '  if (is_haiku) {\n'
     '    sources += [ "image_transport_surface_haiku.cc" ]\n'
     '  }\n'
     '  if (is_fuchsia) {\n'
     '    sources += [ "image_transport_surface_fuchsia.cc" ]\n'),
    ("base/allocator/partition_allocator/src/partition_alloc/BUILD.gn",
     '      if (is_linux || is_chromeos) {\n'
     '        sources += [ "partition_alloc_base/debug/stack_trace_linux.cc" ]\n',
     '      if (is_haiku) {\n'
     '        sources += [ "partition_alloc_base/debug/stack_trace_haiku.cc" ]\n'
     '      }\n'
     '\n'
     '      if (is_linux || is_chromeos) {\n'
     '        sources += [ "partition_alloc_base/debug/stack_trace_linux.cc" ]\n'),

    # Idle detection, the file picker and drag-and-drop's backing store:
    # Haiku takes the same files Fuchsia does -- idle_fuchsia.cc and
    # select_file_dialog_fuchsia.cc are "not implemented" stubs with nothing
    # Fuchsia-specific in them, and os_exchange_data_provider_non_backed is
    # the plain in-process clipboard/drag provider that every platform without
    # a native one uses. content_shell has no file picker and no idle API.
    ("ui/base/idle/BUILD.gn",
     '  if (is_fuchsia) {\n'
     '    sources += [ "idle_fuchsia.cc" ]\n'
     '  }\n',
     '  if (is_fuchsia || is_haiku) {\n'
     '    sources += [ "idle_fuchsia.cc" ]\n'
     '  }\n'),
    ("ui/shell_dialogs/BUILD.gn",
     '  if (is_fuchsia) {\n'
     '    sources += [ "select_file_dialog_fuchsia.cc" ]\n'
     '  }\n',
     '  if (is_fuchsia || is_haiku) {\n'
     '    sources += [ "select_file_dialog_fuchsia.cc" ]\n'
     '  }\n'),
    ("ui/base/BUILD.gn",
     '  if (is_chromeos || (use_aura && is_linux) || is_fuchsia) {\n'
     '    sources += [\n'
     '      "dragdrop/os_exchange_data_provider_non_backed.cc",\n',
     '  if (is_chromeos || (use_aura && is_linux) || is_fuchsia || is_haiku) {\n'
     '    sources += [\n'
     '      "dragdrop/os_exchange_data_provider_non_backed.cc",\n'),

    # Blink asks Skia for a typeface by fontconfig id on every platform that
    # is not Apple, Android, Windows or Fuchsia -- which on Haiku means
    # SkFontConfigInterface, a class Chromium only builds with fontconfig.
    # Haiku has no fontconfig and no such ids: nothing calls this path here
    # (HaikuFontMgr answers by family and character), so it joins the list of
    # platforms that do not compile it.
    ("third_party/blink/renderer/platform/fonts/skia/sktypeface_factory.cc",
     "#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_WIN) && \\\n"
     "    !BUILDFLAG(IS_FUCHSIA)\n"
     "  sk_sp<SkFontConfigInterface> fci(SkFontConfigInterface::RefGlobal());\n",
     "#if !BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_WIN) && \\\n"
     "    !BUILDFLAG(IS_FUCHSIA) && !BUILDFLAG(IS_HAIKU)\n"
     "  sk_sp<SkFontConfigInterface> fci(SkFontConfigInterface::RefGlobal());\n"),

    # BoringSSL: the define has to be on the config every target gets, not on
    # the no-asm one, which only an MSan build uses.
    ("third_party/boringssl/BUILD.gn",
     '  defines = [ "OPENSSL_SMALL" ]\n',
     '  defines = [ "OPENSSL_SMALL" ]\n'
     '  if (is_haiku) {\n'
     '    # No cpu_aarch64_*.cc knows Haiku, so there is no\n'
     '    # OPENSSL_cpuid_setup() to call: take the baseline features.\n'
     '    defines += [ "OPENSSL_STATIC_ARMCAP" ]\n'
     '  }\n'),

    # ffmpeg links librt for clock_gettime. Haiku keeps clock_gettime in
    # libroot and has no librt at all, so the link stops with "unable to find
    # library -lrt".
    ("third_party/ffmpeg/BUILD.gn",
     '      # librt for clock_gettime on precise\n'
     '      libs += [\n'
     '        "m",\n'
     '        "z",\n'
     '        "rt",\n'
     '      ]\n',
     '      # librt for clock_gettime on precise. Haiku has clock_gettime in\n'
     '      # libroot and ships no librt.\n'
     '      libs += [\n'
     '        "m",\n'
     '        "z",\n'
     '      ]\n'
     '      if (!is_haiku) {\n'
     '        libs += [ "rt" ]\n'
     '      }\n'),

    # //content/browser names scoped_refptr<gpu::GpuChannelHost> in a function
    # signature regardless of ENABLE_GPU_CHANNEL_MEDIA_CAPTURE, while the
    # header that declares the type is included only when the flag is on. On
    # every platform in that flag's list the mismatch is invisible; Haiku is
    # not in it (there is no GPU process here), so the forward declaration has
    # to come from somewhere.
    # WebAuthn off, because there is nothing to authenticate with and the
    # API does not say so.
    #
    # Haiku has no platform authenticator and HidService here is a stub that
    # enumerates nothing, so there is no way for a credential request to
    # succeed. What Blink reports, though, is
    # isConditionalMediationAvailable() == true (while
    # isUserVerifyingPlatformAuthenticatorAvailable() == false), and
    # navigator.credentials.get() and create() then neither resolve nor
    # reject -- they hang. A site that reads the first answer offers a passkey
    # and leaves the user on a spinner with no way back to its password form;
    # x.com does exactly that, which is how this was found.
    #
    # Removing the feature removes window.PublicKeyCredential, which is the
    # thing sites feature-detect, so they fall back to passwords. This runs
    # before the --enable-blink-features handling below it in the same
    # function, so --enable-blink-features=WebAuth still puts it back for
    # anyone who wants to test the hang. Undo it when there is a real
    # authenticator.
    ("content/child/runtime_features.cc",
     "  // These checks are custom wrappers around base::FeatureList::IsEnabled\n"
     "  // They\'re moved here to distinguish them from actual base checks\n",
     "#if BUILDFLAG(IS_HAIKU)\n"
     "  // No authenticator exists on this platform, and the API claims one\n"
     "  // does: sites offer a passkey and then wait for a promise that never\n"
     "  // settles. Taking the feature away removes window.PublicKeyCredential,\n"
     "  // which is what they feature-detect.\n"
     "  WebRuntimeFeatures::EnableFeatureFromString(\"WebAuth\", false);\n"
     "#endif\n"
     "\n"
     "  // These checks are custom wrappers around base::FeatureList::IsEnabled\n"
     "  // They\'re moved here to distinguish them from actual base checks\n"),

    ("content/browser/video_capture_service_impl.cc",
     '#if BUILDFLAG(IS_WIN)\n'
     '#define CREATE_IN_PROCESS_TASK_RUNNER base::ThreadPool::CreateCOMSTATaskRunner\n',
     '#if !BUILDFLAG(ENABLE_GPU_CHANNEL_MEDIA_CAPTURE)\n'
     '// BindInProcessInstance() takes a gpu::GpuChannelHost whether or not it\n'
     '// has anything to do with it.\n'
     '#include "gpu/ipc/client/gpu_channel_host.h"\n'
     '#endif\n'
     '\n'
     '#if BUILDFLAG(IS_WIN)\n'
     '#define CREATE_IN_PROCESS_TASK_RUNNER base::ThreadPool::CreateCOMSTATaskRunner\n'),

    # The zygote. `is_posix && !is_android && !is_apple` makes Haiku a zygote
    # platform, and RunZygote() then calls ContentMainDelegate::ZygoteStarting,
    # which only exists for Linux and ChromeOS. Haiku has none of what the
    # zygote is for either -- no fork sandbox, no /proc -- and the launcher
    # runs single-process anyway.
    ("content/public/common/zygote/features.gni",
     "use_zygote = is_posix && !is_android && !is_apple\n",
     "use_zygote = is_posix && !is_android && !is_apple && !is_haiku\n"),

    # Screen capture. The mojo interfaces declare FocusCapturedSurface and
    # the MediaDevices methods for every platform, while //content implements
    # them behind ENABLE_SCREEN_CAPTURE, so a platform outside that list
    # leaves MediaDevicesDispatcherHost abstract. Haiku is a desktop, so it
    # joins the list rather than having every call site guarded.
    ("content/public/common/features.gni",
     "enable_screen_capture = is_linux || is_chromeos || (is_apple && use_blink) ||\n"
     "                        is_win || is_android || is_fuchsia\n",
     "enable_screen_capture = is_linux || is_chromeos || (is_apple && use_blink) ||\n"
     "                        is_win || is_android || is_fuchsia || is_haiku\n"),

    # In-product help (feature_engagement) declares its desktop feature
    # constants behind a platform list, and //components/autofill names them
    # without one, so every suggestion generator fails to compile on a
    # platform the list omits. Haiku joins the desktop set in both the header
    # and the definitions; the features themselves do nothing here, because
    # content_shell has no IPH UI.
    ("components/feature_engagement/public/feature_constants.h",
     "#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \\\n"
     "    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)\n",
     "#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \\\n"
     "    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) ||               \\\n"
     "    BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_HAIKU)\n"),
    ("components/feature_engagement/public/feature_constants.cc",
     "#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \\\n"
     "    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)\n",
     "#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || \\\n"
     "    BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) ||               \\\n"
     "    BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_HAIKU)\n"),

    # Safe Browsing's download file-type list is generated per platform, and
    # an unknown one is spelled "unknown_target_arch", which the generator
    # then refuses. The Linux list is the right one for Haiku: the same
    # executable, archive and script types matter.
    ("components/safe_browsing/content/resources/BUILD.gn",
     '  } else if (is_linux) {\n'
     '    target_arch = "linux"\n',
     '  } else if (is_linux || is_haiku) {\n'
     '    target_arch = "linux"\n'),

    # Six #error walls of the same kind: a platform switch with no arm for
    # Haiku. Each takes the Linux answer, which is what Haiku behaves like in
    # every one of these (a desktop, syncable, non-mobile OS).
    ("components/content_settings/core/browser/website_settings_registry.cc",
     '#elif BUILDFLAG(IS_FUCHSIA)\n'
     '  if (!(platform & PLATFORM_FUCHSIA))\n'
     '    return nullptr;\n'
     '#else\n'
     '#error "Unsupported platform"\n',
     '#elif BUILDFLAG(IS_FUCHSIA)\n'
     '  if (!(platform & PLATFORM_FUCHSIA))\n'
     '    return nullptr;\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  if (!(platform & PLATFORM_LINUX))\n'
     '    return nullptr;\n'
     '#else\n'
     '#error "Unsupported platform"\n'),
    ("components/policy/core/common/cloud/cloud_policy_util.cc",
     '#elif BUILDFLAG(IS_CHROMEOS)\n'
     '  NOTREACHED();\n'
     '#else\n'
     '#error Unsupported platform\n',
     '#elif BUILDFLAG(IS_CHROMEOS)\n'
     '  NOTREACHED();\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  // Same as the POSIX path above: the machine name is what Haiku has.\n'
     '  char hostname[HOST_NAME_MAX + 1] = {};\n'
     '  if (gethostname(hostname, sizeof(hostname) - 1) == 0) {\n'
     '    return std::string(hostname);\n'
     '  }\n'
     '  return std::string();\n'
     '#else\n'
     '#error Unsupported platform\n'),
    ("components/sync_device_info/local_device_info_util.cc",
     '#elif BUILDFLAG(IS_FUCHSIA)\n'
     '  return DeviceInfo::OsType::kFuchsia;\n'
     '#else\n'
     '#error Please handle your new device OS here.\n',
     '#elif BUILDFLAG(IS_FUCHSIA)\n'
     '  return DeviceInfo::OsType::kFuchsia;\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  return DeviceInfo::OsType::kLinux;\n'
     '#else\n'
     '#error Please handle your new device OS here.\n'),
    ("components/trusted_vault/trusted_vault_connection_impl.cc",
     '#elif BUILDFLAG(IS_FUCHSIA)\n'
     '  // Not used in Fuchsia.\n'
     '  return trusted_vault_pb::PhysicalDeviceMetadata::DEVICE_TYPE_UNKNOWN;\n'
     '#else\n'
     '#error Please handle your new device OS here.\n',
     '#elif BUILDFLAG(IS_FUCHSIA)\n'
     '  // Not used in Fuchsia.\n'
     '  return trusted_vault_pb::PhysicalDeviceMetadata::DEVICE_TYPE_UNKNOWN;\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  return trusted_vault_pb::PhysicalDeviceMetadata::DEVICE_TYPE_LINUX;\n'
     '#else\n'
     '#error Please handle your new device OS here.\n'),
    ("components/variations/service/variations_service.cc",
     '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_BSD) || BUILDFLAG(IS_SOLARIS)\n',
     '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_BSD) || BUILDFLAG(IS_SOLARIS) || \\\n'
     '    BUILDFLAG(IS_HAIKU)\n'),
    ("components/webui/flags/flags_state.cc",
     '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_OPENBSD)\n'
     '  return kOsLinux;\n',
     '#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_OPENBSD) || BUILDFLAG(IS_HAIKU)\n'
     '  return kOsLinux;\n'),

    # The component updater's OS string. Nothing here updates components, but
    # the file is compiled and #errors on an OS it does not know.
    ("components/update_client/update_query_params.cc",
     '#elif BUILDFLAG(IS_OPENBSD)\n'
     '    "openbsd";\n'
     '#else\n'
     '#error "unknown os"\n',
     '#elif BUILDFLAG(IS_OPENBSD)\n'
     '    "openbsd";\n'
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '    "haiku";\n'
     '#else\n'
     '#error "unknown os"\n'),

    # crashpad has no Haiku port -- its util does not even define the address
    # types for this OS -- and content_shell's crash reporting is optional
    # (--enable-crash-reporter) with the crash keys already stubbed out here
    # (use_crash_key_stubs is true for Haiku). So the whole crashpad side is
    # left out, the way Fuchsia leaves it out.
    ("content/shell/BUILD.gn",
     '  sources = [\n'
     '    "app/shell_crash_reporter_client.cc",\n'
     '    "app/shell_crash_reporter_client.h",\n'
     '    "app/shell_main_delegate.cc",\n'
     '    "app/shell_main_delegate.h",\n'
     '  ]\n',
     '  sources = [\n'
     '    "app/shell_main_delegate.cc",\n'
     '    "app/shell_main_delegate.h",\n'
     '  ]\n'
     '  if (!is_haiku) {\n'
     '    sources += [\n'
     '      "app/shell_crash_reporter_client.cc",\n'
     '      "app/shell_crash_reporter_client.h",\n'
     '    ]\n'
     '  }\n'),
    ("content/shell/BUILD.gn",
     '  if (!is_fuchsia) {\n'
     '    deps += [\n'
     '      "//components/crash/core/app",\n'
     '      "//components/crash/core/app:test_support",\n'
     '    ]\n'
     '  }\n',
     '  if (!is_fuchsia && !is_haiku) {\n'
     '    deps += [\n'
     '      "//components/crash/core/app",\n'
     '      "//components/crash/core/app:test_support",\n'
     '    ]\n'
     '  }\n'),
    # content_shell_lib reaches the same place through its own else-branch.
    ("content/shell/BUILD.gn",
     '    deps += [ "//third_party/fuchsia-sdk/sdk/fidl/fuchsia.element:fuchsia.element_hlcpp" ]\n'
     '  } else {\n'
     '    deps += [\n'
     '      "//components/crash/content/browser",\n'
     '      "//components/crash/core/app",\n'
     '    ]\n'
     '  }\n',
     '    deps += [ "//third_party/fuchsia-sdk/sdk/fidl/fuchsia.element:fuchsia.element_hlcpp" ]\n'
     '  } else if (!is_haiku) {\n'
     '    deps += [\n'
     '      "//components/crash/content/browser",\n'
     '      "//components/crash/core/app",\n'
     '    ]\n'
     '  }\n'),

    # Enterprise policy. The policy templates know no "haiku" platform, and a
    # platform with no policies at all produces a cloud_policy.proto whose
    # import of policy_common_definitions.proto is unused -- which protoc,
    # run with --fatal_warnings, rejects. Haiku's policy surface is a desktop
    # Linux one, so it reads that list.
    ("components/policy/tools/generate_policy_source.gni",
     '      "--target-platform=" + target_os,\n',
     '      "--target-platform=" + _policy_target_platform,\n'),
    ("components/policy/tools/generate_policy_source.gni",
     '    script = "//components/policy/tools/generate_policy_source.py"\n',
     '    script = "//components/policy/tools/generate_policy_source.py"\n'
     '\n'
     '    _policy_target_platform = target_os\n'
     '    if (target_os == "haiku") {\n'
     '      _policy_target_platform = "linux"\n'
     '    }\n'),
    ("content/shell/app/shell_main_delegate.cc",
     '#include "content/shell/app/shell_crash_reporter_client.h"\n',
     '#if !BUILDFLAG(IS_HAIKU)\n'
     '#include "content/shell/app/shell_crash_reporter_client.h"\n'
     '#endif\n'),
    ("content/shell/app/shell_main_delegate.cc",
     '#if !BUILDFLAG(IS_FUCHSIA)\n'
     '#include "components/crash/core/app/crashpad.h"  // nogncheck\n'
     '#endif\n',
     '#if !BUILDFLAG(IS_FUCHSIA) && !BUILDFLAG(IS_HAIKU)\n'
     '#include "components/crash/core/app/crashpad.h"  // nogncheck\n'
     '#endif\n'),
    ("content/shell/app/shell_main_delegate.cc",
     '#if !BUILDFLAG(IS_FUCHSIA)\n'
     'content::ShellCrashReporterClient& GetShellCrashReporterClient() {\n',
     '#if !BUILDFLAG(IS_FUCHSIA) && !BUILDFLAG(IS_HAIKU)\n'
     'content::ShellCrashReporterClient& GetShellCrashReporterClient() {\n'),
    ("content/shell/app/shell_main_delegate.cc",
     '#if !BUILDFLAG(IS_FUCHSIA)\n'
     '  if (base::CommandLine::ForCurrentProcess()->HasSwitch(\n'
     '          switches::kEnableCrashReporter)) {\n',
     '#if !BUILDFLAG(IS_FUCHSIA) && !BUILDFLAG(IS_HAIKU)\n'
     '  if (base::CommandLine::ForCurrentProcess()->HasSwitch(\n'
     '          switches::kEnableCrashReporter)) {\n'),

    # Same reason, two more doors into //chrome. //extensions names
    # //chrome:resources and //chrome/common:buildflags from its renderer and
    # test-support targets, and loading either chrome file defines the whole
    # browser's target graph, which then has to resolve. content_shell has no
    # extensions, and this tree only ever builds for Haiku, so both deps are
    # dropped outright rather than guarded.
    ("extensions/renderer/BUILD.gn",
     '    "//build:android_buildflags",\n'
     '    "//build:chromeos_buildflags",\n'
     '    "//chrome:resources",\n'
     '    "//components/crx_file",\n',
     '    "//build:android_buildflags",\n'
     '    "//build:chromeos_buildflags",\n'
     '    "//components/crx_file",\n'),
    ("extensions/BUILD.gn",
     '    "//build:chromeos_buildflags",\n'
     '    "//chrome/common:buildflags",\n'
     '    "//components/crx_file",\n',
     '    "//build:chromeos_buildflags",\n'
     '    "//components/crx_file",\n'),

    ("content/test/BUILD.gn",
     'group("telemetry_gpu_integration_test_scripts_only") {\n'
     '  testonly = true\n'
     '  deps = [\n'
     '    "//tools/perf/chrome_telemetry_build:telemetry_chrome_test_without_chrome",\n'
     '  ]\n',
     'group("telemetry_gpu_integration_test_scripts_only") {\n'
     '  testonly = true\n'
     '  deps = []\n'
     '  if (!is_haiku) {\n'
     '    deps += [ "//tools/perf/chrome_telemetry_build:telemetry_chrome_test_without_chrome" ]\n'
     '  }\n'),
]


def main(src):
    applied = skipped = drifted = 0
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
            drifted += 1
            continue
        open(p, "w").write(s.replace(old, new, 1))
        applied += 1
    print("  content: %d applied, %d already done, %d drifted"
          % (applied, skipped, drifted))


if __name__ == "__main__":
    main(sys.argv[1])
