# 左前、右后保留来源上身朝向

输入是 BlendMirrorEight_20261117/Expected.json 中的原生混合与镜像数据。
使用已有 Blender 坐标桥，按原时间采样导出两个 FBX 与 Blender 文件。
bake_keep_upper_heading.py 记录骨骼范围和腰背过渡。
import_ue.py 将两个序列导入隔离实验目录，检查逐帧姿态后指定保存。

[运行记录][report]

[report]: ../../Docs/验证/Stance_20261022/左前右后保留来源上身朝向_20261118.md
