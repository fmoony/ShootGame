"""下载完整构建备份到用户指定的 G 盘目录；密码只在内存中保留。"""

import concurrent.futures
import getpass
import hashlib
import json
import re
import shlex
import shutil
import signal
import sys
import threading
import time
from pathlib import Path

from ShootGameRemote import HOST_FINGERPRINT, PinnedServerKey, REMOTE_ROOT, paramiko

RUN = sys.argv[1]
assert re.fullmatch(r'full-\d{8}T\d{6}Z', RUN)
BASE = Path('G:/ShootGame_Backups')
assert BASE.resolve() == BASE.absolute(), 'Backup base must not redirect outside its approved path'
DEST = BASE / ('FullBuild-' + RUN[5:])
REMOTE = REMOTE_ROOT + '/backups/' + RUN
DEST.mkdir(parents=True, exist_ok=True)
IDENTITY = dict(run=RUN, remote=REMOTE, host_fingerprint=HOST_FINGERPRINT)
identity_path = DEST / 'BackupIdentity.json'
if identity_path.exists():
    assert json.loads(identity_path.read_text(encoding='utf-8')) == IDENTITY
else:
    assert not any(DEST.iterdir()), 'Existing destination contains unrelated files'
    with identity_path.open('x', encoding='utf-8') as stream:
        json.dump(IDENTITY, stream, indent=2)


def save_json(name, value):
    temporary = DEST / (name + '.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    temporary.replace(DEST / name)


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(4 * 1024 * 1024):
            result.update(block)
    return result.hexdigest()


PASSWORD = getpass.getpass('SSH password (not saved): ')


def connect():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(PinnedServerKey())
    client.connect('103.236.77.127', username='root', password=PASSWORD,
                   look_for_keys=False, allow_agent=False, timeout=20, auth_timeout=20)
    client.get_transport().set_keepalive(20)
    return client


def download_metadata(sftp, name, expected=None):
    assert '/' not in name and '\\' not in name
    target = DEST / name
    if target.exists() and expected:
        assert target.stat().st_size == expected['bytes'] and digest(target) == expected['sha256']
        return
    partial = DEST / (name + '.partial')
    with partial.open('wb') as stream:
        sftp.getfo(REMOTE + '/' + name, stream, max_concurrent_prefetch_requests=32)
    if expected:
        assert partial.stat().st_size == expected['bytes'] and digest(partial) == expected['sha256'], name
    partial.replace(target)


lock = threading.Lock()
cancel = threading.Event()
progress = {}
completed = set()
started = time.monotonic()


def interrupt(signum, frame):
    cancel.set()
    raise KeyboardInterrupt('Download interrupted; verified prefixes retained')


signal.signal(signal.SIGINT, interrupt)


def download_part(item):
    name = item['name']
    assert re.fullmatch(r'archive\.tar\.zst\.part-\d{4}', name)
    final = DEST / name
    partial = DEST / (name + '.partial')
    if final.exists():
        assert final.stat().st_size == item['bytes'] and digest(final) == item['sha256'], name
        with lock:
            progress[name] = item['bytes']
            completed.add(name)
        return
    for attempt in range(1, 4):
        client = None
        try:
            if cancel.is_set():
                raise RuntimeError('Download cancelled')
            client = connect()
            offset = partial.stat().st_size if partial.exists() else 0
            assert offset <= item['bytes'], name
            if offset:
                command = f'head -c {offset} -- {shlex.quote(REMOTE + "/" + name)} | sha256sum'
                _, stdout, stderr = client.exec_command(command, timeout=120)
                remote_prefix = stdout.read().decode().split()[0]
                assert stdout.channel.recv_exit_status() == 0, stderr.read().decode()
                assert digest(partial) == remote_prefix, 'Partial prefix hash mismatch: ' + name
            with paramiko.SFTPClient.from_transport(client.get_transport(), window_size=8 * 1024 ** 2) as sftp:
                source = sftp.open(REMOTE + '/' + name, 'rb')
                source.settimeout(60)
                source.seek(offset)
                source.prefetch(file_size=item['bytes'], max_concurrent_requests=64)
                with partial.open('ab' if partial.exists() else 'xb') as target:
                    while offset < item['bytes']:
                        if cancel.is_set():
                            raise RuntimeError('Download cancelled')
                        block = source.read(min(1024 * 1024, item['bytes'] - offset))
                        assert block, 'Unexpected remote EOF: ' + name
                        target.write(block)
                        offset += len(block)
                        with lock:
                            progress[name] = offset
                    target.flush()
                source.close()
            assert digest(partial) == item['sha256'], 'Part SHA256 mismatch: ' + name
            partial.rename(final)
            with lock:
                completed.add(name)
            return
        except (AssertionError, FileExistsError):
            raise
        except Exception:
            if attempt == 3 or cancel.is_set():
                raise
            time.sleep(attempt * 3)
        finally:
            if client:
                client.close()


try:
    client = connect()
    with client.open_sftp() as sftp:
        previous_phase = None
        while True:
            with sftp.open(REMOTE + '/Status.json', 'rb') as stream:
                remote_status = json.loads(stream.read().decode())
            save_json('RemoteStatus.json', remote_status)
            if remote_status['phase'] != previous_phase:
                print(json.dumps(remote_status), flush=True)
                previous_phase = remote_status['phase']
            assert remote_status['phase'] != 'Failed', remote_status
            if remote_status['phase'] == 'Complete':
                break
            time.sleep(30)
        download_metadata(sftp, 'BackupManifest.json')
        manifest = json.loads((DEST / 'BackupManifest.json').read_text(encoding='utf-8'))
        assert manifest['format'] == 'shootgame-full-build-v1' and manifest['run'] == RUN
        assert sum(item['bytes'] for item in manifest['parts']) == manifest['archive_bytes']
        already = sum((DEST / p['name']).stat().st_size for p in manifest['parts'] if (DEST / p['name']).exists())
        assert shutil.disk_usage(DEST).free >= manifest['archive_bytes'] - already + 10 * 1024 ** 3
        for item in manifest['metadata']:
            download_metadata(sftp, item['name'], item)
        download_metadata(sftp, 'SHA256SUMS')
        download_metadata(sftp, 'Worker.log')
    client.close()
    last_print = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        futures = [pool.submit(download_part, item) for item in manifest['parts']]
        while True:
            if shutil.disk_usage(DEST).free < 10 * 1024 ** 3:
                cancel.set()
                raise RuntimeError('G drive free space below 10 GiB; partial files retained')
            for future in futures:
                if future.done():
                    if future.exception() is not None:
                        cancel.set()
                    future.result()
            with lock:
                value = dict(phase='Downloading', run=RUN, destination=str(DEST),
                             downloaded_bytes=sum(progress.values()), total_bytes=manifest['archive_bytes'],
                             completed_parts=len(completed), total_parts=len(manifest['parts']),
                             elapsed_seconds=round(time.monotonic() - started, 1))
            save_json('DownloadStatus.json', value)
            if time.monotonic() - last_print >= 30:
                print(json.dumps(value), flush=True)
                last_print = time.monotonic()
            if all(future.done() for future in futures):
                break
            time.sleep(5)
    value['phase'] = 'PartsVerified'
    save_json('DownloadStatus.json', value)
    print(json.dumps(value), flush=True)
except BaseException as error:
    cancel.set()
    save_json('DownloadFailure.json', dict(run=RUN, error=str(error)))
    raise
