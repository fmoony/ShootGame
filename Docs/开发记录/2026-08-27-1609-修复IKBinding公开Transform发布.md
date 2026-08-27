# 修复 IK Binding 公开 Transform 发布缺失

- 日期：2026-08-27
- 计划提交说明：`动画：修复IK Binding公开Transform发布`
- 变更类型：生产代码 / 测试

## 目的

修复第三人称 IK Binding 状态机迁移引入的表现回归：状态机在私有 Binding 缓存中算出了正确的 `HandToMuzzle` / `LeftHandGripInRightHandSpace` / `HandGripInLeftHandSpace`，但 `RefreshIKEnabled` 从未把这三个几何数据发布到 AnimBP 消费的公开属性，导致运行中 AnimGraph 始终拿到初始 `FTransform::Identity`——IK 不是被关闭，而是带着错误几何运行（枪口前向折叠到 hand_r 前向，视觉表现为系统性瞄准偏差）。

## 本提交完成内容

- `UShooterThirdPersonAnimInstance::RefreshIKEnabled` 在 State / Reason / Enabled 同步之外，无条件把三个缓存 Transform 复制到公开接口：
  - `HandToMuzzle = AimBinding.HandToMuzzle`；
  - `LeftHandGripInRightHandSpace = LeftHandBinding.WeaponGripInRightHandSpace`；
  - `HandGripInLeftHandSpace = LeftHandBinding.HandGripInLeftHandSpace`。
  - 非 Ready 状态缓存已被 `SetBindingState` 清为 Identity，无条件复制不泄露旧数据；生产路径每帧最后调用 `RefreshIKEnabled`，`ForceRebuildIKBindings` 与复位路径同样经过它。
- 头文件注释同步为“刷新公开输出”。
- 新增回归测试 `ShootGame.Aim.Binding.PublicOutputPublish`：
  - 无武器生产序列下公开 Transform 保持 Identity；
  - 完整生产序列（签名注入版，镜像 `UpdateShooterAnimationData` 调用顺序）后公开属性 == 私有 Binding 缓存，且几何结果真实非 Identity；
  - 帧级输入注入后 `bAimIKEnabled` / `bLeftHandIKEnabled` 成立；
  - `ResetBindingsAndOutputs` 后三个公开 Transform 回到 Identity。
- 测试壳新增 `CallUpdateShooterAnimationDataForSignatures`（生产序列签名注入版）。

## 验证结果

- `Scripts/Tests/BuildEditor.ps1`：ShootGameEditor Development 编译通过。
- `Scripts/Tests/RunAutomation.ps1 -TestFilter ShootGame.Aim.Binding`：17 个测试全部通过（含新增 PublicOutputPublish）；报告位于 `Saved/Automation/Reports/20260827_160644_ShootGame.Aim.Binding/index.json`。
- `Scripts/Tests/RunAutomation.ps1 -TestFilter ShootGame`：Passed 77，Warnings 5，Failed 0；报告位于 `Saved/Automation/Reports/20260827_160717_ShootGame/index.json`。
- `Scripts/Tests/RunStandaloneSmoke.ps1`：通过。
- `Scripts/Tests/RunNetworkSession.ps1 -MapPath /Game/Shooter/Maps/Lvl_Shooter`：Dedicated + 2 客户端会话通过，三端无 IK Binding 异常日志。
- `git diff --check`：通过。

## 遇到的问题

- 回归测试最初按“完整生产序列后直接断言 bAimIKEnabled == true”编写，但 `UpdateAimInputs` 会按角色运行上下文重置 `AimDirectionWorld` / `bAimTargetWorldValid`（测试角色非本地控制且无表现目标），序列后 Enabled 必然为 false。这不是发布缺陷，而是输入层语义；调整为在发布断言之后单独注入有效帧级输入再断言 Enabled。

## 处理方式

- 主修复放在 `RefreshIKEnabled` 内（唯一统一发布点），保持 Rebuild 只写私有 Binding、Refresh 统一发布的单向数据流；`ResetBindingsAndOutputs` 的公开清空保持不变。
- 断言顺序调整：先断言公开 == 私有（发布核心），再注入输入断言 Enabled 组合（输入层，已有 EnabledInputSeparation 覆盖）。

## 遗留项

- 人工 PIE 视觉回归（Rifle 枪口重新对准、左手重新贴合握把；Pistol Aim Ready / LeftHand Unsupported）待人工验收，预期恢复到提交 `0b18090` 的 IK 基线。
- 日志中的 `Failed to register test with the name 'FShooter*'` 警告经核对仅存在于 2026-08-25 的旧编辑器日志（针对 FShooterAbility*，属编辑器热重载时的 Automation 重复注册历史噪音）；独立自动化进程中测试注册与执行正常，不阻塞本次修复，未在本提交处理。
