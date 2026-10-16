#!/usr/bin/env bash
# 使用已有 Linux Server 二进制和 Windows Editor 生成的 LinuxServer Cook 内容打包。
set -Eeuo pipefail
ROOT=/home/ash-run/shootgame-dedicated-20261014
ENGINE="$ROOT/engine"
PROJECT="$ROOT/project/ShootGame.uproject"
RUNNAME="package-$(date -u +%Y%m%dT%H%M%SZ)"
RUN="$ROOT/logs/$RUNNAME"
test "$(id -un)" = ash-run
test -x "$ROOT/project/Binaries/Linux/ShootGameServer"
test -d "$ROOT/project/Saved/Cooked/LinuxServer"
mkdir -p "$RUN" "$ROOT/packages"
exec 9>"$ROOT/package.lock"
flock -n 9 || exit 22
exec > >(tee -a "$RUN/Package.log") 2>&1
export XDG_CACHE_HOME="$ROOT/cache/xdg"
export XDG_CONFIG_HOME="$ROOT/cache/config"
export XDG_DATA_HOME="$ROOT/cache/data"
export DOTNET_CLI_HOME="$ROOT/cache/dotnet"
export DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
export NUGET_PACKAGES="$ROOT/cache/nuget"
export NUGET_HTTP_CACHE_PATH="$ROOT/cache/nuget-http"
export TMPDIR="$ROOT/tmp"
export uebp_LogFolder="$RUN/AutomationTool"
export LINUX_MULTIARCH_ROOT="$ENGINE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/"\
"v25_clang-18.1.0-rockylinux8"
finish()
{
	local result=$?
	trap - EXIT
	if [ -n "${WATCHER:-}" ]; then kill "$WATCHER" 2>/dev/null || true; fi
	printf 'PACKAGE_EXIT_CODE=%s\n' "$result"
	exit "$result"
}
trap finish EXIT
trap 'exit 143' TERM
python3 -B "$ROOT/scripts/WatchBuildDisk.py" "$ROOT" "$$" &
WATCHER=$!
sleep 1
kill -0 "$WATCHER"
cd "$ENGINE"
bash Engine/Build/BatchFiles/RunUBT.sh UnrealPak Linux Development \
	-SkipUBTBuild -MaxParallelActions=8 -NoUBA -NoHotReload "-Log=$RUN/UnrealPakBuild.log"
test -x "$ENGINE/Engine/Binaries/Linux/UnrealPak"
bash Engine/Build/BatchFiles/RunUAT.sh BuildCookRun \
	"-project=$PROJECT" -noP4 -utf8output -unattended -skipbuild -skipcook -nocompileeditor \
	-server -noclient -serverplatform=Linux -serverconfig=Development -servertarget=ShootGameServer \
	-stage -pak -package -archive "-archivedirectory=$ROOT/packages/$RUNNAME" \
	"-stagingdirectory=$ROOT/stage/$RUNNAME"
printf 'LINUX_SERVER_PACKAGE_SUCCESS\n'
