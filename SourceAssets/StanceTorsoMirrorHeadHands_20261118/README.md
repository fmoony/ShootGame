# 躯干镜像并保留头手朝向

沿用 BlendMirrorEight_20261117/Expected.json 中原生混合和全身镜像的数据。
骨盆、胸与腿用镜像变换。头与左右手在角色空间采用各自的来源旋转。
颈部、锁骨和手臂逐节过渡，手指采用来源局部姿态。

bake_torso_mirror_head_hands.py 使用既有坐标桥输出两个 FBX 与 Blender 文件。
import_ue.py 导入两个新序列，逐帧验证后指定保存。

[运行记录][report]

[report]: ../../Docs/验证/Stance_20261022/躯干镜像并保留头手朝向_20261118.md
