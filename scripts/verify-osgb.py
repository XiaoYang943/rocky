"""只读核对 OSGB 固定原件、转换器源码及 OSG SDK；不下载或修复依赖。"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--dependencies', type=Path, required=True)
parser.add_argument('--osg', type=Path, required=True)
parser.add_argument('--gdal', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
digest = lambda data: hashlib.sha256(data).hexdigest()
lock = json.loads((root / 'dependencies.lock.json').read_text(encoding='utf-8-sig'))
entry = next(item for item in lock['sources'] if item['name'] == 'osg2vsg')
archive = args.dependencies / 'downloads' / f"osg2vsg-{entry['commit']}.zip"
assert digest(archive.read_bytes()) == entry['archiveSha256'].lower(), 'osg2vsg archive mismatch'
source = args.dependencies / 'src' / entry['directory']
with zipfile.ZipFile(archive) as zipped:
    prefix = zipped.infolist()[0].filename.split('/')[0] + '/'
    expected = set()
    for item in zipped.infolist():
        if item.is_dir():
            continue
        relative = item.filename[len(prefix):]
        expected.add(relative)
        assert (source / relative).read_bytes() == zipped.read(item), f'osg2vsg source changed: {relative}'
    actual = {file.relative_to(source).as_posix() for file in source.rglob('*') if file.is_file()}
    assert actual == expected, 'osg2vsg source contains unexpected files'
manifest = json.loads((args.osg / 'sdk-manifest.json').read_text(encoding='utf-8-sig'))
package = json.loads((root.parents[1] / 'packages/osg/manifest.json').read_text(encoding='utf-8-sig'))
assert manifest['sourceCommit'] == package['commit'], 'OSG source commit mismatch'
assert manifest['gdalRuntimeManifestSha256'].lower() == digest((args.gdal / 'runtime-manifest.json').read_bytes()), 'OSG GDAL runtime mismatch'
for item in manifest['files']:
    path = (args.osg / item['path']).resolve()
    assert path.is_relative_to(args.osg.resolve()), 'OSG manifest path escapes SDK'
    assert digest(path.read_bytes()) == item['sha256'].lower(), f'OSG SDK changed: {item["path"]}'
print('Verified fixed osg2vsg archive/source and OSG SDK identity.')
