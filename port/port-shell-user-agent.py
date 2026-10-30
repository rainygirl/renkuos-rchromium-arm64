#!/usr/bin/env python3
"""Make R Chromium say what it is: Chromium 154 on Haiku.

content_shell is Chromium's test shell, and it identifies itself as such.
Its version is a placeholder, 999.77.34.5, so the user agent says
Chrome/999.0.0.0. The client hints say the brand is "content_shell" and the
platform "Unknown". Google's sign-in reads that as an embedded or automated
browser and refuses it with "This browser or app may not be secure"; that
also ends Slack's "Sign in with Google".

This sets content_shell's version to the Chromium version the tree is
(chrome/VERSION), and the client hints to brand "Chromium" on platform
"Haiku". Nothing is borrowed from another browser or OS: the user agent stays
"(Haiku) ... Chrome/<major>.0.0.0", the form Chromium itself builds.

Usage: port-shell-user-agent.py <chromium src>. Idempotent.
"""
import re
import sys

src = sys.argv[1]

version = {}
for line in open("%s/chrome/VERSION" % src):
    key, _, value = line.strip().partition("=")
    version[key] = value
full = "%(MAJOR)s.%(MINOR)s.%(BUILD)s.%(PATCH)s" % version
major = version["MAJOR"]

gn = "%s/content/shell/BUILD.gn" % src
s = open(gn).read()
new = re.sub(r'content_shell_version = "[^"]*"',
             'content_shell_version = "%s"' % full, s, count=1)
new = re.sub(r'content_shell_major_version = "[^"]*"',
             'content_shell_major_version = "%s"' % major, new, count=1)
if new == s:
    print("  content/shell/BUILD.gn: version already %s" % full)
else:
    open(gn, "w").write(new)
    print("  content/shell/BUILD.gn: version %s" % full)

cc = "%s/content/shell/browser/shell_content_browser_client.cc" % src
s = open(cc).read()
if '"Chromium", CONTENT_SHELL_MAJOR_VERSION' in s:
    print("  shell_content_browser_client.cc: already applied")
    raise SystemExit

old = ('  metadata.brand_version_list.emplace_back("content_shell",\n'
       '                                           CONTENT_SHELL_MAJOR_VERSION);\n'
       '  metadata.brand_full_version_list.emplace_back("content_shell",\n'
       '                                                CONTENT_SHELL_VERSION);\n'
       '  metadata.full_version = CONTENT_SHELL_VERSION;\n'
       '  metadata.platform = "Unknown";\n')
assert s.count(old) == 1, s.count(old)
s = s.replace(old, (
    '#if BUILDFLAG(IS_HAIKU)\n'
    '  // R Chromium: Chromium on Haiku, not the test shell on "Unknown".\n'
    '  metadata.brand_version_list.emplace_back("Chromium", CONTENT_SHELL_MAJOR_VERSION);\n'
    '  metadata.brand_full_version_list.emplace_back("Chromium", CONTENT_SHELL_VERSION);\n'
    '  metadata.full_version = CONTENT_SHELL_VERSION;\n'
    '  metadata.platform = "Haiku";\n'
    '#else\n') + old + '#endif\n', 1)
open(cc, "w").write(s)
print("  shell_content_browser_client.cc: brand Chromium, platform Haiku")
