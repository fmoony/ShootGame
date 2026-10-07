# Ammo Prediction 归因与收敛 Phase C 执行计划（复现阶段）

- 创建日期：2026-10-05。
- 关联问题记录：`Docs/执行计划/_待解决问题_换弹弹匣预测.md`（H-A1、H-A2、H-A3）。
- 源码基线：`3df68e6`；引擎：本机 UE 5.6.1。
- 当前阶段：只建立针对性复现证据，不修改生产预测语义。
  是否引入 Activation / Shot acknowledgement、是否扩大预测边界，留待复现结果后由用户确认。
- 2026-10-05 更新：H-A2 修复与 B-light（按 Activation 对账 + 确认补播）已实施，
  边界与不变量覆盖见下文对应小节；原始复现计划文本保持原样。
- 2026-10-07 契约同步：本文保留 B-light 的历史实现与取证结果，但不再把 Owner 确认补播视为
  当前契约要求，也不再把 `Backfill=1` 视为 Accepted 的成功条件。现行 Owner Fire Presentation
  定义以[网络射击 AI 自主验证契约](../架构/网络射击AI自主验证契约.md)为准。

## 1. 目标与边界

本计划完成问题记录 Phase C 的第一步：为 H-A1～H-A3 建立可归因的复现夹具与逐端证据。
本轮不实施修复；若复现成立，另行提出修复方案并按验证契约第 5 节请求用户确认预测边界取舍。

范围外：

- 不统一 HUD 显示入口（Phase D）；
- 不引入服务器时间相位补偿（Phase B）；
- 不修改武器射速、伤害、Projectile 与服务器事务；
- 不把夹具新增的观测字段当成新网络协议；
- 不覆盖丢包、断线、重生矩阵，除非复现必须。

## 2. 需要复现的风险

- H-A1：只按 `MagazineAmmo` 差值确认预测消费。
  服务器在同一复制帧内完成「射击 → 补弹」并回到上次可见值时，客户端收不到 OnRep，
  `PendingPredictedShots` 可能永久残留，本地预算与权威弹药失配。
- H-A2：迟到 Reject 的退款可能缺少原消费上下文。
  客户端在 Reject 到达前已经结束预测实例并清空 `CachedWeapon` 时，
  `GA_Fire::EndAbility` 的 Rejected 退还分支没有武器可退，Pending 残留。
- H-A3：服务器已接受并提交射击时，本地可能因过期 Ammo 预算拒绝本地预测表现。
  Owner 因此没有对应的本地动作边界与表现，形成 Invariant 1 的单向缺口。

## 3. 复现设计

统一夹具：Listen Server + 1 个远端拥有者客户端，另加一个不参与输入的本机观察视图。
所有客户端输入都走生产入口 `DoStartFiring / DoStopFiring`，不直接调用 `Weapon.Fire`
或服务器实现。夹具只在服务器用权威事务准备起点弹药，或构造服务器已知的最小状态。

### 3.1 H-A1：同值快照（射击 → 同帧补弹）

- 起点：权威与客户端镜像同为 1 发，Pending 为 0。
- 服务器在权威弹丸生成回调里立即把权威弹药恢复到步骤起点值，使 1 → 0 → 1
  落在同一个复制帧内；中间值不进入 Actor 复制。
  生产 Rifle 的 MagazineSize 大于 1，`ReloadFromReserve` 会补满到容量而不是只补 1 发；
  夹具因此用既有测试弹药钩子恢复精确原值，只构造「权威结果回到上次可见值」这一条件，
  不复制生产判定，也不由夹具自身给出通过结论。
- 观测：
  - Server：权威射击 +1、Projectile +1、最终 MagazineAmmo = 1、Reserve 减少 1；
  - Owner：镜像仍为 1（未收到 MagazineAmmo 变化）、`PendingPredictedShots` 应收敛到 0。
- 判定：固定窗口后 Pending 为 0。
  若保持 1，即复现 H-A1；不需要以丢包为前提。

### 3.2 H-A3：预算否决 Owner 预测表现

- 承接 3.1 的残留状态：镜像 1、Pending 1，本地预测可用弹药为 0。
- Owner 通过生产输入再次开火；服务器弹药仍为 1，能够接受这一发。
- 观测：
  - Server：权威射击 +1、Projectile +1、弹药 1 → 0；
  - Owner：本地预测反馈增量应为 1，本地预测可用弹药不应被过期镜像或残留 Pending 否决。
- 判定：存在权威射击时 Owner 预测反馈增量必须 >= 1。
  当前实现若为 0，即复现 H-A3；该判定与 3.1 的结论分开记录，互不代替。

### 3.3 H-A2：迟到 Reject 的预测消费退还

