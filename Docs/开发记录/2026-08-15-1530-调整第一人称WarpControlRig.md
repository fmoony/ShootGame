# 调整第一人称 Warp Control Rig

- 日期：2026-08-15
- 计划提交说明：`动画：调整第一人称 Warp Control Rig`
- 变更类型：资产

## 目的

调整第一人称 Warp Control Rig（`CtrlRig_FPWarp`）的动画表现。该资产是 Shooter 第一人称手臂动画的实际依赖，属于正式资产。按 [Shooter Content Browser 资产目录整理执行方案](../执行计划/Shooter_Content_Browser_资产目录整理执行方案.md) 阶段 0 要求，先把该动画行为修改独立提交，避免与后续大规模资产路径迁移混在同一提交中。

## 本提交完成内容

- `Content/Variant_Shooter/Anims/Base/CtrlRig_FPWarp.uasset` 的 Control Rig 动画调整（用户编辑器内完成并保存，具体参数以 `.uasset` 为准）。

## 验证结果

- 用户在 Unreal 编辑器中确认调整结果正确并保存资产。
- 纯资产行为调整，未改动 C++、配置或测试；本轮不运行完整自动化。

## 遇到的问题

无。

## 处理方式

无。

## 遗留项

- 本资产后续将随 Content 目录整理迁入 `/Game/Shooter/Animation/Rigs/`。
