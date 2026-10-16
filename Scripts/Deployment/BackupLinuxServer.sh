#!/usr/bin/env bash
# 只备份本轮产物，不清理服务器内容；下载后须核对 SHA256。
set -euo pipefail
ROOT=/home/ash-run/shootgame-dedicated-20261014
RUNNAME=${1:-package-20261014T142052Z}
[[ "$RUNNAME" =~ ^package-[0-9]{8}T[0-9]{6}Z$ ]]
test "$(id -un)" = ash-run
cd "$ROOT"
PACKAGE="$ROOT/packages/$RUNNAME"
test -x "$PACKAGE/LinuxServer/ShootGame/Binaries/Linux/ShootGameServer"
BACKUP="$ROOT/backups/release-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$BACKUP/BackupMetadata"
cp "$ROOT/project/Binaries/Linux/ShootGameServer.target" "$BACKUP/BackupMetadata/"
cp "$ROOT/engine/Engine/Build/Build.version" "$BACKUP/BackupMetadata/"
cp "$ROOT/InputsReady.json" "$ROOT/BuildStatus.json" "$BACKUP/BackupMetadata/"
cp -r "$ROOT/scripts" "$BACKUP/BackupMetadata/"
systemctl show shootgame-server-20261014.service -p ActiveState -p SubState -p MainPID \
	-p User -p WorkingDirectory > "$BACKUP/BackupMetadata/ServerUnit.txt"
ss -H -lun 'sport = :7777' > "$BACKUP/BackupMetadata/UDP7777.txt"
SERVERLOG=$(find "$ROOT/logs" -maxdepth 2 -path '*/server-*/Server.log' -type f | sort | tail -n 1)
test -n "$SERVERLOG"
cp "$SERVERLOG" "$BACKUP/BackupMetadata/Server.log"
printf 'PACKAGE=%s\nSERVER_LOG=%s\nMAP=/Game/Shooter/Maps/Lvl_Test\n' \
	"$PACKAGE" "$SERVERLOG" > "$BACKUP/BackupMetadata/Provenance.txt"
# 调试符号已有首轮备份；此归档保存新的完整可运行包及其版本、日志、脚本。
tar -I 'gzip -1' --exclude='*.debug' --exclude='*.sym' -cf "$BACKUP/LinuxServer.tar.gz" \
	-C "$PACKAGE" LinuxServer -C "$BACKUP" BackupMetadata
sha256sum "$BACKUP/LinuxServer.tar.gz" | tee "$BACKUP/SHA256.txt"
printf 'BACKUP_DIRECTORY=%s\n' "$BACKUP"
