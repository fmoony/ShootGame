#!/usr/bin/env bash
# 启动本轮打包产物；root 只负责创建 unit，游戏进程以 ash-run 运行。
set -euo pipefail
ROOT=/home/ash-run/shootgame-dedicated-20261014
RUNNAME=${1:-package-20261014T142052Z}
[[ "$RUNNAME" =~ ^package-[0-9]{8}T[0-9]{6}Z$ ]]
PACKAGE="$ROOT/packages/$RUNNAME/LinuxServer"
UNIT=shootgame-server-20261014.service
LOG="$ROOT/logs/server-$(date -u +%Y%m%dT%H%M%SZ)"
test "$(id -u)" -eq 0
test -x "$PACKAGE/ShootGame/Binaries/Linux/ShootGameServer"
test -f "$PACKAGE/ShootGame/Content/Paks/ShootGame-LinuxServer.utoc"
if systemctl is-active --quiet "$UNIT"; then
	printf 'SERVER_ALREADY_RUNNING\n'
	exit 0
fi
if [[ "$(systemctl show "$UNIT" -p LoadState --value)" != not-found ]]; then
	test "$(systemctl show "$UNIT" -p User --value)" = ash-run
	OLDPACKAGE=$(systemctl show "$UNIT" -p WorkingDirectory --value)
	[[ "$OLDPACKAGE" == "$ROOT/packages/"*/LinuxServer ]]
	systemctl reset-failed "$UNIT"
fi
test -z "$(ss -H -lun 'sport = :7777')"
install -d -o ash-run -g ash-run -m 0750 "$LOG"
systemd-run --unit="$UNIT" --service-type=exec \
	--property=User=ash-run --property=Group=ash-run --property=WorkingDirectory="$PACKAGE" \
	--property=CPUQuota=200% --property=MemoryHigh=4G --property=MemoryMax=6G \
	--property=MemorySwapMax=1G --property=KillMode=control-group --property=OOMPolicy=stop \
	--property=Restart=no --property=TimeoutStopSec=60s \
	"$PACKAGE/ShootGame/Binaries/Linux/ShootGameServer" /Game/Shooter/Maps/Lvl_Test \
	-Port=7777 -log -unattended -nosound "-AbsLog=$LOG/Server.log"
printf 'SERVER_LOG_DIRECTORY=%s\n' "$LOG"
systemctl show "$UNIT" -p ActiveState -p SubState -p MainPID -p User -p WorkingDirectory
