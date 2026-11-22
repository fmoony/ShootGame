# Blender 下蹲接地验证

日期：2026-11-14；UE 5.8.4；Blender 4.5.8 LTS。
目标是原 Stance 实验，不涉及生产角色或网络接入。

## 骨骼数据

- RuntimeFinal.summary.json：264 场景、43,808 帧逐场景统计。
- RuntimeFinal.csv.gz：最终实际 Body 的完整运行数据。
- RuntimeBefore.summary.json：40 场景、7,511 帧旧版基线。
- RuntimeBefore.csv.gz：旧版实际运行数据，不是单段动画预测值。
- VerifyBlender.json：90 段资源重载、循环、骨长和保护检查。
- BlendReadback.json：最终 blend 中 90 个实际 Action 回读。
- Protection.json：21 项资产的开始和结束哈希核对。
- MoveSummary.json：最终方向、握持和肩部连续性数据。

每帧记录 pelvis、左右 thigh / calf / foot / ball、head 和双手。
heel 是参考姿态定义的局部标记，不是地面 Trace。
记录 Body 组件空间与世界空间位置、组件空间旋转。
脚滑按支撑期世界空间接触点位移计算。
新旧方向密度不同，不解释为逐帧对应的全范围 A/B。

## 判定边界

稳定动作逐场景约束穿地不超过 0.5 cm、无双脚高于 1 cm。
支撑高度 P99 不超过 1 cm、脚滑 P95 不超过 5 cm/s。
所有方向分别检查，不用整体统计隐藏局部失败。
最终稳定最低接触约 -0.322 cm，未出现双脚同时悬空。
最差场景支撑高度 P99 约 0.416 cm，脚滑 P95 约 3.694 cm/s。
腿部骨长变化小于 0.000002 cm。

启停混合最低参考点约 -2.92 cm，单独记录为遗留问题。
全过程没有双脚同时高于 1 cm 的帧。
稳定通过不等于启停、鞋底网格、碰撞或坡面通过。

## 实现

C++ 不再按脚尖移动骨盆，也不在方向混合后求双腿。
腿部结果由 Blender 关键帧和普通动画混合产生。
上身 Grip 和脊柱瞄准保持原职责。

原始 FBX、Skeleton、正式 AnimBP、地图和 FP 网格不改。
站立和滑铲保持原样，失败候选与完整日志保留在 Saved。
本目录只收最终证据和基线，不回写历史验证目录。
