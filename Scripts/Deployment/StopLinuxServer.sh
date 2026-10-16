#!/usr/bin/env bash
# 仅停止本轮普通用户专服；保留包、日志与实验资源。
set -euo pipefail
ROOT=/home/ash-run/shootgame-dedicated-20261014
UNIT=shootgame-server-20261014.service
test "$(id -u)" -eq 0
if [[ "$(systemctl show "$UNIT" -p LoadState --value)" = not-found ]]; then
	printf 'SERVER_ALREADY_STOPPED\n'
	exit 0
fi
test "$(systemctl show "$UNIT" -p User --value)" = ash-run
PACKAGE=$(systemctl show "$UNIT" -p WorkingDirectory --value)
[[ "$PACKAGE" == "$ROOT/packages/"*/LinuxServer ]]
systemctl stop "$UNIT"
if [[ "$(systemctl show "$UNIT" -p LoadState --value)" != not-found ]]; then
	systemctl reset-failed "$UNIT"
fi
printf 'SERVER_STOPPED\n'
