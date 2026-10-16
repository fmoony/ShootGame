"""在独立目录内制作完整构建快照；归档输出不进入自身，原始文件保持不动。"""

import datetime
import hashlib
import json
import os
import shutil
import signal
import stat
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path('/home/ash-run/shootgame-dedicated-20261014')
RUN = sys.argv[1]
assert RUN.startswith('full-') and len(RUN) == 21 and RUN[5:13].isdigit()
OUT = ROOT / 'backups' / RUN
assert os.getuid() == 1000 and OUT.is_dir()
PART_BYTES = 1024 ** 3
MAX_BYTES = 72 * PART_BYTES
START = datetime.datetime.now(datetime.timezone.utc).isoformat()


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def state(phase, **fields):
    value = dict(run=RUN, phase=phase, started_utc=START,
                 updated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), **fields)
    write_json(OUT / 'Status.json', value)
    print(json.dumps(value, ensure_ascii=False), flush=True)


def entries():
    yield ROOT
    for directory, dirs, files in os.walk(ROOT, followlinks=False):
        parent = Path(directory)
        dirs[:] = sorted(name for name in dirs if not (
            parent == ROOT / 'backups' and name.startswith('full-')))
        for name in sorted(dirs + files):
            yield parent / name


def record(path):
    item = path.lstat()
    kind = ('file' if stat.S_ISREG(item.st_mode) else 'directory' if stat.S_ISDIR(item.st_mode)
            else 'symlink' if stat.S_ISLNK(item.st_mode) else 'special')
    result = dict(path=path.relative_to(ROOT).as_posix(), type=kind, size=item.st_size,
                  mode=stat.S_IMODE(item.st_mode), uid=item.st_uid, gid=item.st_gid,
                  mtime_ns=item.st_mtime_ns)
    if kind == 'symlink':
        result['target'] = os.readlink(path)
    return result


def command(args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT).strip()


