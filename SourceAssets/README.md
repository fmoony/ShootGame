# SourceAssets 当前保留范围

维护日期：2026-11-22。此目录保存长期制作输入与来源说明。
当前下蹲移动采用已验收的 0.3 秒八向版本，最终序列在 Content 中。

## 当前下蹲制作输入

FourDirectionRootLockSource_20261117 完整保留 18 项：

- Original：四份原始 FBX。
- ImportReady：四份父层运动正确烘焙的 FBX、四份 Blender 文件与对照数据。
- Sources.json、prepare_import.py、import_ue.py 及原始说明。

当前前、左、右与后退躯干派生的导入记录指向其 ImportReady 四份 FBX。
后退躯干派生还包含 UE 内局部姿态修改，不能靠重导 Back.fbx 原样恢复。
其余四段斜向是 UE 内烘焙或派生结果，没有对应的外部 FBX 导入记录。
最终资产、处理过程与原生数据见[实验当前入口][stance]。

import_ue.py 保留的是首次导入流程，含当时的资产路径。
它不是当前最终环境的一键重导入口，本轮没有执行或更新它。
prepare_import.py 及其共享工具和原始输入均保持。

## 保留的其他来源

- Audio：脚步声音源、授权与说明，原样保留。
- FourDirectionCrouch_20261117/Original：较早一批四方向原始 FBX。
- StanceCrouch 根目录：最早五份原始 FBX 与 Manifest.json。
- 各旧方案 README：历史制作说明，保留原文。

## 已停止保留的派生材料

已清理旧接地 92 段、StanceAware 重制、早期八向与镜像等候选。
旧 README 中提到的派生文件、旧路径及命令只描述当时流程。
Scripts 中的旧试作入口未在本轮改写，也不属于当前可重放入口。
这些旧方案的源材料已备份，不能将保留 README 理解为旧流程仍可直接运行。

清理后原有保留文件 62 项，加本说明共 63 项，约 23.08 MiB。
其中 Audio 为 21 项，原有下蹲输入与说明为 41 项。
[清理清单、备份位置与保护检查][cleanup]。

[stance]: ../Docs/验证/Stance_20261022/README.md
[cleanup]: ../Docs/验证/Stance_20261022/SourceAssetsCleanup_20261122/Report.md
