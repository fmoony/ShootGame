# rootLock 来源四方向输入

本目录保留 2026-11-17 用户提供的四份新原地移动动作。
来源目录：D:/GameResource/Model/UETemplate/Animation/Crouch/rootLock。

- Original：四份字节相同的原始 FBX，路径与 SHA 见 Sources.json。
- ImportReady：完整父层运动烘焙后的导入副本、四份 blend 和对照数据。
- prepare_import.py：既有父层烘焙工具的本次来源适配。
- import_ue.py：仅导入到新的实验路径并验证原始姿态，拒绝覆盖已有资产。

没有去除轨迹、制作步态、统一身体基准、镜像、相位对齐或额外锁定 Root。
新序列 Force Root Lock=false，Enable Root Motion=false。
FBX 的原地行进特征与身体摆动均来自输入原件。

prepare_import.py 默认输出至 Saved/FourDirectionRootLockSource_Replay/Candidate。
输出目录必须不存在；重放不会自动覆盖当前 ImportReady 或 UE 资产。
import_ue.py 是首次导入流程，当前资产已存在，不应重复运行。
当前导入源路径指向稳定 ImportReady，未指向临时 Saved。

[测试与证据](../../Docs/验证/Stance_20261022/四方向rootLock来源重导入测试_20261117.md)
