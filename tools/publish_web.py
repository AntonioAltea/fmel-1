#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MEL for a web site: the simulator and the installer, ready to serve as static files.

  tools/publish_web.py SIM_DIR INSTALL_DIR [--sim-url URL] [--install-url URL] [--head HTML]

SIM_DIR       the simulator (build/emu: web/emu/build.sh)
INSTALL_DIR   the installer (web/install) with the package (build/mel.fwsc: ./build.sh) and mel.json, its SHA-256 and
              identity, which the page checks before it installs anything
--sim-url / --install-url: where each one is served, for the links between them (default: as sibling folders)
Both pages carry noindex (a page meant only for whoever has its address). --head adds HTML at the end of both pages'
<head> (a site's own stylesheet, say). Build both first.
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

NOINDEX = '<meta name="robots" content="noindex, nofollow">'


def page(html, head):
    if NOINDEX not in html:
        html = html.replace('<meta charset="utf-8">', '<meta charset="utf-8">\n' + NOINDEX, 1)
    return html.replace("</head>", head + "\n</head>", 1) if head else html


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sim_dir")
    ap.add_argument("install_dir")
    ap.add_argument("--sim-url", help="the simulator's address (default: ../SIM_DIR's name/)")
    ap.add_argument("--install-url", help="the installer's address (default: ../INSTALL_DIR's name/)")
    ap.add_argument("--head", default="", help="HTML for the end of both pages' <head>")
    a = ap.parse_args()
    dest, inst = Path(a.sim_dir), Path(a.install_dir)
    sim_url = a.sim_url or f"../{dest.name}/"
    install_url = a.install_url or f"../{inst.name}/"
    emu, pkg = SRC / "build" / "emu", SRC / "build" / "mel.fwsc"
    for need in (emu / "mel.wasm", pkg):
        if not need.exists():
            raise SystemExit(f"{need} missing: run web/emu/build.sh and ./build.sh first")
    product, image = fm1_install.load_package(pkg, False)      # whole (its CRCs) and with the Felucca loader
    if fm1_install.package_damage(image) or not product.startswith("FM-1_95"):
        raise SystemExit(f"{pkg}: not a whole MEL package ({product})")
    dest.mkdir(parents=True, exist_ok=True)
    inst.mkdir(parents=True, exist_ok=True)
    for f in ("worklet.js", "mel.wasm"):
        shutil.copy2(emu / f, dest / f)
    sim = page((emu / "index.html").read_text(), a.head)
    sim = sim.replace("</header>", f'  <p><a href="{install_url}">Instalarlo en tu FM-1</a></p>\n</header>', 1)
    (dest / "index.html").write_text(sim)
    (inst / "index.html").write_text(page((SRC / "web" / "install" / "index.html").read_text(), a.head)
                                     .replace('href="../"', f'href="{sim_url}"'))
    for f in ("install.js", "fm1ota.js", "fm1pkg.js"):
        shutil.copy2(SRC / "web" / "install" / f, inst / f)
    raw = pkg.read_bytes()
    shutil.copy2(pkg, inst / "mel.fwsc")
    ver = subprocess.run(["git", "-C", str(SRC), "describe", "--always", "--dirty"], capture_output=True,
                         text=True).stdout.strip() or "dev"
    meta = {"file": "mel.fwsc", "sha256": hashlib.sha256(raw).hexdigest(), "product": product, "version": ver}
    (inst / "mel.json").write_text(json.dumps(meta, indent=1) + "\n")
    print(f"simulator -> {dest}/\ninstaller -> {inst}/  ({product}, {ver}, sha256 {meta['sha256'][:12]}…)")


if __name__ == "__main__":
    main()
