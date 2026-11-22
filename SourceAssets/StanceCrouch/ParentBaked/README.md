# 原始动作父层烘焙版本

本目录由五份原始 Source FBX 派生，保留各自完整动作。
每段同时保留可编辑 blend、标准动画 FBX；原始 Source 文件不覆盖。
默认参考坐标归一化后，将 Armature 对象运动逐帧烘焙到真实 root。
保留原始初始 XY、朝向和水平净位移，因此预览中的角色可能离开原点。

本版本没有重新制作腿部、接地、握持、节奏或循环。
Blender 4.5.8 LTS 实际求值导出，30 fps，共 339 帧、89 根源骨骼。
静态容器命名为 Armature，UE 的 Blender FBX 导入逻辑跳过该容器。
最终动画直接绑定生产 SK_Mannequin，预览使用 SKM_Manny_Simple。

Expected.json 是从原始 FBX 独立计算的姿态对照数据。
UE 动画内容通过 FbxFactory 从 FBX 导入，Expected 不负责写入轨道。
BakeSummary.json 记录 Blender 导入与烘焙前后的矩阵误差。

## 工具入口

工具位于 Scripts/Animation/Stance：

- bake_source_parent.py：读取原始 FBX，输出新的 Saved 候选目录。
- import_baked_source.py：标准 FBX 导入与全帧验证。
- verify_baked_source.py：从磁盘重新加载，检查 RAW 和 COMPRESSED 姿态。

烘焙使用项目已有 BlenderPython458 环境，--output-dir 必须指定新目录。
默认输入为本目录；-SourceBakeInput 可指定项目内其他候选目录。
默认 Probe 使用生产 Skeleton 副本且不保存，检查后删除临时目录。
-SourceBakeCommit 才保存五个新动画，目标目录已存在时拒绝覆盖。
关闭编辑器和实际导入仍须遵守项目会话管理要求。

## 使用边界

导入目标为 /Game/Experiments/Stance_20261022/SourceBaked。
当前实验地图、BlendSpace 和既有 92 段派生动画没有切换。
本版本用于原始动作与生产骨架的导入验证。
它不代表分层、持枪、接地、循环或网络行为已通过验收。

[验证记录](../../../Docs/验证/StanceSourceBaked_20261115/README.md)。
