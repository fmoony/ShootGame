#!/usr/bin/env bash
# 只启动本轮独立构建；已有任务运行时不重复启动。
set -euo pipefail
ROOT=/home/ash-run/shootgame-dedicated-20261014
UNIT=shootgame-build-20261014.service
test "$(id -u)" -eq 0
test -f "$ROOT/InputsReady.json"
test -f "$ROOT/scripts/LinuxServerBuild.sh"

LOAD=$(systemctl show "$UNIT" -p LoadState --value 2>/dev/null || true)
if [ "$LOAD" = loaded ]; then
	test "$(systemctl show "$UNIT" -p User --value)" = ash-run
	test "$(systemctl show "$UNIT" -p WorkingDirectory --value)" = "$ROOT/engine"
	if systemctl is-active --quiet "$UNIT"; then
		printf 'BUILD_ALREADY_RUNNING\n'
		exit 0
	fi
	systemctl reset-failed "$UNIT"
	LOAD=$(systemctl show "$UNIT" -p LoadState --value 2>/dev/null || true)
fi
if [ "$LOAD" = loaded ]; then
	systemctl set-property --runtime "$UNIT" CPUQuota=800%
	systemctl start "$UNIT"
else
	systemd-run --unit="$UNIT" --service-type=exec \
		--description='ShootGame UE5.6.1 isolated native Server build' \
		--property=User=ash-run --property=Group=ash-run --property=WorkingDirectory="$ROOT/engine" \
		--property=CPUQuota=800% --property=MemoryHigh=20G --property=MemoryMax=24G \
		--property=MemorySwapMax=4G --property=Nice=10 --property=IOSchedulingClass=best-effort \
		--property=IOSchedulingPriority=7 --property=KillMode=control-group --property=OOMPolicy=stop \
		--property=Restart=no --property=TimeoutStopSec=60s \
		/usr/bin/setsid --wait /bin/bash "$ROOT/scripts/LinuxServerBuild.sh"
fi
systemctl show "$UNIT" -p ActiveState -p SubState -p MainPID -p User -p MemoryMax -p CPUQuotaPerSecUSec
