#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def fail(message: str) -> None:
    raise SystemExit(f"release metadata error: {message}")

def project_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(Emojineer VERSION ([0-9]+\.[0-9]+\.[0-9]+)", text)
    if not match:
        fail("CMake project version was not found")
    return match.group(1)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", default="", help="optional release tag, e.g. v1.0.0")
    args = parser.parse_args()

    version = project_version()
    package_path = ROOT / "editors" / "vscode" / "package.json"
    package = json.loads(package_path.read_text(encoding="utf-8"))
    editor_version = str(package.get("version", ""))
    if editor_version != version:
        fail(f"CMake version {version} != VS Code extension version {editor_version}")

    if args.tag:
        match = re.fullmatch(r"v?([0-9]+\.[0-9]+\.[0-9]+)", args.tag)
        if not match:
            fail(f"release tag {args.tag!r} is not an exact vMAJOR.MINOR.PATCH tag")
        if match.group(1) != version:
            fail(f"release tag {args.tag} does not match project version {version}")

    major = int(version.split(".", 1)[0])
    license_files = [
        path for name in ("LICENSE", "LICENSE.md", "LICENSE.txt", "COPYING")
        if (path := ROOT / name).is_file()
    ]
    editor_license = str(package.get("license", "")).strip()

    if major >= 1:
        if not license_files:
            fail("1.x release requires an explicit tracked root license file")
        if not editor_license:
            fail("1.x release requires editors/vscode/package.json license metadata")
        if editor_license.upper() == "UNLICENSED":
            fail("1.x release license metadata is still UNLICENSED; record the owner decision explicitly")

    print(f"release metadata verified for Emojineer {version}")
    if major < 1 and not license_files:
        print("note: pre-1.0 build has no license decision yet; a 1.x release will be blocked")

if __name__ == "__main__":
    main()
