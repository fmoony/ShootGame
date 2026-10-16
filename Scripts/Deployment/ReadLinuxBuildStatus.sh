#!/usr/bin/env bash
# 只读查看本轮构建状态；不重启任务、不修改文件。
set -u
ROOT=/home/ash-run/shootgame-dedicated-20261014
printf '[UNIT]\n'
systemctl show shootgame-build-20261014.service \
	-p ActiveState -p SubState -p Result -p MainPID -p ExecMainStatus \
	-p User -p MemoryCurrent -p MemoryPeak -p MemoryMax -p MemoryHigh \
	-p MemorySwapCurrent -p MemorySwapMax -p CPUQuotaPerSecUSec
CGROUP=$(systemctl show shootgame-build-20261014.service -p ControlGroup --value)
if [ -n "$CGROUP" ] && [ -r "/sys/fs/cgroup$CGROUP/memory.events" ]; then
	printf '[BUILD_MEMORY_EVENTS]\n'
	cat "/sys/fs/cgroup$CGROUP/memory.events"
fi
printf '[STATE_AND_RECENT_LOG]\n'
python3 -B - "$ROOT" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
state_path = root / "BuildStatus.json"
if not state_path.exists():
    print("BuildStatus.json does not exist yet")
    raise SystemExit(0)
state = json.loads(state_path.read_text())
print(json.dumps(state, indent=2))
directory = pathlib.Path(state["log_directory"]).resolve()
assert directory.is_relative_to(root / "logs")
for name in ("Build.log", "UnrealBuildTool.log"):
    path = directory / name
    if path.exists():
        with path.open("rb") as stream:
            stream.seek(max(0, path.stat().st_size - 12000))
            lines = stream.read().decode("utf-8", errors="replace").splitlines()
        print("[" + str(path) + "]")
        print("\n".join(lines[-25:]))
guard = root / "DiskGuardTriggered.json"
if guard.exists():
    print("DISK_GUARD_TRIGGERED", guard.read_text())
for name in ("ShootGameServer", "ShootGameServer.target"):
    artifact = root / "project/Binaries/Linux" / name
    print("ARTIFACT", name, "exists=" + str(artifact.exists()))
PY
printf '[DISK_MEMORY]\n'
df -h "$ROOT"
free -h
