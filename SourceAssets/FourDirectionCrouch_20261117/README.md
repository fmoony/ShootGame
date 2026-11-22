# 四方向下蹲原始资源与导入副本

来源目录：D:/GameResource/Model/UETemplate/Animation/Crouch。
本目录属于 Stance 隔离实验，不接入生产动画。

- Original：四份原始 FBX 的逐字节副本；来源与 SHA256 见 Sources.json。
- ImportReady：完整父层变换进入 Root 的导入副本，不生成新的身体或脚步轨迹。
- prepare_import.py：仅重放父层转换，默认输出到新的 Saved 候选目录。
- import_ue.py：本次导入与逐帧校验脚本；目标存在时拒绝覆盖。
- ImportReady/Expected.json：直接读取原始 FBX 完整层级得到的逐帧位置与旋转。
- ImportReady/BakeSummary.json：Blender 中转换前后的保真误差。

没有统一骨盆高度、躯干前倾、步幅、相位、支撑比例或源动作时长。
保留原始 Root 平移、旋转、身体起伏与净行进；右移来源为原地动作。
只消除固定的默认 Root 坐标基准，不逐片段重设初始朝向或地面。
未修正源动画的 IK 辅助骨轨道，也未添加 IK、接地补偿或新的斜向动画。

UE 资产根：
/Game/Experiments/Stance_20261022/FourDirection_20261117

当前 Skeleton 是生产 SK_Mannequin 的隔离副本 SK_FourDirection。
其骨骼参考姿势与生产一致，只在副本添加生产兼容关系与 Manny Simple 预览。
四段序列均来自本目录 ImportReady，不引用旧实验动画。

本次证据：Saved/FourDirectionReimport_20261117_01。
导入保真验证与动画自然度、接地、混合质量验收分别报告。
