# 网络射击 AI 自主验证契约

- 生效日期：2026-09-16
- 最近修订：2026-10-07（正式收敛 Owner Fire Presentation 与 Authority Result 语义）
- 适用范围：玩家网络射击、预测表现、本地动作边界、服务器裁决、远端确认与相关测试
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

### 1.1 不变量以标题含义为准

- 标题给出的是必须成立的标准；正文列举的检查项只是该含义在当前阶段的实例，不是定义边界。
- 清单逐项通过不等于不变量成立。若本次改动触及的含义维度没有被现有清单覆盖，
  必须补充证据，或在报告中显式列为缺失证据。
- 任何改动或新计划都必须先按标题含义逐条确认覆盖范围，再选择验证链；
  确认流程见第 3 节，输出结构见第 7 节。
- 含义的扩展或收缩属于 Gameplay 语义变化，按第 5 节交由用户确认。

### 1.2 Fire 的三层事实

网络射击必须分别记录以下三件事；它们相关，但不是同一个事实：

```text
Shot Intent        = 玩家尝试开枪，并向服务器提交该意图
Owner Presentation = 本地拥有者是否在输入发生时立即表现这一枪
Authority Result   = 服务器是否认定这一枪在 Gameplay 中成立

Intent != Presentation != Authority Result
```

- `Shot Intent` 是请求事实，不因客户端暂时没有足够预测依据而被吞掉；
- `Owner Presentation` 是本地时间敏感反馈，当前项目包括 FP Fire Montage、Recoil、Owner Muzzle FX
  与 Owner Fire Sound；
- `Authority Result` 只由服务器裁决，决定 Ammo、Fire cadence、Weapon identity、Projectile / Trace、
  Hit、Damage、Death 以及其它 Gameplay 结果。

相关术语的职责边界如下：

- `Client Prediction`：客户端先建立本地动作边界与可选的 Owner Presentation，
  不建立权威 Gameplay 结果；
- `Ammo Prediction`：拥有端的本地预算或显示扣减，只能由本地预测路径消费，
  并在 Accept / Reject 后结清，
  不能写服务器 Ammo；
- `PredictionKey`：把预测消费、Reject Refund 与最终结清归因到具体 Ability Activation 的标识，
  不能单独证明 Owner 已表现，也不能替代服务器 Authority Result；
- `Remote Presentation`：服务器 Commit 后驱动远端第三人称表现，不复用 Owner 的本地预测入口。

Owner 瞬时 Fire Feedback 的目标是在本地 Fire Action 发生时尽可能以 0 RTT 反馈输入。
服务器确认不是这些反馈的播放时机。若本地 Shot Attempt 当时没有足够依据进行预测表现，之后服务器接受
也不要求延迟补播过去的 Owner 瞬时反馈。

“有足够预测依据”只决定客户端此刻是否敢先表现、是否消费本地预测 Ammo budget；它不能被解释为客户端
是否有资格向服务器发送 `Shot Intent`。过期的客户端复制状态可以抑制本地表现或本地预算消费，但不能
直接吞掉一个应由服务器裁决的 `Shot Intent`。

## 2. 顶层验收不变量

每条不变量由三部分组成：

```text
含义：标题断言的标准，是判定的唯一依据
当前阶段的实例：该含义在当前实现与测试中的具体落点，非穷尽，可随阶段增删
反例：出现即判 FAIL 的可观察现象
```

### Invariant 1：Owner Immediate Feedback

**含义**：Owner 的瞬时 Fire Feedback 属于本地时间敏感表现。具备足够本地预测依据时，
本地 Shot Attempt 应在服务器确认返回前立即尝试表现；该表现来自本地路径，而不是服务器确认回放。
本不变量不要求每一个 Server Accepted Shot 最终都补齐一次 Owner 瞬时表现。

- 玩家输入必须在本地形成可用的 `Shot Intent`，不等一次 RTT；
- 有足够本地预测依据时，Owner 的 FP Fire Montage、Recoil、Muzzle FX、Fire Sound
  立即尝试播放；
- 本地依据不足时，可以不播放 Owner 瞬时反馈，也不消费本地预测 Ammo budget，
  但仍必须提交 `Shot Intent`；
- 允许本地表现与服务器结果存在相位差；`Accepted` 但当时未预测表现不再构成单向缺口；
- Owner 瞬时表现至多发生一次，并且只由本地 Shot Attempt 在正确时间窗口决定。

**当前阶段的实例（非穷尽）**：

