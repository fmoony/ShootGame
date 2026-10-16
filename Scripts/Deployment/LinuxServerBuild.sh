#!/usr/bin/env bash
# 本轮独立 Linux Server 构建：所有下载、缓存和产物均位于新工作目录。
set -Eeuo pipefail

ROOT=/home/ash-run/shootgame-dedicated-20261014
ENGINE="$ROOT/engine"
PROJECT="$ROOT/project/ShootGame.uproject"
RUN="$ROOT/logs/$(date -u +%Y%m%dT%H%M%SZ)"
PHASE=Prepare
WATCHER=
test "$(id -un)" = ash-run
test "$(id -u)" -ne 0
test -f "$ROOT/InputsReady.json"
mkdir -p "$RUN" "$ROOT/cache/gitdeps" "$ROOT/cache/sdk" "$ROOT/cache/nuget" "$ROOT/tmp"
exec 9>"$ROOT/build.lock"
flock -n 9 || exit 22
exec > >(tee -a "$RUN/Build.log") 2>&1

export XDG_CACHE_HOME="$ROOT/cache/xdg"
export XDG_CONFIG_HOME="$ROOT/cache/config"
export XDG_DATA_HOME="$ROOT/cache/data"
export DOTNET_CLI_HOME="$ROOT/cache/dotnet"
export DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
export NUGET_PACKAGES="$ROOT/cache/nuget"
export NUGET_HTTP_CACHE_PATH="$ROOT/cache/nuget-http"
export TMPDIR="$ROOT/tmp"

state()
{
	python3 -B - "$ROOT" "$RUN" "$PHASE" "$1" "$2" "$$" <<'PY'
import datetime, json, pathlib, sys
root, run, phase, status, code, pid = sys.argv[1:]
record = dict(phase=phase, status=status, exit_code=int(code), pid=int(pid), log_directory=run,
              updated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), parallel_actions=8)
for target in (pathlib.Path(root) / "BuildStatus.json", pathlib.Path(run) / "BuildStatus.json"):
    temporary = target.with_suffix(".tmp")
    temporary.write_text(json.dumps(record, indent=2), encoding="utf-8")
    temporary.replace(target)
print("BUILD_STATE " + json.dumps(record), flush=True)
PY
}

finish()
{
	local result=$?
	trap - EXIT
	if [ -n "$WATCHER" ]; then kill "$WATCHER" 2>/dev/null || true; fi
	if [ "$result" -eq 0 ]; then state Complete 0; else state Failed "$result"; fi
	exit "$result"
}
trap finish EXIT
trap 'exit 143' TERM

state Running 0
python3 -B "$ROOT/scripts/WatchBuildDisk.py" "$ROOT" "$$" &
WATCHER=$!
sleep 1
kill -0 "$WATCHER"

PHASE=VerifySource
state Running 0
python3 -B - "$ENGINE/Engine/Build/Build.version" <<'PY'
import json, sys
version = json.load(open(sys.argv[1]))
assert (version["MajorVersion"], version["MinorVersion"], version["PatchVersion"]) == (5, 6, 1)
assert version["CompatibleChangelist"] == 43139311
print("ENGINE_VERSION_VERIFIED", version, flush=True)
PY

cd "$ENGINE"
PHASE=Dependencies
state Running 0
bash Engine/Build/BatchFiles/Linux/GitDependencies.sh --force --threads=4 \
	--cache="$ROOT/cache/gitdeps" --cache-size-multiplier=1 --cache-days=2 \
	--exclude=Win64 --exclude=Mac --exclude=Android --exclude=IOS --exclude=TVOS --exclude=HoloLens

PHASE=Toolchain
state Running 0
GIT_DIR="$ROOT/cache/sdk" bash Engine/Build/BatchFiles/Linux/SetupToolchain.sh
export LINUX_MULTIARCH_ROOT="$ENGINE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/"\
"v25_clang-18.1.0-rockylinux8"
test -x "$LINUX_MULTIARCH_ROOT/x86_64-unknown-linux-gnu/bin/clang++"
"$LINUX_MULTIARCH_ROOT/x86_64-unknown-linux-gnu/bin/clang++" --version

PHASE=UBTBootstrap
state Running 0
bash Engine/Build/BatchFiles/BuildUBT.sh quiet
test -f "$ENGINE/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
test -f "$ENGINE/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.deps.json"
test -f "$ENGINE/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.runtimeconfig.json"
printf 'UBT_BOOTSTRAP_SUCCESS\n'

PHASE=ServerBuild
state Running 0
bash Engine/Build/BatchFiles/RunUBT.sh ShootGameServer Linux Development \
	"-Project=$PROJECT" -SkipUBTBuild -MaxParallelActions=8 -NoUBA -NoHotReload \
	"-Log=$RUN/UnrealBuildTool.log"
test -x "$ROOT/project/Binaries/Linux/ShootGameServer"
test -f "$ROOT/project/Binaries/Linux/ShootGameServer.target"
printf 'SERVER_TARGET_BUILD_SUCCESS\n'
du -sh "$ROOT/project/Binaries/Linux" "$ENGINE/Engine/Intermediate" "$ROOT/cache"
df -h /
