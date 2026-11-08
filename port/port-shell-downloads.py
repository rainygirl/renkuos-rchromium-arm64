#!/usr/bin/env python3
"""Downloads that work, with a progress window.

content_shell's ShellDownloadManagerDelegate asks for a save path with a
dialog only on Windows; everywhere else ChooseDownloadPath() is
NOTIMPLEMENTED() and hands back an empty path, which cancels the download.
So nothing ever downloaded.

On Haiku this:
- saves to ~/Downloads (not the profile's Downloads folder), with no prompt,
  giving a name that is already taken a " (1)" suffix rather than
  overwriting it;
- attaches HaikuDownloadUI (content/shell/browser/haiku_download_ui.cc, from
  port/files), which feeds the shim's native Downloads window: progress,
  Cancel, "Show in folder", and the Downloads folder opening in Tracker when
  a download completes.

Usage: port-shell-downloads.py <chromium src>. Idempotent.
"""
import sys

src = sys.argv[1]


def patch(path, edits):
    s = open(path).read()
    if "HaikuDownloadUI" in s or "RCH downloads" in s:
        print("  %s: already applied" % path.split("/src/")[-1])
        return
    for old, new in edits:
        n = s.count(old)
        if n != 1:
            print("  %s: DRIFTED (%r found %d times)" % (path, old[:60], n))
            sys.exit(1)
        s = s.replace(old, new, 1)
    open(path, "w").write(s)
    print("  %s: patched" % path.split("/src/")[-1])


patch("%s/content/shell/browser/shell_download_manager_delegate.h" % src, [
    ('#include <stdint.h>\n',
     '#include <stdint.h>\n\n#include <memory>\n'),
    ('#include "content/public/browser/download_manager_delegate.h"\n',
     '#include "build/build_config.h"\n'
     '#include "content/public/browser/download_manager_delegate.h"\n'),
    ('class DownloadManager;\n',
     'class DownloadManager;\n'
     '#if BUILDFLAG(IS_HAIKU)\n'
     'class HaikuDownloadUI;\n'
     '#endif\n'),
    ('  bool suppress_prompting_;\n',
     '  bool suppress_prompting_;\n'
     '#if BUILDFLAG(IS_HAIKU)\n'
     '  // The native Downloads window\'s feed. See haiku_download_ui.h.\n'
     '  std::unique_ptr<HaikuDownloadUI> haiku_download_ui_;\n'
     '#endif\n'),
])

patch("%s/content/shell/browser/shell_download_manager_delegate.cc" % src, [
    ('#include "net/base/filename_util.h"\n',
     '#include "net/base/filename_util.h"\n'
     '\n'
     '#if BUILDFLAG(IS_HAIKU)\n'
     '#include "content/shell/browser/haiku_download_ui.h"\n'
     '#endif\n'),
    ('ShellDownloadManagerDelegate::~ShellDownloadManagerDelegate() {\n',
     'ShellDownloadManagerDelegate::~ShellDownloadManagerDelegate() {\n'
     '#if BUILDFLAG(IS_HAIKU)\n'
     '  haiku_download_ui_.reset();\n'
     '#endif\n'),
    ('  download_manager_ = download_manager;\n}\n',
     '  download_manager_ = download_manager;\n'
     '#if BUILDFLAG(IS_HAIKU)\n'
     '  haiku_download_ui_.reset();\n'
     '  if (download_manager_) {\n'
     '    haiku_download_ui_ =\n'
     '        std::make_unique<HaikuDownloadUI>(download_manager_);\n'
     '  }\n'
     '#endif\n'
     '}\n'),
    ('  weak_ptr_factory_.InvalidateWeakPtrs();\n',
     '  weak_ptr_factory_.InvalidateWeakPtrs();\n'
     '#if BUILDFLAG(IS_HAIKU)\n'
     '  haiku_download_ui_.reset();\n'
     '#endif\n'),
    ('  if (default_download_path_.empty()) {\n'
     '    default_download_path_ = download_manager_->GetBrowserContext()\n',
     '#if BUILDFLAG(IS_HAIKU)\n'
     '  // RCH downloads: ~/Downloads, where the user looks, not the profile.\n'
     '  if (default_download_path_.empty()) {\n'
     '    default_download_path_ =\n'
     '        base::GetHomeDir().Append(FILE_PATH_LITERAL("Downloads"));\n'
     '  }\n'
     '#endif\n'
     '  if (default_download_path_.empty()) {\n'
     '    default_download_path_ = download_manager_->GetBrowserContext()\n'),
    ('  base::FilePath suggested_path(suggested_directory.Append(generated_name));\n',
     '  base::FilePath suggested_path(suggested_directory.Append(generated_name));\n'
     '#if BUILDFLAG(IS_HAIKU)\n'
     '  // There is no save dialog to ask about it, so never overwrite an\n'
     '  // earlier download: "name (1).ext" instead.\n'
     '  if (base::FilePath unique = base::GetUniquePath(suggested_path);\n'
     '      !unique.empty()) {\n'
     '    suggested_path = unique;\n'
     '  }\n'
     '#endif\n'),
    # No save dialog on Haiku: take the generated path, and write it as
    # name.crdownload until it is complete, as the testing exit does.
    ('#else\n  NOTIMPLEMENTED();\n#endif\n',
     '#elif BUILDFLAG(IS_HAIKU)\n'
     '  result = suggested_path;\n'
     '#else\n  NOTIMPLEMENTED();\n#endif\n'),
    ('  target_info.intermediate_path = result;\n'
     '  target_info.target_disposition =\n'
     '      download::DownloadItem::TARGET_DISPOSITION_PROMPT;\n',
     '#if BUILDFLAG(IS_HAIKU)\n'
     '  target_info.intermediate_path =\n'
     '      result.empty() ? result\n'
     '                     : result.AddExtension(FILE_PATH_LITERAL(".crdownload"));\n'
     '  target_info.target_disposition =\n'
     '      download::DownloadItem::TARGET_DISPOSITION_OVERWRITE;\n'
     '#else\n'
     '  target_info.intermediate_path = result;\n'
     '  target_info.target_disposition =\n'
     '      download::DownloadItem::TARGET_DISPOSITION_PROMPT;\n'
     '#endif\n'),
])

gn = "%s/content/shell/BUILD.gn" % src
s = open(gn).read()
if "haiku_download_ui.cc" in s:
    print("  content/shell/BUILD.gn: downloads already listed")
else:
    old = '        "browser/shell_platform_delegate_haiku.cc",\n'
    assert s.count(old) == 1, s.count(old)
    s = s.replace(old,
                  '        "browser/haiku_download_ui.cc",\n'
                  '        "browser/haiku_download_ui.h",\n' + old, 1)
    open(gn, "w").write(s)
    print("  content/shell/BUILD.gn: haiku_download_ui added")