- 第一人称 Montage；
- Muzzle FX；
- Sound；
- Recoil；
- 换弹：本地换弹窗口与由 `State.Reloading` 驱动的换弹表现
  （当前计划的目标；实施前 Owner 换弹窗口仍依赖服务器，属已登记的缺口）；
- 全自动：本地表现节拍在按住期间不等待服务器确认。

Projectile 仍只能由服务器权威生成，不属于本不变量的实例。

**反例（出现即 FAIL）**：

- Owner 表现要等服务器确认到达后才出现；
- 本地已有足够预测依据，却因等待 Server confirmation 而延迟表现；
- 同一个本地 Shot Attempt 由预测路径与确认路径各播放一次 Owner 瞬时表现；
- RTT 后为过去的输入补播 Recoil、Muzzle FX、Fire Sound 或 FP Fire Montage。

### Invariant 2：Local Prediction Obeys Weapon Rules

**含义**：本地预测只使用客户端**确定已知**的规则；它既不得违反这些规则，也不得替服务器猜测
未知真值，并且一切误预测必须有边界。三个维度必须同时成立：

1. **服从**：本地确定的武器规则与本地已知的动作互斥必须真正生效。
   - Semi-auto 可见表现频率不得超过 `RefireRate`，本地节拍只由本地有效 Shot Attempt 推进；
   - Full-auto 连续表现按 `RefireRate` 推进；
   - Weapon 必须有效、未隐藏，并且仍是 `CurrentWeaponActor`；
   - 本机已知的动作互斥状态必须通过 GAS Tag 门控生效，不得为了保住输入而在本地绕过门控。
2. **不越权**：不得用未知或可能过期的真值替代服务器裁决。
   - 不得预测 Ammo、Projectile、Hit、Damage、Death、Score；
   - 不得用可能过期的复制状态（Ammo、Tag）吞掉应提交给服务器的 `Shot Intent`；
     这些状态可以决定本地是否先播放 Owner 瞬时表现、是否消费预测 Ammo budget，不能替代服务器裁决；
   - 客户端以为有弹而服务器实际无弹时，允许一次纯 Cosmetic 误预测，由服务器 Reject 收敛。
3. **收敛**：误预测必须有边界且可收敛。
   - 误预测不得修改 Ammo、生成 Projectile、结算 Hit / Damage / Death / Score；
   - 裁决到达后，本地持续表现、活动 Ability 与本地状态必须收敛；
   - 不得把输入延迟变成补枪：本地节拍未 Ready 的输入必须直接丢弃，不得稍后补射。

**当前阶段的实例（非穷尽）**：

- Semi-auto 高速连点的可见表现频率不超过 `RefireRate`；
- Full-auto 连续表现按 `RefireRate` 推进，松开后停止；
- 节拍未 Ready 的输入直接丢弃，不进入缓冲、不稍后补射；
- Weapon 无效、已隐藏或不再是 `CurrentWeaponActor` 时不产生本地动作边界；
- `State.Dead` 是硬阻塞，不得缓存后重试；
- 本机已知 `State.Reloading` / `State.Equipping` 阻塞激活时，玩家输入不得静默丢失，
  也不得在阻塞结束后重复消费；实现方式（例如本地输入缓冲）不属于契约内容；
- 本地 Owner 瞬时表现不由服务器确认补播；Reject 不回滚已经播出的瞬时表现，
  也不为未播放的历史事件补播。

**反例（出现即 FAIL）**：

- 本地表现快于 `RefireRate`，或由表现入口自行推进节拍；
- 本地 Shot Attempt 因等待确认而延迟表现，或确认路径为过去的 Owner 事件补播；
- 本地预测修改 Ammo、生成 Projectile 或结算伤害；
- 被阻塞的输入永久丢失，或在阻塞结束后补出额外的一次射击。

### Invariant 3：Server Is Final Authority

**含义**：每一次真实 Gameplay 动作都必须提交服务器校验；所有权威结果与权威事务只能由服务器
产生；服务器拒绝时不产生任何权威结果，且客户端预测状态收敛。

以下结果只能由服务器决定：

- Ammo；
- 换弹弹药转移（Reload 事务）；
- Projectile；
- Hit；
- Damage；
- Death；
- Score。

Server Accept 还负责：

- 确认 Shot authority；
- 校验 Weapon identity、Ammo、Fire cadence 与其它权威前置条件；
- 产生 Projectile / Trace、Hit、Damage、Death 等 Gameplay 结果；
- 结清 Prediction record 与 AmmoDisplay / HUD 的最终状态；
- 驱动 Remote TP presentation。

