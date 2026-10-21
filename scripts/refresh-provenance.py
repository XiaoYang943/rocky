"""从固定原件重建完整差异；明确迁移映射以外的缺失文件会报错。"""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--migration', type=Path)
args = parser.parse_args()
moves = json.loads(args.migration.read_text(encoding='utf-8')) if args.migration else {}
manifest_path = root / 'provenance/manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
deps = Path(os.environ.get('GEOSTUDIO_NATIVE_DEPS', str(root.parents[1] / 'demo/native-gis-demo/.deps')))
sha = lambda value: hashlib.sha256(value).hexdigest()
for source in manifest['sources']:
    archive = deps / 'downloads' / f"{source['name']}-{source['upstreamCommit']}.zip"
    assert sha(archive.read_bytes()) == source['archiveSha256'], archive
    file_manifest = root / source['fileManifest']
    entries = json.loads(file_manifest.read_text(encoding='utf-8-sig'))
    patch = []
    binary_deletions = []
    with zipfile.ZipFile(archive) as zipped:
        prefix = zipped.infolist()[0].filename.split('/')[0] + '/'
        for entry in entries:
            old = entry['path']
            path = moves.get(old, old)
            if old and path is None:
                entry['removedPath'] = old
            current = root / path if path else None
            if current:
                assert current.resolve().is_relative_to(root.resolve()), path
                assert current.is_file(), f'Missing mapped source: {path}'
            before = zipped.read(prefix + entry['upstreamPath'])
            assert sha(before) == entry['upstreamSha256']
            after = current.read_bytes() if current else b''
            entry.update(path=path, sha256=sha(after) if current else None)
            if before == after:
                continue
            try:
                b = before.decode('utf-8').replace('\r\n', '\n')
                a = after.decode('utf-8').replace('\r\n', '\n')
            except UnicodeDecodeError:
                if current is None:
                    # 删除原件的动作与身份足以重现差异；不把大样例重新嵌入发行补丁。
                    binary_deletions.append({
                        'operation': 'delete',
                        'upstreamPath': entry['upstreamPath'],
                        'upstreamSha256': entry['upstreamSha256'],
                        'upstreamBytes': len(before),
                        'removedPath': entry['removedPath'],
                    })
                    continue
                raise RuntimeError('Binary source change requires an explicit binary patch: ' + str(path))
            target = path or (entry['upstreamPath'] if source['name'] == 'rocky' else f"vendor/{source['name']}/{entry['upstreamPath']}")
            for line in difflib.unified_diff(b.splitlines(True), a.splitlines(True), fromfile='a/' + target, tofile='b/' + target if current else '/dev/null'):
                patch.append(line if line.endswith('\n') else line + '\n\\ No newline at end of file\n')
    file_manifest.write_text(json.dumps(entries, ensure_ascii=False, indent=2), encoding='utf-8')
    patch_path = root / 'provenance' / source['name'] / '005-capabilities.patch'
    patch_path.write_text(''.join(patch), encoding='utf-8', newline='')
    source['fileManifestSha256'] = sha(file_manifest.read_bytes())
    source['patches'] = [{'path': patch_path.name, 'sha256': sha(patch_path.read_bytes()), 'layout': 'final-paths', 'base': 'fixed upstream archive'}]
    source['binaryDeletions'] = binary_deletions
imported = {entry['path'] for source in manifest['sources'] for entry in json.loads((root / source['fileManifest']).read_text(encoding='utf-8')) if entry['path']}
manifest['ownedFiles'] = {}
for scope in manifest['sourceScopes']:
    for file in (root / scope).rglob('*'):
        if file.is_file() and file.relative_to(root).as_posix() not in imported:
            manifest['ownedFiles'][file.relative_to(root).as_posix()] = sha(file.read_bytes())
manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
print('Verified original archives; exported complete source patches and current file identities.')
