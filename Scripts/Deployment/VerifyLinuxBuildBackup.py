"""流式校验分卷归档及文件元数据；不在 Windows 解压或恢复构建环境。"""

import decimal
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import threading
import time
from pathlib import Path

DEST = Path(sys.argv[1]).resolve()
assert DEST.is_relative_to(Path('G:/ShootGame_Backups').resolve())
manifest = json.loads((DEST / 'BackupManifest.json').read_text(encoding='utf-8'))
expected = {}
with (DEST / 'FileManifest.jsonl').open(encoding='utf-8') as stream:
    for line in stream:
        entry = json.loads(line)
        expected[entry['path']] = entry
assert len(expected) == manifest['entries']
excluded = [name for name in expected if name.startswith('backups/full-')]
for name in excluded:
    # GNU tar 的排除模式也匹配本轮零字节 advisory lock；源码清单仍保留其原始记录。
    assert name == 'backups/full-backup.lock' and expected[name]['size'] == 0, name
    del expected[name]
environment = os.environ.copy()
zstd = Path('D:/anaconda/Library/bin/zstd.exe')
environment['PATH'] = str(zstd.parent) + os.pathsep + environment.get('PATH', '')
errors = []
result = {}
start = time.monotonic()

with (DEST / 'Verification.log').open('ab') as log:
    process = subprocess.Popen([str(zstd), '-d', '-c'], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=log, env=environment)

    def feed():
        try:
            archive_hash = hashlib.sha256()
            total = 0
            for part in manifest['parts']:
                part_hash = hashlib.sha256()
                with (DEST / part['name']).open('rb') as stream:
                    while block := stream.read(4 * 1024 * 1024):
                        archive_hash.update(block)
                        part_hash.update(block)
                        total += len(block)
                        process.stdin.write(block)
                assert part_hash.hexdigest() == part['sha256'], part['name']
            assert total == manifest['archive_bytes']
            assert archive_hash.hexdigest() == manifest['archive_sha256']
            result['archive_sha256'] = archive_hash.hexdigest()
        except BaseException as error:
            errors.append(str(error))
        finally:
            process.stdin.close()

    feeder = threading.Thread(target=feed, daemon=True)
    feeder.start()
    count = 0
    regular_bytes = 0
    last_print = 0
    try:
        with tarfile.open(fileobj=process.stdout, mode='r|') as archive:
            for member in archive:
                name = member.name[2:] if member.name.startswith('./') else member.name
                name = name.rstrip('/') or '.'
                entry = expected.pop(name)
                kind = ('file' if member.isfile() else 'directory' if member.isdir()
                        else 'symlink' if member.issym() else 'special')
                assert kind == entry['type'], (name, kind, entry['type'])
                assert member.mode == entry['mode'] and member.uid == entry['uid']
                assert member.gid == entry['gid'], name
                mtime = decimal.Decimal(member.pax_headers.get('mtime', str(member.mtime)))
                assert int(mtime * 1000000000) == entry['mtime_ns'], ('mtime', name)
                if kind == 'file':
                    assert member.size == entry['size'], name
                    regular_bytes += member.size
                if kind == 'symlink':
                    assert member.linkname == entry['target'], name
                count += 1
                archive.members.clear()
                if time.monotonic() - last_print >= 30:
                    print(json.dumps(dict(phase='VerifyingArchive', entries=count,
                                          regular_bytes=regular_bytes)), flush=True)
                    last_print = time.monotonic()
        while process.stdout.read(4 * 1024 * 1024):
            pass
        feeder.join()
        assert process.wait() == 0, 'zstd checksum/decompression failed'
        assert not errors, errors
        assert not expected, list(expected)[:8]
        assert regular_bytes == manifest['logical_bytes']
        result.update(phase='Verified', entries=count, logical_bytes=regular_bytes,
                      archive_bytes=manifest['archive_bytes'], seconds=round(time.monotonic() - start, 1),
                      excluded_inventory_entries=excluded, restored=False, compiled=False)
        (DEST / 'Verification.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result), flush=True)
    except BaseException:
        process.kill()
        process.wait()
        raise