Server Accept 不负责“确保 Owner 最终一定看到一次瞬时 Fire cosmetic”。
`Accepted Activation == exactly one Authority Shot` 仍然是权威契约，但不再推出
`Accepted Shot == one Owner Presentation`。

服务器接受时，结果必须来自权威路径。服务器拒绝时：

- 不产生任何权威 Gameplay 结果；
- 客户端持续性预测立即停止；
- Ability、本地动作状态（如 `State.Firing`、`State.Reloading`）、Timer、CachedWeapon
  等预测状态最终收敛；
- 已经播放的一次或短暂纯 Cosmetic 表现无需反向抹除。

客户端本地预测可以建立本地动作边界与表现，但不得提交任何权威事务，也不得改写权威真值。

Server Reject 负责：

- 不产生权威 Shot；
- 退还必要的本地预测 Ammo budget；
- 撤销可撤销的预测状态；
- 结清 Prediction record 与 HUD overlay。

Server Reject 不负责：

- 倒放 Recoil；
- 撤回已经播放的 Fire Sound 或 Muzzle FX；
- 回滚已经播放的 FP Fire Montage。

仅证明“没有继续生成 Projectile”不能代替 Cleanup 证据；必须观察应被清除的预测状态本身。

### Invariant 4：Remote Is Confirmed Only

**含义**：Remote 客户端的表现只能来源于服务器确认，不得依赖 Owner 的本地预测入口，
也不得因为 Owner 的本地预测而在远端产生额外表现或额外权威结果。

Owner FP Presentation 与 Remote TP Presentation 是两条不同契约：

- Owner FP Presentation：本地 Shot Attempt 驱动，追求 0 RTT；
- Remote TP Presentation：`Server Commit → TP Montage → Remote Muzzle / Sound`，由权威结果驱动。

取消 Owner confirmed cosmetic backfill 不代表取消服务器确认后的 Remote Presentation。

**当前阶段的实例（非穷尽）**：

- 第三人称 Montage；
- Muzzle FX；
- Sound；
- 必要的复制状态（例如远端观察到的换弹状态）。

当前契约只要求表现来源经过服务器确认；若确认通道使用 Unreliable RPC，
不额外承诺丢包环境下每一份纯表现都最终送达。

**反例（出现即 FAIL）**：

- Remote 端表现由 Owner 本地预测路径触发；
- Owner 的本地预测在远端造成重复表现或重复权威结果。

### Invariant 5：Exactly One Authority Result

**含义**：一次被服务器认可的 Gameplay 动作只产生一份权威结果；Owner 瞬时表现至多发生一次，
并且只由本地 Shot Attempt 在正确时间窗口决定。Owner 可能没有该次瞬时表现，不能因此补造历史事件。

不得因为 LocalPredicted、Listen Host 双角色、Multicast、输入缓冲重试、预测重试或重复调用路径产生：

- 双 Ammo 消耗；
- 换弹事务重复提交（同一窗口内两次弹药转移）；
- 双 Projectile；
- 双 Hit / Damage；
- 双 Authority Commit；
- Owner 对同一认可动作看到重复的第一人称表现。

被服务器拒绝但已经播放的一次纯本地表现不属于权威结果；它仍必须满足 Invariant 2 与 3 的
限速与收敛要求。

### 2.1 Owner Fire Presentation 的四种最终路径

| 路径 | Owner 本地 | Server | Client settlement |
| --- | --- | --- | --- |
| Predicted + Accepted | 立即表现 | 接受 | 结清预算与 Record，不重复表现 |
| Predicted + Rejected | 已经表现 | 拒绝 | Refund / 状态收敛，不回滚瞬时表现 |
| Unpredicted + Accepted | 当时没有表现 | 接受并产生权威 Shot | 结清状态/HUD/Record；Owner 不补播 |
| Unpredicted + Rejected | 没有表现 | 拒绝 | 只清理状态，不表现 |

`Backfill=1` 不再是 Server Accepted 或 Owner 成功的必要条件。测试必须分别记录
`Owner immediate feedback`、`Confirmed replay` 与 `Authority Shot`，不能用总表现数等同三层事实。

FullAuto / PktLag 场景禁止旧 Shot 在后续时间发生 delayed Owner replay。
Host / Standalone Owner 仍在本地 Ability Activation 路径中立即尝试 Owner Feedback，
不依赖 Server Accepted 后的 confirmed cosmetic backfill。

