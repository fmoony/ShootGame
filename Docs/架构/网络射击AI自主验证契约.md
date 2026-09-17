# 网络射击 AI 自主验证契约

- 生效日期：2026-09-16
- 适用范围：玩家网络射击、预测表现、服务器裁决、远端确认与相关测试
- 维护责任：执行网络射击开发与验证的 Agent

## 1. 文档定位

本文是 ShootGame 网络射击的长期验收契约，也是 Agent 选择测试、解释证据和提交最终报告时的
首要依据。它描述系统必须满足的顶层不变量，不规定 Coordinator 的 Phase、Case、内部 RPC、
计数器名称或状态机组织方式。

以下内容一律视为测试实现细节，由 Agent 自主维护：

- Coordinator 内部的 Phase / Case / 8A / 8B 等编号；
- 用于制造场景的测试 RPC、门闩、计数器和等待条件；
- 一个不变量由一个还是多个测试共同取证；
- 定向验证时选择 Automation、Dedicated、Listen 或 Emulated 的具体组合。

若执行计划中的旧测试矩阵、内部编号或历史通过条件与本文冲突，以本文的不变量、证据规则和
报告口径为准。执行计划中的 Gameplay 范围、生产职责和非目标仍然有效。

## 2. 顶层验收不变量

### Invariant 1：Owner Immediate Feedback

玩家按下开火后，拥有者本地的第一人称表现必须在服务器确认返回前开始，不等待一次 RTT。

必须分别覆盖：

- 第一人称 Montage；
- Muzzle FX；
- Sound；
- Recoil。

当前阶段不预测真实 Projectile。Projectile 仍只能由服务器权威生成。

“立即”指表现从本地输入或本地预测激活路径启动，并且时序早于同次服务器确认；不要求 Montage、
粒子、音频和镜头效果在同一渲染帧内完成全部生命周期。

### Invariant 2：Local Prediction Obeys Weapon Rules

本地预测表现必须服从当前武器和客户端已经知道的限制：

- Semi-auto 高速连点的可见表现频率不得超过 `RefireRate`；
- Full-auto 连续表现按 `RefireRate` 推进；
- Ammo 不足时不得继续本地持续表现；
- 本机已知 `State.Reloading`、`State.Equipping`、`State.Dead` 时不得播放；
- Weapon 已隐藏或不再是 `CurrentWeaponActor` 时不得播放；
- 本地表现冷却按 WeaponActor 隔离，不得跨武器共享。

对尚未复制到客户端的服务器权威状态，允许在 Reject 到达前出现有限的短暂误预测，但误预测不得
修改 Ammo、生成 Projectile、结算 Hit / Damage / Death / Score，且必须在裁决后收敛。

若本地门控或冷却误挡了一个最终被服务器接受的射击，确认回退只补播一次，不得形成重复表现。

### Invariant 3：Server Is Final Authority

每一次真实 Gameplay 射击都必须提交服务器校验。以下结果只能由服务器决定：

- Ammo；
- Projectile；
- Hit；
- Damage；
- Death；
- Score。

服务器接受时，结果必须来自权威路径。服务器拒绝时：

- 不产生任何权威 Gameplay 结果；
- 客户端持续性预测立即停止；
- Ability、`State.Firing`、Timer、CachedWeapon 等预测状态最终收敛；
- 已经播放的一次或短暂纯 Cosmetic 表现无需反向抹除。

仅证明“没有继续生成 Projectile”不能代替 Cleanup 证据；必须观察应被清除的预测状态本身。

### Invariant 4：Remote Is Confirmed Only

Remote Client 不预测其他玩家的射击，只消费服务器确认后的第三人称结果：

- 第三人称 Montage；
- Muzzle FX；
- Sound；
- 必要的复制状态。

Remote 表现不得依赖 Owner 的本地预测入口。当前契约只要求表现来源经过服务器确认；若确认通道使用
Unreliable RPC，不额外承诺丢包环境下每一份纯表现都最终送达。

### Invariant 5：Exactly One Authority Result

每一次服务器认可的射击只能产生一份权威 Gameplay 结果。不得因为 LocalPredicted、Listen Host、
Multicast、确认回退或重复调用路径产生：

- 双 Ammo 消耗；
- 双 Projectile；
- 双 Hit / Damage；
- 双 Authority Commit；
- Owner 对同一认可射击看到重复的第一人称表现。

被服务器拒绝但已经播放的一次纯本地表现不属于权威结果；它仍必须满足 Invariant 2 和 3 的限速与
收敛要求。

## 3. Agent 自主验证工作流

### 修改前

Agent 必须先说明本次改动影响哪些不变量，以及每条不变量可能退化的具体方式。不得只以 P1 子阶段、
Coordinator Phase 或内部 Case 编号描述风险。

### 修改中

Agent 自主选择最小充分验证链：

- 纯计算、射速资格、Ammo 条件和服务器 Gameplay 前置规则优先使用普通 Automation Test；
- 本地表现入口及其无权威副作用优先使用运行时 Automation Test；
- Client Prediction、Server Reject、Remote Confirm、Listen Host 去重和跨 Avatar 生命周期使用
  网络 E2E；