- 起点：双方弹药同为 10、Pending 0。
- 服务器给 Subject 的 ASC 挂一个**不复制**的 `State.Reloading` Loose Tag，
  代表「服务器已知、拥有者尚未收到」的最小阻塞状态；客户端本地状态完全未知。
- 服务器请求客户端通过生产输入开火：
  - 客户端本地门控通过 → 形成一次本地预测消费与一次本地表现；
  - 客户端 Press 与 Release 同帧，Reject 到达前本地实例已经 EndAbility；
  - 服务器 `GA_Fire` Tag 门控拒绝本次激活，权威弹药与权威射击都不变化，
    并通过可靠 `ClientActivateAbilityFailed` 下发 Reject。
- 服务器观察到失败回调后移除该 Loose Tag。
- 观测：
  - Server：权威拒绝 +1、权威射击 +0、弹药保持 10；
  - Owner：本地预测反馈 +1、收到 Reject 后 `PendingPredictedShots` 应回到 0。
- 判定：Reject 后固定窗口内 Pending 为 0。
  若保持 1，即复现 H-A2。
- 该构造不需要依赖对称网络延迟制造竞态；`PktLag=100` 只作为复验，不作为前提。

## 4. 最小充分验证链

- 必须网络 E2E：`IsAmmoPredictionContext()` 只对「非权威 + 本机拥有者视图」成立，
  Editor Automation 的 Standalone 世界是权威端，无法进入 Pending 维护分支；
  三条风险都跨越 Client Prediction 与 Server Reject / Authority Commit。
- 复用现有网络夹具：`AShooterNetworkTestCoordinator` 的无头 Listen 会话，
  新增独立 flag 分支与只读观测，不改旧阶段行为。
- 运行两次：正常网络条件与 `PktLag=100`，比较同一构造下的结论是否一致。
- 每个场景同时记录服务器权威增量与 Owner 本地观测，不用动画或 HUD 视觉替代事务证据。

## 5. 不变量覆盖确认

### Invariant 1 Owner Immediate Feedback

- 含义中本次改动触及的维度：本地预测表现是否被过期 Ammo 预算否决（H-A3）；
  迟到 Reject 后本地预测状态是否收敛（H-A2）。
- 含义中本次改动不应改变的维度：本地动作边界的产生条件与来源；
  Owner 表现路径；Reject 不回滚、不补播已播出的表现。
- 需要的新增证据：权威已接受射击时 Owner 预测反馈增量；
  Reject 后 Pending 与本地 Ability 状态。
- 明确不在本次范围的维度与理由：画面、声音、Recoil 的主观质量不属于无头夹具判定范围。

### Invariant 2 Local Prediction Obeys Weapon Rules

- 含义中本次改动触及的维度：本地预算门控与收敛语义。
- 含义中本次改动不应改变的维度：半自动射速节拍、FullAuto 节奏、
  不得用可能过期复制状态二次否决已经成立的本地有效动作。
- 需要的新增证据：Reject 后 Pending 归零；同值快照后预算与权威弹药一致。
- 明确不在本次范围的维度与理由：本轮不修预测规则，只取证；
  射速与输入节拍由既有 Automation 覆盖。

### Invariant 3 Server Is Final Authority

- 含义中本次改动触及的维度：Reject 不产生权威结果；权威弹药只由服务器修改。
- 含义中本次改动不应改变的维度：Ammo、换弹事务、Projectile 的权威边界。
- 需要的新增证据：每场景的权威射击、权威弹药与 Projectile 增量。
- 明确不在本次范围的维度与理由：Hit / Damage / Death / Score 不在本轮构造中。

### Invariant 4 Remote Is Confirmed Only

- 含义中本次改动触及的维度：无新增远端表现通道。
- 含义中本次改动不应改变的维度：远端表现只来自服务器确认。
- 需要的新增证据：服务器 Reject 时远端与拥有者都不产生新的权威结果或补播。
- 明确不在本次范围的维度与理由：远端第三人称表现质量与动画相位另有阶段覆盖。

### Invariant 5 Exactly One Authority Result

- 含义中本次改动触及的维度：迟到 Reject 不得补出第二次权威请求；
  同值快照不得让一次本地消费被重复确认。
- 含义中本次改动不应改变的维度：一次认可动作只产生一份权威结果。
- 需要的新增证据：场景内权威射击 / Projectile / 扣弹增量与本地预测发数对应。
- 明确不在本次范围的维度与理由：换弹事务重复提交已由 Phase A 网络记录覆盖。

## 6. 证据与判定规范

