# Stance-Aware 制作输入

本目录为 2026-11-16 新制作的实验派生，不是未修改的 Source。
当前导入目录为 StanceAware_20261116/Refined，稳定 FBX／blend 位于 BakedV2。
Baked 保留较早候选，当前运行不引用它。

## 输入与处理

- CommonStance.json：从既有前向来源取得的半蹲支撑基准。
- ReferencePoses.json：生产 Stand ADS／Compact LowReady 的原生压缩姿态。
- RuntimeAimReference.json：真实 Stand 图在五个俯仰角下的最终运行骨骼。
- AimReference.json：早期静态端点对照，未作为最终 Crouch AO 的制作目标。

原始 FBX、SourceLayer、EightWay、Mirrored 及其历史验证均保留。
本轮统一足间距、支持时序和步幅，以 Blender 原生控制链 IK 离线烘焙。
根骨没有附加高度偏移，当前实验不执行运行时脚部 IK 或 C++ 姿态计算。

## 当前资源

- 八段 1 秒蹲走：共享 65% 支撑段、左右半周期错相，支撑速度 45 cm/s。
- 半蹲 Idle：固定支撑姿态，不再使用单膝跪姿；尚未补呼吸和重心变化。
- 九段站蹲中间姿态：用于原生 BS_StanceHeight 的 CrouchAlpha 取样。
- Crouch ADS／LowReady：保持腰背基准，以原生 Blender 手臂 IK 校准持枪。
- 五段 Crouch AO：明确使用 Crouch ADS 第 0 帧作 Mesh Space Additive 参考。
- 所有 FBX 保留完整姿态；Additive 类型与参考在 UE 导入时设置并另行验证。

真实运行参考来自 RuntimeReference90.csv 的指定场景和时刻。
文件的 SHA256、场景、时间与完整骨骼已嵌入 RuntimeAimReference.json。
没有将静态资源还原或导入成功等同于实际图中的握持通过。

## 工具

Scripts/Animation/StanceAware 下：

- author_contacts.py：在 Saved 新目录制作支撑步态与站蹲采样。
- prepare_weapon_poses.py：制作两个绝对持枪基准。
- author_aim_poses.py：从实际运行参考制作五个下蹲俯仰姿态。
- bake_assets.py：在新目录写入 Blender 文件与标准 FBX，拒绝覆盖输出。
- import_assets.py：限定实验目录导入；自身候选重导入需明确参数及源路径匹配。
- verify_assets.py：磁盘重载、原始／压缩姿态、循环、原生图与保护回归。
- check_runtime.py：实际骨骼、鞋部蒙皮、双脚相交及支撑期滑动测量。
- Run.ps1：Preview、Capture、Verify；Baseline 显式选择保留的原实验。

制作依赖现有后台 bpy 4.5.8 环境，未增加运行时动画算法。
完整结果与边界见[本轮报告](../../../Docs/验证/StanceAware_20261116/README.md)。