### 2.2 结算原则与未来上下文

Reconciliation primarily corrects state, not historical instantaneous presentation。

网络结算首先纠正状态，而不是重新演出已经过去的瞬时第一视角事件。可以纠正：

- Ammo；
- Pending prediction；
- HUD；
- Shot Record；
- Gameplay result。

不应在 RTT 后重演 Recoil、Muzzle FX、Fire Sound 或 FP Fire Montage。

未来 `Shot Intent` 可以携带 `ClientFireTime`、input sequence、aim / view timestamp 或其它
prediction context，供服务器理解客户端何时发起了什么并进行历史验证或 lag compensation。
服务器仍始终自行裁决 Ammo、Refire、Weapon legality、Hit 与 Damage：可以相信 Client 提供的
Intent context，但不能信任 Client 对 Gameplay result 的最终判断。

## 3. Agent 自主验证工作流

### 修改前

1. 先按第 7 节结构逐条确认五条不变量的**含义覆盖**：触及的维度、不应改变的维度、
   需要的新增证据、明确不在本次范围的维度与理由；
2. 再说明本次改动影响哪些不变量，以及每条不变量可能退化的具体方式；
3. 不得只以 P1 子阶段、Coordinator Phase 或内部 Case 编号描述风险。

### 修改中

Agent 自主选择最小充分验证链：

- 纯计算、射速资格、Ammo 条件和服务器 Gameplay 前置规则优先使用普通 Automation Test；
- 本地表现入口及其无权威副作用优先使用运行时 Automation Test；
- 本地输入意图、缓冲窗口、生命周期清理优先使用运行时 Automation Test；
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
中，但不能替代不变量结论。报告的结论必须与第 7 节的含义覆盖确认一致；实施中覆盖范围发生变化时，
先更新确认再继续。

## 4. 证据质量规则

任何不变量要标记为 PASS，证据必须同时满足以下规则：

- 先取得稳定的目标对象和场景起点快照，再比较该场景的增量；
- 目标 Weapon、Ability 或 Avatar 在观测期间失效时，不得把缺失值折算为 0 或成功；
- `Delta >= 0`、未进入目标分支、零样本或无实际输入不能作为行为已经执行的证据；
- `Inconclusive` 不计为 PASS；无法制造目标时序必须报告证据缺失；
- 证明 Reject Cleanup 时必须先证明客户端确实发起了目标预测尝试，且服务器确实拒绝；
- 证明 Full-auto Timer Cleanup 时必须使用真实 Full-auto 配置并证明 Timer 曾经活动；
- 证明 Remote Confirmed 时必须建立 Authority Commit 与 Remote 表现之间的因果或顺序关系；
- 证明 Owner Immediate 时必须在同一客户端时钟域比较本地 Shot Attempt、本地表现和服务器确认到达；
  本地依据不足时，还必须证明 `Shot Intent` 仍然提交，且 `Unpredicted + Accepted` 不触发
  Owner 延迟补播；
- 证明 Exactly One 时使用场景增量，不能依赖池化 Actor 的历史累计值为零；
- Montage、Muzzle FX、Sound、Recoil 必须有各自可归因的入口证据，不能用单一笼统计数替代全部四项；
- `IsAnyMontagePlaying()`、任意非 Owner 武器弹药为 0 等间接现象不能单独证明目标机制；
- 证明输入意图机制时必须分别证明：被阻塞的输入在窗口内不丢失、一次输入不对应两次本地动作边界
  或两次权威请求、输入意图不跨生命周期泄漏；
- 证明换弹预测时必须分别证明 Owner 本地窗口、服务器事务、Reject 收敛三者独立成立，
  且服务器弹药转移恰好一次；
- 证明“本地表现未被过期复制状态否决”时必须构造复制状态与本地有效动作不一致的场景，
  并同时观察：Shot Intent 仍到达服务器，客户端可不播放瞬时 Owner 反馈且不消费预测预算，
  服务器仍独立裁决最终结果；
- Owner 瞬时表现验证必须分别记录 Predicted / Unpredicted、Accepted / Rejected、
  `Owner immediate feedback`、`Confirmed replay` 与 `Authority Shot`；
