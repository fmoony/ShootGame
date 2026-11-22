# 原生混合烘焙与镜像八向输入

本目录只保留当前四方向实验已验收结果的派生来源。
前后左右原件位于 FourDirectionRootLockSource_20261117，本目录不改它们。

- NativeBlendPoses.json：UE 原生求值采集的右前与左后，60 FPS、61 点、89 骨骼。
- ForwardRight／BackLeft：上述采集结果的忠实 blend／FBX 烘焙。
- ForwardLeft：右前逐帧左右镜像。
- BackRight：左后逐帧左右镜像。
- Expected.json：四段目标姿态；Sources.json 与 BakeSummary.json 记录来源与检查。
- bake_blends_mirrors.py：从稳定采集输入重放，默认输出到 Saved/BlendMirrorEight_Replay。
- import_ue.py：首次导入流程，拒绝覆盖已有 UE 序列。

没有重制步态、相位偏移、IK、接地补偿或骨盆／步幅统一。
四段均保持一秒；Root Motion 提取与 Force Root Lock 均关闭。
重放输出目录必须不存在，不会自动覆盖本目录或修改当前 UE 资产。

[实验结果与邻向限制](../../Docs/验证/Stance_20261022/原生混合烘焙与镜像八向实验_20261117.md)。