- 每个场景输出固定标记：场景开始、服务器权威增量、Owner 本地观测、结论。
- 复现成立时输出可搜索的 mismatch 证据，不输出 PASS；
  夹具自身失败（超时、目标失效）输出失败标记，不得折算为复现。
- 证据日志保存在 `Saved/Automation/Network/<时间戳>/`；
  结论、覆盖场景与遗留风险写入本轮开发记录。
- 测试通过前不提交生产修复；复现夹具与后续修复可以拆成独立提交。

## H-A2 修复的不变量覆盖确认

用户于 2026-10-05 确认本阶段只修 H-A2 迟到 Reject 退款，不扩大预测边界；
H-A1 与 H-A3 保持复现夹具证据，另行决策。

### Invariant 1 Owner Immediate Feedback

- 含义中本次改动触及的维度：Reject 后本地预测状态跨 `EndAbility` 的收敛时点。
- 含义中本次改动不应改变的维度：本地动作边界与表现来源；Reject 不回滚、不补播已播出的表现。
- 需要的新增证据：LateReject 场景 Reject 后 `PendingPredictedShots` 归零，
  且拥有端预测反馈仍只提交一次。
- 明确不在本次范围的维度与理由：H-A1 同值快照与 H-A3 预算门控本轮不改。

### Invariant 2 Local Prediction Obeys Weapon Rules

- 含义中本次改动触及的维度：误预测的收敛路径（退还）。
- 含义中本次改动不应改变的维度：本地射速与节拍、激活门控、不得预测或写权威弹药。
  PredictionKey 只用于归因本次激活的本地预测消费，不扩大预测内容。
- 需要的新增证据：被拒激活的退款发数与本地预测消费发数相等，且没有额外请求或表现。
- 明确不在本次范围的维度与理由：FullAuto 分段归因与旧快照预算保持现状。

### Invariant 3 Server Is Final Authority

- 含义中本次改动触及的维度：无；退款只改拥有端本地 Pending。
- 含义中本次改动不应改变的维度：Reject 不产生权威结果；不写 MagazineAmmo / ReserveAmmo。
- 需要的新增证据：场景中服务器权威射击为 0、权威弹药保持不变。
- 明确不在本次范围的维度与理由：服务器事务与换弹提交不改。

### Invariant 4 Remote Is Confirmed Only

- 含义中本次改动触及的维度：无；不新增远端表现通道。
- 含义中本次改动不应改变的维度：远端表现只来自服务器确认。
- 需要的新增证据：观察端在该 Reject 场景没有新增权威结果或表现。
- 明确不在本次范围的维度与理由：远端动画与音效质量不属于本修复。

### Invariant 5 Exactly One Authority Result

- 含义中本次改动触及的维度：一次误预测只允许一份退还。
- 含义中本次改动不应改变的维度：一次认可动作只产生一份权威结果。
- 需要的新增证据：Reject 委托与 `EndAbility` 两条路径不重复退款；
  退款数量等于本次激活已预测消费数量。
- 明确不在本次范围的维度与理由：权威事务计数不变。

## H-A2 修复实现约束

- 只在拥有端本地预测路径登记退款上下文，按激活 PredictionKey 索引；
  服务器与观察端不新增复制字段或 RPC。
- 退款只调用既有 `RefundPredictedAmmo`，不写任何权威字段。
- 两条到达路径（PredictionKey Rejected 委托与 `EndAbility` Rejected）共享幂等入口。
- 上下文只弱引用 WeaponActor，并在新激活时清理已结清与超额条目。

## B-light 实现的不变量覆盖确认

用户于 2026-10-05 确认实施 B-light：按 `GA_Fire` Activation 对账 Owner 预测与服务器结果，
服务器结果领先时按当时实现补播一次「已确认」表现；不扩大预测内容。
该段记录的是历史实现方案，不改变现行契约：Unpredicted + Accepted 不得延迟补播 Owner 瞬时反馈。

### 实现范围

- `AShooterWeapon` 增加 OwnerOnly 的最近 4 轮 `FShooterFireActivationResult` 环：
  `{ActivationKey, ProcessedShots, bSettled, ActivationSerial}`；
  `ProcessedShots` 只在服务器 `ConsumeAmmo` 真实提交成功后递增，
  `bSettled` 在服务器 `EndAbility` 置位；不记录 per-shot id。
- 拥有端 `GA_Fire` 按 ActivationKey 维护账本：
  预测发数、已见服务器结果、已补播、已对账预测、补播序号游标与本地尝试序号游标。
  `PendingPredictedShots` 不再由 `OnRep_MagazineAmmo` 吸收或重订。
