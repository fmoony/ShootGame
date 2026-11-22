# 下蹲移动最终实验

当前基线：2026-11-22 人工接受的八向下蹲移动，方向平滑为 0.3 秒。
本说明是本次精简提交的当前入口；历史过程文件仍保留在原工作区。

## 进入实验

在 UE 5.8.4 打开以下地图并进入 PIE：

/Game/Experiments/Stance_20261022/L_Smoothing03_Crouch

WASD 控制实际移动，组合键为斜向；松键停止移动和动画播放。
V 切换已有第三人称观察视角，移动速度为 150 cm/s。
本环境仅为隔离演示，不使用正式 CharacterMovement 或网络预测。

地图使用 BP_Smoothing03GameMode，启动即指定 ABP_Smoothing03_Crouch。
C++ 实验 GameMode 的默认 AnimClass 同样指向该类。
预览场景使用 BP_BakedEightPreview，Skeleton 为 SK_FourDirection。

## 动画数据流

Pawn 的 PreviewForward／PreviewRight → AnimBP 变量 → BlendSpace Player → Output。
混合资源为 BS_Smoothing03_Crouch，两轴 Smoothing Time=0.3 秒。
保留 SpringDamper、DampingRatio=1、WeightSpeed=0 和 PlayRate=1。
无同步组或 SyncMarker，不应将共同归一化时间称为支撑相位已对齐。
没有实时 Orientation Warping、持枪分层、IK 或 C++ 骨骼补偿。

八段序列与样本如下，名称均位于上述实验根目录：

| 方向 | Forward | Right | 动画 |
| --- | ---: | ---: | --- |
| 前 | 1 | 0 | AS_RootLockSource_Forward |
| 右前 | 0.5 | 0.5 | AS_BlendBaked_ForwardRight |
| 右 | 0 | 1 | AS_RootLockSource_Right |
| 右后 | −0.5 | 0.5 | AS_BackRight_TorsoReference |
| 后 | −1 | 0 | AS_Back_TorsoReference |
| 左后 | −0.5 | −0.5 | AS_BackLeft_TorsoReference |
| 左 | 0 | −1 | AS_RootLockSource_Left |
| 左前 | 0.5 | −0.5 | AS_OWBaked_FrontLeft |

地图、GameMode、预览蓝图、AnimBP、BlendSpace、Skeleton 和八段序列共 14 项。
每段循环为 1 秒，Root Motion 提取和 Force Root Lock 均关闭。

## 制作来源与维护

当前四方向来源保留于 SourceAssets/FourDirectionRootLockSource_20261117。
其中 Original、ImportReady、Blender 工程、对照数据与来源说明保持。
四段斜向在 UE 内烘焙或派生，后退等方向另含离线躯干旋转调整。
后退派生虽记录 Back.fbx 来源，直接重导该 FBX 不能恢复其最终躯干修改。
制作包中的 import_ue.py 是历史首次导入流程，不是当前的一键重导入口。

原件及清理后的源材料边界见[SourceAssets 当前说明][sources]。
保留工具依赖项目已有 RifleTools 和 fbx_parent_motion.py。
没有将已退役的 StanceAware、镜像和手工步态流程作为本版本的生成入口。

## 验收与已知限制

用户已确认本版本作为当前下蹲移动最终版本，见[人工验收记录][acceptance]。
0.3 秒减缓部分转向上移峰速，但中间上拱与脚部下沉仍存在。
姿态接近目标比 0.2 秒增加约 67～167 ms；这些取舍随最终版本接受。
未验收下蹲 Idle、武器行为、第一人称、生产 Gameplay、坡面或多人网络。
不将数值通过等同于这些场景或整体动画自然度通过。

- [0.2／0.3 秒同步采样与画面对照][smoothing]。
- [最终环境整理、14 项保留资源与八向回归][environment]。
- [SourceAssets 清理、原始输入保护和导入来源检查][cleanup]。

历史 0.2 秒、实时 OW 和旧候选资产已删除，不作为当前可进入的地图。
历史记录已按主题归档，保留关键证据；非关键附件和退役脚本已精简。
[历史主题、证据范围与恢复说明](历史归档_20261122/README.md)。
上述报告包含当时的本机备份位置和保护清单，供解释过程，不是运行依赖。

[sources]: ../../../SourceAssets/README.md
[acceptance]: 下蹲移动最终版本验收_20261122.md
[smoothing]: Smoothing03Compare_20261122/Report.md
[environment]: FinalEnvironmentCleanup_20261122/Report.md
[cleanup]: SourceAssetsCleanup_20261122/Report.md