def sha256(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        while block := source.read(4 * 1024 * 1024):
            result.update(block)
    return result.hexdigest()


process = None
try:
    state('Preflight')
    running = command(['systemctl', 'list-units', '--state=running', '--no-legend',
                       '--plain', 'shootgame-*'])
    assert not any(line and 'shootgame-fullbackup-' not in line for line in running.splitlines()), running
    assert shutil.disk_usage(ROOT).free > MAX_BYTES + 80 * PART_BYTES
    version = json.loads((ROOT / 'engine/Engine/Build/Build.version').read_text())
    assert version['Changelist'] == 44394996 and version['CompatibleChangelist'] == 43139311
    packages = command(['dpkg-query', '-W', '-f=${Package} ${Version}\n', 'build-essential',
                        'gcc', 'g++', 'make', 'libc6', 'libstdc++6', 'libssl3t64',
                        'zlib1g', 'python3', 'git', 'zstd', 'pigz', 'tar'])
    environment = dict(source_root=str(ROOT), source_commit='6978b63c8951e57d97048d8424a0bebd637dde1d',
                       source_tag='5.6.1-release', build_version=version,
                       system=Path('/etc/os-release').read_text(), uname=command(['uname', '-a']),
                       user=command(['getent', 'passwd', 'ash-run']), packages=packages,
                       toolchain='v25_clang-18.1.0-rockylinux8', server_default_map='Lvl_Test',
                       excluded=['backups/full-*'], compression='zstd -T4 -3',
                       linux_editor_built=False, cook_origin='Windows Launcher UE 5.6.1')
    write_json(OUT / 'Environment.json', environment)
    state('Inventory')
    count = 0
    logical_bytes = 0
    with (OUT / 'FileManifest.jsonl').open('x', encoding='utf-8') as inventory:
        for path in entries():
            item = record(path)
            assert item['type'] != 'special', item['path']
            inventory.write(json.dumps(item, ensure_ascii=False, separators=(',', ':')) + '\n')
            count += 1
            if item['type'] == 'file':
                logical_bytes += item['size']
    state('Compressing', entries=count, logical_bytes=logical_bytes, archive_bytes=0, parts=0)
    pipeline = ('tar --format=pax --pax-option=delete=atime,delete=ctime --acls --xattrs '
                '--numeric-owner --sparse --exclude="./backups/full-*" -cpf - -C "$1" . '
                '| zstd -T4 -3 -c')
    process = subprocess.Popen(['bash', '-o', 'pipefail', '-c', pipeline, 'backup', str(ROOT)],
                               stdout=subprocess.PIPE, start_new_session=True)
    parts = []
    archive_hash = hashlib.sha256()
    total = 0
    last_status = 0
    stream = None
    while block := process.stdout.read(4 * 1024 * 1024):
        if stream is None:
            name = f'archive.tar.zst.part-{len(parts):04d}'
            stream = (OUT / name).open('xb')
            part_hash = hashlib.sha256()
            part_size = 0
        stream.write(block)
        part_hash.update(block)
        archive_hash.update(block)
        part_size += len(block)
        total += len(block)
        assert total <= MAX_BYTES, 'Archive exceeds 72 GiB capacity limit'
        if part_size >= PART_BYTES:
            stream.flush()
            os.fsync(stream.fileno())
            stream.close()
            stream = None
            parts.append(dict(name=name, bytes=part_size, sha256=part_hash.hexdigest()))
        if time.monotonic() - last_status >= 30:
            assert shutil.disk_usage(ROOT).free >= 80 * PART_BYTES, 'Free disk below 80 GiB'
            state('Compressing', entries=count, logical_bytes=logical_bytes,
                  archive_bytes=total, parts=len(parts))
            last_status = time.monotonic()
    if stream is not None:
        stream.flush()
        os.fsync(stream.fileno())
        stream.close()
        parts.append(dict(name=name, bytes=part_size, sha256=part_hash.hexdigest()))
    assert process.wait() == 0, 'tar/zstd failed; partial output retained'
    assert parts
    state('CheckingSourceStability', entries=count, archive_bytes=total, parts=len(parts))
    checked = 0
    with (OUT / 'FileManifest.jsonl').open(encoding='utf-8') as inventory:
        for line in inventory:
            before = json.loads(line)
            after = record(ROOT / before['path'])
            assert before == after, f'Source changed during backup: {before["path"]}'
            checked += 1
    assert sum(1 for _ in entries()) == checked
    write_json(OUT / 'SourceConsistency.json', dict(verified=True, entries=checked))
    (OUT / 'RESTORE.md').write_text('''# ShootGame Linux 构建环境独立备份

这是一个分卷的完整 tar.zst 归档；按文件名顺序连接所有 archive.tar.zst.part-* 即为完整压缩流。
包括源码、第三方依赖、官方工具链、Binaries、Intermediate、缓存、工程、Cook、产物与日志。
排除 backups/full-*，避免把完整备份再次装入自身；原始资源未删除。

先在 Linux 中执行 sha256sum -c SHA256SUMS，检查全部分卷及元数据。
恢复建议使用 Ubuntu 24.04 x86_64、GNU tar 和 zstd，并参考 Environment.json 恢复系统依赖。
为了复用含绝对路径的构建记录，建议使用原路径和 ash-run 的 UID/GID 1000。
必须选择空的恢复目录，不覆盖已有用户文件。以下命令仅供日后手动执行：

```bash
RESTORE_ROOT=/home/ash-run/shootgame-dedicated-20261014
test ! -e "$RESTORE_ROOT" || exit 1
mkdir -p "$RESTORE_ROOT"
set -o pipefail
cat archive.tar.zst.part-* | zstd -d -c | tar --acls --xattrs --same-owner -xpf - -C "$RESTORE_ROOT"
```

保留归档内的 Build.version，发布 CL=44394996、兼容 CL=43139311。
恢复后先检查编译依赖，再以普通用户进行 UBT 增量验证；不能承诺恢复后完全不重编。
Linux Editor 未构建；当前 Cook 流程依赖 Windows Launcher UE5.6.1。
本备份不包含服务器其他实验、系统盘镜像、SSH 私钥或登录密码。
''', encoding='utf-8')
    metadata = []
    for name in ('Environment.json', 'FileManifest.jsonl', 'SourceConsistency.json', 'RESTORE.md'):
        path = OUT / name
        metadata.append(dict(name=name, bytes=path.stat().st_size, sha256=sha256(path)))
    manifest = dict(format='shootgame-full-build-v1', run=RUN, source_root=str(ROOT),
                    archive_bytes=total, logical_bytes=logical_bytes, entries=count,
                    archive_sha256=archive_hash.hexdigest(), parts=parts, metadata=metadata)
    write_json(OUT / 'BackupManifest.json', manifest)
    checksums = parts + metadata + [dict(name='BackupManifest.json', sha256=sha256(OUT / 'BackupManifest.json'))]
    (OUT / 'SHA256SUMS').write_text(''.join(f'{p["sha256"]}  {p["name"]}\n' for p in checksums), encoding='ascii')
    state('Complete', entries=count, logical_bytes=logical_bytes, archive_bytes=total,
          parts=len(parts), archive_sha256=manifest['archive_sha256'])
except BaseException as error:
    if process is not None and process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait()
    state('Failed', error=str(error))
    raise