- 结清只由账本完成：
  - `Processed <= Predicted`：确认对应预测发数并清空剩余 Pending，不补播；
  - `Processed > Predicted`：历史 B-light 实现曾按缺口调用
    `PlayOwnerConfirmedShotFeedback()` 补播恰好一次；该历史计数不再是当前 Owner 契约的成功条件；
  - 本地已预测、服务器最终未提交的发数在结清时按 phantom 清理，不补表现。
- 服务器是否认可本次 Activation 以 `OnConfirmDelegate` / Rejected 为准，
  CaughtUp 不再充当接受证明。
- 不新增 per-shot RPC / ShotId、不预测空弹、不推测换弹转移、不增加表现猜测，
  不改 HUD / Phase B / Projectile / Damage / Manager / Subsystem。

### Invariant 1 Owner Immediate Feedback（历史 B-light 证据，不改变现行定义）

- 触及：H-A3 的服务器领先窗口曾由「确认补播」补齐；H-A1 同值快照不再依赖数值吸收。
- 不改变：本地动作边界的产生来源；Reject 不回滚也不补播已播出的预测表现。
- 新增证据：历史 `ConfirmedBackfill` 场景记录过 3 发权威射击、0 发预测反馈与 3 发确认补播；
  当前契约要求该路径改按 `Unpredicted + Accepted` 验收为 Owner immediate feedback = 0、
  Confirmed replay = 0、Authority Shot = 3。
- 不在范围：画面、声音与后坐力的主观质量。

### Invariant 2 Local Prediction Obeys Weapon Rules

- 触及：`PendingPredictedShots` 只由 Activation 账本确认、phantom 清理、Reject 退还
  与生命周期复位修改，不再跟随 `MagazineAmmo` 差值。
- 不改变：本地射速与节拍、弹药门控；预测仍不写任何权威字段。
- 新增证据：SameValueSnapshot 收敛（Mag=1、Pending=0、Resolved=1）；
  PredictedAccepted 保持 1 发预测反馈、0 发补播。
- 不在范围：空弹预测与换弹期间预测仍被禁止，未扩大预测边界。

### Invariant 3 Server Is Final Authority

- 触及：无；`ProcessedShots` 只读服务器提交计数，客户端不反写。
- 不改变：Ammo、Reload、Projectile 的权威边界。
- 新增证据：每个场景的权威射击、权威弹药与 Projectile 增量由服务器日志给出。
- 不在范围：Hit / Damage / Death / Score。

### Invariant 4 Remote Is Confirmed Only

- 触及：无；新结果记录为 `COND_OwnerOnly`，不进入远端表现通道。
- 不改变：远端表现只来自服务器确认。
- 新增证据：补播只在拥有端目标执行；本夹具不构造远端第三人称表现。
- 不在范围：远端动画与音效质量。

### Invariant 5 Exactly One Authority Result

- 触及：一次 Activation 的每发服务器 Shot 只能对应一次预测或一次补播。
- 不改变：一次认可动作只产生一份权威结果。
- 历史证据：PredictedAccepted 无补播；ConfirmedBackfill 补播数等于缺口数。
  按现行契约，Accepted 不要求补齐 Owner 表现，后续测试应使用四种 Owner outcome 口径；
  LateReject 恰好一次退还且账本结清；ReloadLifecycle 无未结清账本。
- 不在范围：权威事务计数不变。

### B-light 实现约束

- OwnerOnly 结果记录只承担服务器提交计数与结束标记，不承担预测账本；
  预测账本只存在于拥有端 `GA_Fire` 实例内。
- 历史补播只调用 `PlayOwnerConfirmedShotFeedback()`：
  不消费 Pending、不推进本地节拍、不发 RPC、不生成 Projectile、不写 Ammo；该入口的历史存在
  不构成当前契约下的 Owner 播放义务。
- 历史同一次 Activation 的补播游标单调向前；
  本地节拍追上已被服务器确认补播的序号时跳过该次本地尝试，不重复播放。
- 账本在新激活时清理已结清及失效条目；有效未结清账本等待可靠最终通知。
  2026-10-05 收敛修正：不再以未结清上限 4 提前清除 Pending；
  `FPredictionKey::Current` 为 int16，槽位键以 int32 存储，不按数值大小判断新旧。
- 生命周期边界（Owner 变化、归还池、Destroy、重新绑定）清空结果环与 Pending，
  防止上一持有者状态污染下一轮。

## 7. 实施顺序

1. 新增 `-ShootGameAmmoPredictionTest` 夹具分支与三个场景驱动（只加测试代码与观测）。
2. 编译并运行正常与 `PktLag=100` 两个 Listen 会话。
3. 逐条记录复现 / 排除 / 证据不足。
4. 按证据提出修复候选与预测边界影响，用户确认后再实施修复。
