#!/usr/bin/env python3
"""Install R Chromium (arm64) on Haiku from the prebuilt release tarball.

One-line install on the device (Haiku ships python3, not curl/tar):

    python3 -c "import urllib.request as u;exec(u.urlopen('https://raw.githubusercontent.com/rainygirl/renkuos-rchromium-arm64/main/install.py').read())"

Or from a checkout:  ./install.sh [--uninstall] [--prefix DIR] [--file TARBALL]

What it does:
  1. downloads rchromium-arm64.tar.gz from the latest GitHub release
     (verifies the .sha256 next to it),
  2. unpacks it into ~/config/non-packaged/apps/RChromium/,
  3. writes an "R Chromium" launcher next to it with the app icon,
  4. links it into Deskbar -> Applications and onto the Desktop, and
  5. adds an `rchromium` command to ~/config/non-packaged/bin/.
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request

REPO = "rainygirl/renkuos-rchromium-arm64"
ASSET = "rchromium-arm64.tar.gz"
URL = "https://github.com/%s/releases/latest/download/%s" % (REPO, ASSET)
APP = "R Chromium"
HOME = os.path.expanduser("~")
DEFAULT_PREFIX = os.path.join(HOME, "config", "non-packaged", "apps", "RChromium")
MENU_DIR = os.path.join(HOME, "config", "non-packaged", "data", "deskbar", "menu", "Applications")
BIN_DIR = os.path.join(HOME, "config", "non-packaged", "bin")
DESKTOP_DIR = os.path.join(HOME, "Desktop")
LIBRARY_PATH = ("%A/lib:" + HOME + "/config/non-packaged/lib:" + HOME + "/config/lib:"
                "/boot/system/non-packaged/lib:/boot/system/lib")


def die(msg):
    sys.stderr.write("install: %s\n" % msg)
    sys.exit(1)


def parse_args(argv):
    opts = {"prefix": DEFAULT_PREFIX, "file": None, "url": URL, "uninstall": False}
    it = iter(argv)
    for a in it:
        if a == "--uninstall":
            opts["uninstall"] = True
        elif a in ("--prefix", "--file", "--url"):
            try:
                opts[a[2:]] = next(it)
            except StopIteration:
                die("%s needs a value" % a)
        elif a in ("-h", "--help"):
            print(__doc__)
            sys.exit(0)
        else:
            die("unknown option %s (try --help)" % a)
    opts["prefix"] = os.path.abspath(opts["prefix"])
    return opts


def check_platform():
    u = os.uname()
    if u.sysname != "Haiku":
        die("this installs a Haiku application; run it on Haiku (got %s)" % u.sysname)
    if u.machine not in ("arm64", "aarch64"):
        die("this build is for arm64 Haiku only (got %s)" % u.machine)


def running():
    out = subprocess.run(["ps"], capture_output=True, text=True).stdout
    return any("content_shell" in line for line in out.splitlines())


def fetch(url, dest):
    print("Downloading %s" % url)
    req = urllib.request.Request(url, headers={"User-Agent": "rchromium-install"})
    with urllib.request.urlopen(req) as r, open(dest, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            if total:
                sys.stdout.write("\r  %3d%%  %d / %d MB" % (done * 100 // total, done >> 20, total >> 20))
            else:
                sys.stdout.write("\r  %d MB" % (done >> 20))
            sys.stdout.flush()
    print()


def verify(tarball, url):
    try:
        with urllib.request.urlopen(urllib.request.Request(url + ".sha256", headers={"User-Agent": "rchromium-install"})) as r:
            expected = r.read().decode().split()[0]
    except Exception as e:  # noqa: BLE001 - a missing checksum is a warning, not a failure
        print("  (no checksum published, skipping verification: %s)" % e)
        return
    h = hashlib.sha256()
    with open(tarball, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    if h.hexdigest() != expected:
        die("checksum mismatch for %s" % tarball)
    print("  checksum OK")


def extract(tarball, prefix):
    staging = prefix + ".new"
    shutil.rmtree(staging, ignore_errors=True)
    # Remove the old copy first: keeping it alongside the new one would need
    # twice the space (~350 MB) on the target volume.
    if os.path.exists(prefix):
        shutil.rmtree(prefix)
    os.makedirs(staging)
    print("Unpacking into %s" % prefix)
    with tarfile.open(tarball) as t:
        # The tarball has a single top-level directory; strip it.
        members = t.getmembers()
        top = members[0].name.split("/")[0]
        for m in members:
            parts = m.name.split("/", 1)
            if parts[0] != top or len(parts) == 1:
                continue
            m.name = parts[1]
            if hasattr(tarfile, "data_filter"):
                t.extract(m, staging, filter="data")
            else:
                t.extract(m, staging)
    if not os.path.exists(os.path.join(staging, "content_shell")):
        shutil.rmtree(staging, ignore_errors=True)
        die("tarball does not contain content_shell")
    os.rename(staging, prefix)


def run(cmd):
    subprocess.run(cmd, check=False)


def link(target, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.lexists(path):
        os.remove(path)
    os.symlink(target, path)


LAUNCH_FLAGS = ("--ozone-platform=haiku --disable-gpu "
                "--in-process-gpu --disable-gpu-compositing")


def install_links(prefix):
    # Stock content_shell has no app signature or icon resources, and it needs
    # the software-compositing switches, so the thing Tracker, Deskbar and the
    # shell launch is a small script next to the binary: it cds into the app
    # dir (resources resolve relative to it), lets the runtime loader find
    # lib/libchromium_haiku.so via %A/lib, and passes the switches. Tracker
    # runs an executable script on double-click, and the script's BEOS:ICON
    # attribute is what the Desktop link and the Deskbar entry show.
    launcher = os.path.join(prefix, APP)
    with open(launcher, "w") as f:
        f.write('#!/bin/sh\n# R Chromium launcher (written by install.py)\n'
                'cd "%s" || exit 1\nLIBRARY_PATH="%s"\nexport LIBRARY_PATH\n'
                # Haiku's default of 256 descriptors is not enough once
                # renderers run in their own processes.
                'ulimit -n 8192 2>/dev/null\n'
                'exec ./content_shell %s "$@"\n' % (prefix, LIBRARY_PATH, LAUNCH_FLAGS))
    os.chmod(launcher, 0o755)
    icon = os.path.join(prefix, "rchromium.hvif")
    if os.path.exists(icon):
        run(["addattr", "-f", icon, "-t", "icon", "BEOS:ICON", launcher])
    link(launcher, os.path.join(MENU_DIR, APP))
    link(launcher, os.path.join(DESKTOP_DIR, APP))
    link(launcher, os.path.join(BIN_DIR, "rchromium"))


def uninstall(prefix):
    if running():
        die("%s is still running -- close it and run this again." % APP)
    for p in (os.path.join(MENU_DIR, APP), os.path.join(DESKTOP_DIR, APP), os.path.join(BIN_DIR, "rchromium")):
        if os.path.lexists(p):
            os.remove(p)
    shutil.rmtree(prefix, ignore_errors=True)
    print("Removed %s." % APP)


def main(argv):
    opts = parse_args(argv)
    check_platform()
    if opts["uninstall"]:
        uninstall(opts["prefix"])
        return
    if running():
        die("%s is still running -- close it and run this again." % APP)
    os.makedirs(os.path.dirname(opts["prefix"]), exist_ok=True)
    tarball = opts["file"]
    tmp = None
    if tarball is None:
        # Download next to the install dir: /boot/home is one volume, and the
        # system tmp dir may be on a small boot volume.
        tmp = tarball = opts["prefix"] + ".download.tar.gz"
        fetch(opts["url"], tarball)
        verify(tarball, opts["url"])
    elif not os.path.exists(tarball):
        die("no such file: %s" % tarball)
    try:
        extract(tarball, opts["prefix"])
    finally:
        if tmp and os.path.exists(tmp):
            os.remove(tmp)
    install_links(opts["prefix"])
    print("Installed %s to %s" % (APP, opts["prefix"]))
    print("Find it in Deskbar -> Applications -> %s, on the Desktop, or run `rchromium <URL>`." % APP)


if __name__ == "__main__":
    # Also true when exec()'d by the one-line installer, where any extra
    # arguments after `python3 -c "..."` land in sys.argv[1:] as usual.
    main(sys.argv[1:])
