"""显式应用或只读验证固定 VSG 资源回收补丁，不下载、不升级依赖。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--deps", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--record-install", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    entry = next(x for x in json.loads((root / "dependencies.lock.json").read_text(encoding="utf-8"))["sources"] if x["name"] == "vsg")
    patch = root / entry["patch"]
    if digest(patch.read_bytes()) != entry["patchSha256"]:
        raise RuntimeError("VSG patch identity mismatch")
    archive = args.deps / "downloads" / ("vsg-" + entry["commit"] + ".zip")
    if digest(archive.read_bytes()) != entry["archiveSha256"]:
        raise RuntimeError("VSG archive identity mismatch")
    source = args.deps / "src" / entry["directory"]
    file_map = root / entry["patchFiles"]
    if digest(file_map.read_bytes()) != entry["patchFilesSha256"]:
        raise RuntimeError("VSG source map identity mismatch")
    files = json.loads(file_map.read_text(encoding="utf-8"))
    with zipfile.ZipFile(archive) as upstream:
        for item in files:
            raw = upstream.read(entry["directory"] + "/" + item["path"]).replace(b"\r\n", b"\n")
            if digest(raw) != item["upstreamSha256"]:
                raise RuntimeError("VSG original mismatch: " + item["path"])
    hashes = [digest((source / x["path"]).read_bytes().replace(b"\r\n", b"\n")) for x in files]
    if hashes != [x["sha256"] for x in files]:
        if not args.apply or hashes != [x["upstreamSha256"] for x in files]:
            raise RuntimeError("VSG source differs; run explicit bootstrap -Only vsg for the untouched fixed source. Existing edits are preserved.")
        subprocess.run(["git", "-C", str(source), "apply", "--check", str(patch)], check=True)
        subprocess.run(["git", "-C", str(source), "apply", str(patch)], check=True)
    for item in files:
        if digest((source / item["path"]).read_bytes().replace(b"\r\n", b"\n")) != item["sha256"]:
            raise RuntimeError("VSG patched source mismatch: " + item["path"])
    if args.apply:
        print("Verified fixed VSG resource-retirement source patch")
        return
    install = args.deps / "install"
    identity = install / "share/geostudio-vsg-resource-retirement.json"
    binaries = {str(p.relative_to(install)).replace("\\", "/"): digest(p.read_bytes()) for p in [install / "bin/vsg-17.dll", install / "lib/vsg.lib"]}
    expected = {"commit": entry["commit"], "patchSha256": entry["patchSha256"], "binaries": binaries}
    if args.record_install:
        identity.parent.mkdir(parents=True, exist_ok=True)
        identity.write_text(json.dumps(expected, indent=2) + "\n", encoding="utf-8")
    else:
        if not identity.is_file() or json.loads(identity.read_text(encoding="utf-8")) != expected:
            raise RuntimeError("VSG installed binary identity mismatch; explicitly rebuild bootstrap -Only vsg")
    print("Verified fixed VSG resource-retirement patch and installed binaries")


if __name__ == "__main__":
    main()
