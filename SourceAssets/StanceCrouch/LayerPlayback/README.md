# 新来源下半身播放版本

由 ParentBaked 的五份 blend 派生，供现有实验分层作为下半身输入。
完整 SourceBaked、原始 Source 与旧 92 段步态资源保持不变。

只在 Blender 中移除移动片段每循环的净水平行进。
保留 root 的原始高度、偏航和周期摆动，不逐片段重置朝向。
非 root 局部轨道与完整源版本保持一致，差异仅为导出精度。
idle 不移除水平漂移，保留原始单膝跪姿。
没有新增脚部接地、腿部 IK、鞋底校平或步态自然度制作。

CrouchForwardRight 由 CrouchForwardLeft 左右骨骼交换并按参考姿态镜像。
镜像后错开半周期，维持左右脚的交替关系。
其他方向由新 BlendSpace 的五个移动样本混合，不另造原生来源。

六份 FBX 通过标准 FbxFactory 导入，直接绑定生产 SK_Mannequin。
Expected.json 只用于独立核对，不负责在 UE 写入动画轨道。
BakeSummary.json 记录来源、相位、移除的净行进量及 Blender 求值误差。

## 资源与运行

资产目录：/Game/Experiments/Stance_20261022/SourceLayer。
序列前缀 AS_SourceLayer_；BlendSpace 为 BS_SourceLayer_Lower。
Aim / LowReady 共用下半身输入，沿用原站立持枪上半身分层。
Pawn 速度由完整源的行进量与当前混合权重匹配，保持输入方向。
这只调整 Actor 移动，不修改骨骼或执行地面检测。

默认实验使用本版本；-StanceLegacyLower 可切回旧步态作对照。
Run.ps1 的 Preview / Verify 默认选择新版本，-LegacyLower 选择旧版本。
原 ApplyResources 仍只更新旧 92 段，不覆盖此目录。

## 制作入口

Scripts/Animation/Stance/bake_layer_source.py 生成新的 Saved 候选目录。
--output-dir 只能指定项目 Saved 内尚不存在的目录。
使用项目已有 BlenderPython458 环境，实际加载 ParentBaked blend 并重新求值。
重建全局矩阵时必须保留原始骨架尺度，不能用单位尺度替代。
否则关闭重定向时可能看似正常，启用生产骨架重定向后会放大骨长。

import_layer_source.py 默认 Probe，不保存生产资产。
-SourceLayerCommit 才保存六段新动画，目标目录存在时拒绝覆盖。
实验 Commandlet 的 -SourceLayerSpace 只创建新的 BlendSpace，也拒绝覆盖。
修改和导入须遵守编辑器会话管理，未退出的编辑器不能与离线保存并行。

[本轮实际运行与边界](../../../Docs/验证/StanceSourceLayer_20261115/README.md)。