- `Backfill=1` 不再是 Accepted 的成功条件；对 Owner 发生确认补播本身应作为违反当前契约的信号，
  而不是缺发补偿证据；
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
- 不变量含义的扩展或收缩；
- 预测边界扩大或缩小；
- 服务器与客户端权威职责变化；
- Remote 纯表现从“不保证逐发必达”改为可靠送达；
- 会改变玩家可感知节拍、反馈数量或纠错体验的取舍。

## 6. 固定最终报告格式

```text
Invariant 1 Owner Immediate Feedback

- PASS / FAIL
- 含义覆盖：Shot Intent / Owner Presentation / Authority Result 三者分离
- Predicted / Unpredicted：
- Owner immediate feedback：
- Confirmed replay：必须为 0
- Server Accepted 但未本地表现：允许，需记录原因与状态收敛
- 证据：
- 使用的测试：

Invariant 2 Local Prediction Obeys Weapon Rules

- PASS / FAIL
- 含义覆盖：服从 / 不越权 / 收敛
- Semi-auto：
- Full-auto：
- 本地已知限制（Weapon 有效 / Reloading / Equipping / Dead）：
- 不越权（不得预测 Ammo / Projectile / Hit / Damage；不得用过期复制状态否决有效动作）：
- 收敛（节拍未 Ready 不补枪 / Reject 后状态清理 / 不延迟补播历史 Owner 事件）：
- 使用的测试：

Invariant 3 Server Is Final Authority

- PASS / FAIL
- 含义覆盖：动作已提交校验 / 结果只由服务器产生 / 拒绝无结果且收敛
- Accept：
- Reject：
- Cleanup：
- `Accepted Activation == exactly one Authority Shot`：
- Owner confirmed cosmetic backfill：不属于 Accept 职责，必须为 0
- 使用的测试：

Invariant 4 Remote Is Confirmed Only

- PASS / FAIL
- 含义覆盖：来源经过服务器确认 / 不依赖 Owner 预测入口 / 不产生额外远端结果
- 证据：
- 使用的测试：

Invariant 5 Exactly One Authority Result

- PASS / FAIL
- 含义覆盖：一份权威结果 / Owner 表现至多一次且只由本地时间窗口决定
- Authority Commit 数：
- Ammo 消耗：
- 换弹事务提交次数：
- Projectile 数：
- 是否存在重复：
- 使用的测试：

Owner Fire Presentation 四路径：

```text
PredictedAccepted:   Owner immediate feedback = 1, Confirmed replay = 0
PredictedRejected:   Owner immediate feedback = 1, Rollback cosmetic = 0, Authority Shot = 0
UnpredictedAccepted: Owner immediate feedback = 0, Confirmed replay = 0,
                     Authority Shot = 1, 状态收敛
UnpredictedRejected: Owner feedback = 0, Authority Shot = 0, 状态收敛
```

FullAuto / PktLag：不得在后续时间发生 delayed Owner replay。

- Build / Automation / Dedicated / Listen / Emulated 结果
- 当前缺失的自动化证据
- 是否存在需要用户本人做设计取舍的问题
```

报告中的 PASS 必须由本轮实际读取的报告或日志支持。未运行的层级写“未运行及原因”，不得沿用历史
绿色结果冒充本轮证据。

## 7. 按标题含义确认改动与计划

任何改动或新计划在实施前必须以如下结构逐条确认五条不变量（可直接写入执行计划、工作说明或
开发记录）：

```text
Invariant N <标题>
- 含义中本次改动触及的维度：
- 含义中本次改动不应改变的维度：
- 需要的新增证据：
- 明确不在本次范围的维度与理由：
```

规则：

- 四条都必须填写；“本轮不涉及”不是理由，必须说明该维度为什么不受影响，或给出验证方式；
- 确认要覆盖含义的全部维度，而不是复述正文清单；
- 缺失证据必须显式列出，不得用“整体绿色”替代；
- 确认结果与最终报告的不变量结论必须一致；
- 实施中含义覆盖发生变化时，先更新确认，再继续实施。

当前计划的确认结果见
[输入缓冲与 Reload 本地预测执行计划](../已完成计划/输入缓冲与Reload本地预测执行计划.md)
（已完成并归档）
的「按契约标题含义的不变量覆盖确认」一节。

计划内部的子不变量（例如按模块拆分的 Input Buffer、Fire Prediction、Reload Prediction 等）只是
含义覆盖的实现分解，必须能够映射回本文五条不变量，不能替代它们。

已归档阶段的验收结论见
[P1 基础射击反馈执行计划](../已完成计划/P1_LocalPredicted基础射击反馈执行计划.md)。
