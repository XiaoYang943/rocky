"""核验新安装目录的来源闭包、原许可及 runtime 资源，不执行渲染。"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--installed", required=True, type=Path)
package = parser.parse_args().installed / "share/GeoStudioNative"
manifest = json.loads((package / "provenance/manifest.json").read_text(encoding="utf-8-sig"))
expected = {"provenance/manifest.json"}
for source in manifest["sources"]:
    entries = [(source["fileManifest"], source["fileManifestSha256"])]
    entries += [(f"provenance/{source['name']}/{patch['path']}", patch["sha256"]) for patch in source["patches"]]
    for relative, identity in entries:
        path = package / relative
        assert path.resolve().is_relative_to(package.resolve()), relative
        assert hashlib.sha256(path.read_bytes()).hexdigest() == identity, relative
        expected.add(relative)
actual = {p.relative_to(package).as_posix() for p in (package / "provenance").rglob("*") if p.is_file()}
assert actual == expected, f"Unexpected provenance contents: {actual ^ expected}"
for relative in expected:
    assert (package / relative).read_bytes() == (ROOT / relative).read_bytes(), relative
licenses = {"rocky/LICENSE.txt": "LICENSE.txt", "vsgCs/LICENSE.md": "provenance/vsgCs/upstream/LICENSE.md",
            "vsgXchange/LICENSE.md": "provenance/vsgXchange/upstream/LICENSE.md"}
for installed, original in licenses.items():
    assert (package / "licenses" / installed).read_bytes() == (ROOT / original).read_bytes(), installed
resources = [p for p in (ROOT / "resources").rglob("*") if p.is_file()]
for path in resources:
    relative = path.relative_to(ROOT)
    assert (package / relative).read_bytes() == path.read_bytes(), relative
print(f"Package contents verified: {len(expected)} current provenance files, {len(licenses)} licenses, {len(resources)} runtime resources.")
