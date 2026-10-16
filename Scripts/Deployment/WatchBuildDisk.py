"""仅监测本轮构建进程组；磁盘余量不足时终止该组，保护已有实验。"""

import json
import os
import shutil
import signal
import sys
import time
from pathlib import Path

root = Path(sys.argv[1]).resolve()
process_group = int(sys.argv[2])
minimum_free = 80 * 1024 ** 3
if str(root) != "/home/ash-run/shootgame-dedicated-20261014":
    raise SystemExit("Unexpected workspace")
if os.getpgid(process_group) != process_group:
    raise SystemExit("Build must run in an isolated process group")

iteration = 0
while True:
    try:
        os.kill(process_group, 0)
    except ProcessLookupError:
        break
    free = shutil.disk_usage(root).free
    if iteration % 4 == 0:
        print(f"DISK_GUARD FreeGiB={free / 1024 ** 3:.2f} MinimumGiB=80", flush=True)
    if free < minimum_free:
        (root / "DiskGuardTriggered.json").write_text(
            json.dumps({"free_bytes": free, "minimum_bytes": minimum_free, "process_group": process_group}),
            encoding="utf-8",
        )
        os.killpg(process_group, signal.SIGTERM)
        break
    iteration += 1
    time.sleep(15)
