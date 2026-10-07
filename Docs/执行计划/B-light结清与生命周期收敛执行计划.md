# B-light 结清与生命周期收敛执行计划

- 日期：2026-10-05；基线：`023d76d`；引擎：本机 UE 5.6.1。
- 用户已授权收敛审计发现的问题；本轮不进入 Phase B 或 Phase D。
- 2026-10-07 契约同步：本文保留 B-light 的账本、PredictionKey 结清、Refund、HUD 与生命周期
  证据，但其中“Owner 确认补播”不再是现行 Owner Fire Presentation 契约。当前契约见
  [网络射击 AI 自主验证契约](../架构/网络射击AI自主验证契约.md)；
  `Backfill=1` 不再是 Accepted 的成功条件。

## 实现依据与范围

当前四槽属性环不能保证每轮最终结果被观察，旧账本也未随武器租用作废。
官方资料说明属性是状态同步，可靠 Client RPC 可用于拥有连接的最终结清。
本机 `NetDriver.cpp` 将 `FUNC_NetReliable` 写入可靠 bunch。
本机 GAS 按激活 PredictionKey 匹配确认与拒绝，不替自定义弹药维护账目。

- [Epic RPC](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine)
- [Epic 复制顺序](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicated-object-execution-order-in-unreal-engine)

官方网页当前为 5.8；实际实现按本机 UE5.6 头文件与源码核对。
以下为项目缺口驱动的设计，不声称 Epic 规定必须采用此结构。

- 保留四槽属性环提供活动轮次进度，增加每 Activation 一次可靠最终结果通知。
- 不通过槽位覆盖推测最终发数，不任意淘汰仍等待裁决的有效账本。
- Owner 解绑时作废相应账本；账本记录本地上下文代次并校验结果来源武器。
- 切枪和动作取消停止旧确认表现，预算仍按最终结果结算。
- 批量确认表现按武器间隔排队；不新增射击请求、不推进本地射击节拍。
- 无法提交的反馈明确记录为抑制，不能把失败调用计作已播放。

## 不变量含义覆盖确认

### Invariant 1 Owner Immediate Feedback

- 触及：历史确认补播账本及其 Owner 播放入口的契约解释。
- 不改变：本地预测反馈入口、预算边界、PredictionKey 结清与已播表现不回滚。
- 新增证据：预测与未预测路径分开记录；Owner confirmed replay 不再作为 Accepted 的补偿
  条件。
- 排除：本文历史网络日志中的 `Backfill` 计数仍表示当时实现事实，
  不代表当前契约要求；主观体验需人工验收。

### Invariant 2 Local Prediction Obeys Weapon Rules

- 触及：裁决未达时账本保留，租用边界预算隔离，确认队列不挤占同一发预测。
- 不改变：空弹和换弹转移不预测，不写权威弹药。
- 新增证据：超过四轮且停止输入后 Pending 收敛；旧退款不减少新 Pending。
- 排除：极长会话 PredictionKey 回绕另行验证，本轮不新增射击身份协议。

### Invariant 3 Server Is Final Authority

- 触及：服务器发布最终结清的传输路径。
- 不改变：Processed 仍只在真实扣弹后递增；客户端仅维护预算与表现。
- 新增证据：Accept 的真实发数与最终计数对应，Reject 无权威结果。
- 排除：Hit、Damage、Score 未改；不扩大为这些结果的专项验收。

### Invariant 4 Remote Is Confirmed Only

- 触及：新增 Client RPC 的拥有连接边界。
- 不改变：远端确认表现仍沿既有通道，不改变其可靠性承诺。
- 新增证据：多客户端结果通知只到拥有者；观察者补播计数保持零。
- 排除：远端主观画面与音效质量不由无头测试判定。

### Invariant 5 Exactly One Authority Result

- 触及：最终 RPC 与属性回流去重、排队反馈与生命周期取消。
- 不改变：权威射击次数、扣弹与弹丸路径，不为补播创建权威请求。
- 新增证据：重复最终结果不重复对账或入队，切枪与回池清理幂等。
- 排除：不承诺切枪后回放所有旧表现；旧目标失效时抑制并保留明确证据。

## 验证链

- Automation：生产账本跨四轮、重复结果、回池旧退款、批量队列与隐藏门控。
- 网络：原五场景、真实输入多轮结清、Dedicated 双客户端及延迟丢包。
- 加强原夹具：逐路反馈与实际 Fire 活动、Timer 活动证据。
- 编译、版式与 include 门禁；实际结果写入本轮开发记录。
## 实施结论

- 本计划列出的 B-light 收敛改动已实现；五不变量的排除边界保持有效。
- 全量回归七阶段通过；最终生产实现 Automation 共 144 项，0 失败。
- Listen 主机加两客户端与 Dedicated 双客户端分别六场景全部收敛。
  Dedicated PktLag=1000 / PktLoss=2：账本未结清峰值 7，八轮最终通知全部到达。
- 11 文件版式门禁 0 警告；include 与 diff 检查通过；未自动提交。
- 已解决状态、失败会话排除与启动就绪新发现见问题记录第 9 / 10 节。
- 完整证据：`Docs/开发记录/2026-10-05-1840-B-light最终结清与生命周期收敛.md`。
