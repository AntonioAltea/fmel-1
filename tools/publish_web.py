#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MEL's web site (GitHub Pages: .github/workflows/pages.yml): the simulator and the installer, as static files.

  tools/publish_web.py SITE [--fwsc PACKAGE ...] [--version V] [--install-from DIR]

SITE/            the simulators (build/emu: web/emu/build.sh): the FM-1's (index.html) and the SMK-37's (smk37.html),
                 with their pictures (img/)
SITE/install/    the installer (web/install, or --install-from) with the packages (--fwsc, default build/mel.fwsc and
                 build/mel-smk37*.fwsc: ./build.sh, ./build.sh --board smk37 [--model elite|mke]): the FM-1's, and one
                 for each SMK-37 model there is, each by its identity; and mel.json, their SHA-256, identities and the
                 version (--version, default git describe), which the page checks before it installs anything
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


KEYS = {"fm1": "FM-1_95", "smk37-pro": "SMK-37 Pro_45", "smk37-elite": "SMK-37 Elite_45", "smk37-mke": "MKE-P37_45"}
FILES = {"fm1": "mel.fwsc", "smk37-pro": "mel-smk37.fwsc", "smk37-elite": "mel-smk37-elite.fwsc", "smk37-mke": "mel-smk37-mke.fwsc"}


def head(html, style):
    return html.replace("</head>", f'<link rel="stylesheet" href="{style}">\n</head>', 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("site", type=Path)
    ap.add_argument("--fwsc", type=Path, nargs="+", help="the packages the installer offers (an FM-1 one is needed)")
    ap.add_argument("--version", help="the package's version, as the installer shows it (default: git describe)")
    ap.add_argument("--install-from", type=Path, default=SRC / "web" / "install", help="the installer's page")
    a = ap.parse_args()
    site, inst, emu = a.site, a.site / "install", SRC / "build" / "emu"
    fwscs = a.fwsc or [SRC / "build" / "mel.fwsc", *sorted((SRC / "build").glob("mel-smk37*.fwsc"))]
    for need in (emu / "mel.wasm", emu / "mel-smk37.wasm", *fwscs):
        if not need.exists():
            raise SystemExit(f"{need} missing: run web/emu/build.sh and ./build.sh first")
    pkgs = {}                                   # the installer's key -> (path, identity)
    for f in fwscs:
        product, image = fm1_install.load_package(f, False)   # whole (its CRCs) and with the Felucca loader
        key = next((k for k, pre in KEYS.items() if product.startswith(pre) and len(product) == len(pre) + 1), None)
        if fm1_install.package_damage(image) or not key:
            raise SystemExit(f"{f}: not a whole MEL package ({product})")
        if key in pkgs:
            raise SystemExit(f"{f}: a second package for {key} ({pkgs[key][0]})")
        pkgs[key] = (f, product)
    if "fm1" not in pkgs:
        raise SystemExit("no FM-1 package (FM-1_95N) among --fwsc")

    inst.mkdir(parents=True, exist_ok=True)
    shutil.copytree(SRC / "web" / "site" / "style", site / "style", dirs_exist_ok=True)
    for f in ("worklet.js", "mel.wasm", "mel-smk37.wasm"):
        shutil.copy2(emu / f, site / f)
    shutil.copytree(emu / "img", site / "img", dirs_exist_ok=True)
    for f in ("index.html", "smk37.html"):
        (site / f).write_text(head((emu / f).read_text(), "style/mel.css"))
    (inst / "index.html").write_text(head((a.install_from / "index.html").read_text(), "../style/mel.css"))
    for f in ("install.js", "fm1ota.js", "fm1pkg.js"):
        shutil.copy2(a.install_from / f, inst / f)
    ver = a.version or subprocess.run(["git", "-C", str(SRC), "describe", "--tags", "--always", "--dirty"],
                                      capture_output=True, text=True).stdout.strip() or "dev"
    meta = {"version": ver, "packages": {}}
    for key, (f, product) in sorted(pkgs.items()):
        shutil.copy2(f, inst / FILES[key])
        meta["packages"][key] = {"file": FILES[key], "sha256": hashlib.sha256(f.read_bytes()).hexdigest(), "product": product}
    (inst / "mel.json").write_text(json.dumps(meta, indent=1) + "\n")
    (site / ".nojekyll").write_text("")
    (site / "instalar").mkdir(exist_ok=True)
    (site / "instalar" / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0; url=../install/">'
        '<link rel="canonical" href="../install/"><a href="../install/">install/</a>\n')
    print(f"site -> {site}/  (installer {ver}: " + ", ".join(p for _, p in sorted(pkgs.values())) + ")")


if __name__ == "__main__":
    main()