- 只有确实涉及延迟或丢包语义时才运行 Emulated；
- 只有改动或证据涉及主机双角色时才运行 Listen；
- 大阶段收口或用户明确要求时才运行七阶段完整回归。

不得为了“覆盖更多”无限增加网络场景。优先复用能够直接证明目标不变量的既有场景；若一个服务器
本地规则可以由 Automation 证明，不得仅为了沿用 Coordinator 而新增网络 Phase。

### 现有证据不足时

可以补测试，但在实施前必须在工作说明或开发记录中写清楚：

1. 缺少哪条不变量的哪一项证据；
2. 现有测试为什么不能证明；
3. 新测试具体增加哪个可观察事实；
4. 为什么必须是网络 E2E，或为什么普通 Automation 已足够。

新增测试不得复制生产判定来制造通过结果，不得通过反射篡改武器类型或配置来伪造与生产不一致的
测试条件。

### 修改后

Agent 根据受影响不变量自行组合验证链，并按第 6 节固定格式报告。测试内部编号可以留在日志定位信息
中，但不能替代不变量结论。

## 4. 证据质量规则

任何不变量要标记为 PASS，证据必须同时满足以下规则：

- 先取得稳定的目标对象和场景起点快照，再比较该场景的增量；
- 目标 Weapon、Ability 或 Avatar 在观测期间失效时，不得把缺失值折算为 0 或成功；
- `Delta >= 0`、未进入目标分支、零样本或无实际输入不能作为行为已经执行的证据；
- `Inconclusive` 不计为 PASS；无法制造目标时序必须报告证据缺失；
- 证明 Reject Cleanup 时必须先证明客户端确实发起了目标预测尝试，且服务器确实拒绝；
- 证明 Full-auto Timer Cleanup 时必须使用真实 Full-auto 配置并证明 Timer 曾经活动；
- 证明 Remote Confirmed 时必须建立 Authority Commit 与 Remote 表现之间的因果或顺序关系；
- 证明 Owner Immediate 时必须在同一客户端时钟域比较本地输入、本地表现和服务器确认到达；
- 证明 Exactly One 时使用场景增量，不能依赖池化 Actor 的历史累计值为零；
- Montage、Muzzle FX、Sound、Recoil 必须有各自可归因的入口证据，不能用单一笼统计数替代全部四项；
- `IsAnyMontagePlaying()`、任意非 Owner 武器弹药为 0 等间接现象不能单独证明目标机制；
- 测试整体绿色不等于五条不变量全部通过，未取证的项目必须列为缺失证据。

主观的枪声听感、Recoil 手感和画面自然度不由无头自动化宣告通过。自动化负责证明调用、来源、时序、
次数、去重和状态收敛；主观质量需要人工体验时，应明确列为人工验收边界。

## 5. 决策与升级边界

以下测试内部事项由 Agent 自主处理，不向用户抛出决策：

- Phase / Case 命名与重排；
- 测试夹具、等待条件、观测计数和日志字段；
- 移除恒真断言、假 0、未执行分支和重复场景；
- 将服务器本地场景降级为 Automation；
- 在不改变 Gameplay 语义的前提下合并重复网络场景；
- 根据受影响不变量选择定向或完整验证。

只有下列事项需要用户确认：

- Gameplay 语义变化；
- 预测边界扩大或缩小；
- 服务器与客户端权威职责变化；
- Remote 纯表现从“不保证逐发必达”改为可靠送达；
- 会改变玩家可感知节拍、反馈数量或纠错体验的取舍。

## 6. 固定最终报告格式

```text
Invariant 1 Owner Immediate Feedback

- PASS / FAIL
- 证据：
- 使用的测试：

Invariant 2 Local Prediction Obeys Weapon Rules

- PASS / FAIL
- Semi-auto：
- Full-auto：
- Reload / Equip / Dead / Ammo 等限制：
- 使用的测试：

Invariant 3 Server Is Final Authority

- PASS / FAIL
- Accept：
- Reject：
- Cleanup：
- 使用的测试：

Invariant 4 Remote Is Confirmed Only

- PASS / FAIL
- 证据：
- 使用的测试：

Invariant 5 Exactly One Authority Result

- PASS / FAIL
- Authority Commit 数：
- Ammo 消耗：
- Projectile 数：
- 是否存在重复：
- 使用的测试：

- Build / Automation / Dedicated / Listen / Emulated 结果
- 当前缺失的自动化证据
- 是否存在需要用户本人做设计取舍的问题
```

报告中的 PASS 必须由本轮实际读取的报告或日志支持。未运行的层级写“未运行及原因”，不得沿用历史
绿色结果冒充本轮证据。

## 7. 当前 P1 方案的适用方式

P1 执行计划和详细实施方案继续负责生产职责、API 边界、实现顺序与风险回退。本文负责最终验收与
测试治理：

```text
P1 生产方案
→ Agent 标识受影响不变量
→ 选择最小充分验证链
→ 修复或补齐真实证据缺口
→ 按五条不变量报告
```

P1-D 不再以“完成某个内部 Phase / Case”作为验收结论。内部场景可以继续存在，但只有能够直接映射到
本文五条不变量的证据才进入最终报告。
