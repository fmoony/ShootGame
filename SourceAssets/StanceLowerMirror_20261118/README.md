# 左前、右后的下半身镜像

源数据为 BlendMirrorEight_20261117/Expected.json。
该文件包含右前、左后原拍摄结果与左右镜像后的完整逐帧组件变换。
本轮取镜像后的 Root、骨盆与下半身，将原来的上身局部骨骼接回到新骨盆。

- bake_lower_mirror.py 复用项目 Blender 坐标桥，导出两个 FBX 和两个 blend。
- Expected.json 是输出资产逐帧的目标骨骼。
- BakeSummary.json 记录保留范围、局部姿态差值和烘焙误差。
- import_ue.py 在指定实验目录导入两个新序列，并逐帧核对后指定保存。

完整运行证据在 [实验记录][report]。

[report]: ../../Docs/验证/Stance_20261022/恢复方向混合与下半身镜像_20261118.md
