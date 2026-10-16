"""SSH 密码仅保留在内存中；文件传输限于本轮独立部署目录。"""

import base64
import getpass
import hashlib
import json
import posixpath
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
ARTIFACT_ROOT = PROJECT_ROOT / "Saved/Deployment/20261014"
REMOTE_ROOT = "/home/ash-run/shootgame-dedicated-20261014"
HOST_FINGERPRINT = "mGxANJb67onJOPWFflgQZ22pumaFmo1tPf+VlmmcFzA"
sys.path.insert(0, str(ARTIFACT_ROOT / "PythonPackages"))
import paramiko


class PinnedServerKey(paramiko.MissingHostKeyPolicy):
    def missing_host_key(self, client, hostname, key):
        actual = base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
        if actual != HOST_FINGERPRINT:
            raise paramiko.SSHException("服务器主机密钥与本轮已核对的指纹不同。")


def remote_path(value):
    result = posixpath.normpath(value)
    if not result.startswith(REMOTE_ROOT + "/"):
        raise ValueError("传输路径必须位于本轮独立工作目录。")
    return result


def local_path(value):
    result = Path(value).resolve()
    if not result.is_relative_to(PROJECT_ROOT):
        raise ValueError("本地传输路径必须位于 ShootGame 工作区。")
    return result


def execute(client, command):
    with (ARTIFACT_ROOT / "RemoteCommands.log").open("a", encoding="utf-8") as log:
        log.write("\nCOMMAND: " + command + "\n")
        channel = client.get_transport().open_session()
        channel.exec_command(command)
        while not channel.exit_status_ready() or channel.recv_ready() or channel.recv_stderr_ready():
            for ready, receive in (
                (channel.recv_ready, channel.recv),
                (channel.recv_stderr_ready, channel.recv_stderr),
            ):
                if ready():
                    output = receive(32768).decode("utf-8", errors="replace")
                    print(output, end="", flush=True)
                    log.write(output)
                    log.flush()
            time.sleep(0.05)
        status = channel.recv_exit_status()
        channel.close()
        print(json.dumps({"exit_code": status}), flush=True)


def main():
    password = getpass.getpass("SSH password (not saved): ")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(PinnedServerKey())
    client.connect("103.236.77.127", username="root", password=password,
                   look_for_keys=False, allow_agent=False, timeout=15, auth_timeout=20)
    del password
    transport = client.get_transport()
    transport.set_keepalive(20)
    fingerprint = base64.b64encode(hashlib.sha256(transport.get_remote_server_key().asbytes()).digest())
    print("CONNECTED SHA256:" + fingerprint.decode().rstrip("="), flush=True)
    if len(sys.argv) == 2 and sys.argv[1] == "--status":
        command_path = Path(__file__).with_name("ReadLinuxBuildStatus.sh")
        try:
            execute(client, command_path.read_text(encoding="utf-8"))
        finally:
            client.close()
        return
    print("READY: one JSON request per line; actions run/upload/download/exit", flush=True)
    try:
        for line in sys.stdin:
            try:
                request = json.loads(line)
                action = request["action"]
                if action == "exit":
                    break
                if action == "run":
                    execute(client, request["command"])
                elif action in ("upload", "download"):
                    source = local_path(request["local"])
                    target = remote_path(request["remote"])
                    with client.open_sftp() as sftp:
                        if action == "upload":
                            if request.get("shell_text"):
                                content = source.read_bytes().replace(b"\r\n", b"\n")
                                with sftp.open(target, "wb") as stream:
                                    stream.write(content)
                            else:
                                last_update = [0.0]

                                def progress(done, total):
                                    if time.monotonic() - last_update[0] >= 10 or done == total:
                                        print(json.dumps({"sent_bytes": done, "total_bytes": total}), flush=True)
                                        last_update[0] = time.monotonic()

                                sftp.put(str(source), target, callback=progress)
                        else:
                            if not source.is_relative_to(ARTIFACT_ROOT.resolve()):
                                raise ValueError("备份只能写入本轮 Saved/Deployment 目录。")
                            if source.exists():
                                raise FileExistsError("备份文件已存在；请使用新的时间戳文件名。")
                            source.parent.mkdir(parents=True, exist_ok=True)
                            sftp.get(target, str(source))
                    print(json.dumps({"action": action, "complete": True}), flush=True)
                else:
                    raise ValueError("未知 action。")
            except Exception as error:
                print(json.dumps({"error_type": type(error).__name__, "message": str(error)}), flush=True)
            print("READY", flush=True)
    finally:
        client.close()


if __name__ == "__main__":
    main()
