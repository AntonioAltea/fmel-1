#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MEL's web site (GitHub Pages: .github/workflows/pages.yml): the simulator and the installer, as static files.

  tools/publish_web.py SITE [--fwsc PACKAGE] [--version V] [--install-from DIR]

SITE/            the simulator (build/emu: web/emu/build.sh)
SITE/install/    the installer (web/install, or --install-from) with the package
                 (--fwsc, default build/mel.fwsc: ./build.sh) and mel.json, its SHA-256, identity and version (--version,
                 default git describe), which the page checks before it installs anything
SITE/style/      web/site/style: the look of www.manturon.es over both pages
SITE/instalar/   the installer's first address (Spanish): a redirect to install/
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "tools"))
import fm1_install  # noqa: E402


def head(html, style):
    return html.replace("</head>", f'<link rel="stylesheet" href="{style}">\n</head>', 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("site", type=Path)
    ap.add_argument("--fwsc", type=Path, default=SRC / "build" / "mel.fwsc", help="the package the installer offers")
    ap.add_argument("--version", help="the package's version, as the installer shows it (default: git describe)")
    ap.add_argument("--install-from", type=Path, default=SRC / "web" / "install", help="the installer's page")
    a = ap.parse_args()
    site, inst, emu = a.site, a.site / "install", SRC / "build" / "emu"
    for need in (emu / "mel.wasm", a.fwsc):
        if not need.exists():
            raise SystemExit(f"{need} missing: run web/emu/build.sh and ./build.sh first")
    product, image = fm1_install.load_package(a.fwsc, False)   # whole (its CRCs) and with the Felucca loader
    if fm1_install.package_damage(image) or not product.startswith("FM-1_95"):
        raise SystemExit(f"{a.fwsc}: not a whole MEL package ({product})")

    inst.mkdir(parents=True, exist_ok=True)
    shutil.copytree(SRC / "web" / "site" / "style", site / "style", dirs_exist_ok=True)
    for f in ("worklet.js", "mel.wasm"):
        shutil.copy2(emu / f, site / f)
    sim = head((emu / "index.html").read_text(), "style/mel.css")
    (site / "index.html").write_text(sim)
    (inst / "index.html").write_text(head((a.install_from / "index.html").read_text(), "../style/mel.css"))
    for f in ("install.js", "fm1ota.js", "fm1pkg.js"):
        shutil.copy2(a.install_from / f, inst / f)
    raw = a.fwsc.read_bytes()
    shutil.copy2(a.fwsc, inst / "mel.fwsc")
    ver = a.version or subprocess.run(["git", "-C", str(SRC), "describe", "--tags", "--always", "--dirty"],
                                      capture_output=True, text=True).stdout.strip() or "dev"
    meta = {"file": "mel.fwsc", "sha256": hashlib.sha256(raw).hexdigest(), "product": product, "version": ver}
    (inst / "mel.json").write_text(json.dumps(meta, indent=1) + "\n")
    (site / ".nojekyll").write_text("")
    (site / "instalar").mkdir(exist_ok=True)
    (site / "instalar" / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0; url=../install/">'
        '<link rel="canonical" href="../install/"><a href="../install/">install/</a>\n')
    print(f"site -> {site}/  (installer: {product}, {ver}, sha256 {meta['sha256'][:12]}…)")


if __name__ == "__main__":
    main()
