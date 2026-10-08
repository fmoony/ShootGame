# HUD 弹药预测执行计划

- 日期：2026-10-06；基线：`023d76d` 与前两轮保留的未提交收敛改动。
- 用户授权持续推进到 HUD 预测，已确认编辑器内容保存、本轮不自行更改。
- 本轮完成 S-H1：统一弹药显示入口与事件，覆盖开火预测、拒绝及最终纠正。

## 基线与外部依据

当前 Weapon、Character、Controller 三处刷新均读取真实 MagazineAmmo / ReserveAmmo。
预测消费与 Reject 没有 HUD 通知；预算 Pending 由 Activation 结果结清。
可靠最终结果与弹药属性可先后到达，直接显示 MagazineAmmo - Pending 会重复扣减或回升。

Epic 文档说明不同属性 RepNotify 没有固定顺序，关联数据可组合到结构。
UMG 优化文档建议由事件刷新；本轮沿用项目已有 Character → Controller → Widget 事件链。
本机 UE5.6 DataReplication.cpp 的 RepNotify 分发用于核对安装版本。
官方网页当前为 UE5.8；不把当前项目自定义弹药方案写成 Epic 强制架构。

- [Epic 复制顺序](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicated-object-execution-order-in-unreal-engine)
- [Epic UMG 事件刷新](https://dev.epicgames.com/documentation/en-us/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine)

## 最小显示协议

- 权威 Ammo 及既有预测预算保持原语义；显示使用独立的每 Activation 扣减记录。
  只在真实本地预测 Shot 成立时扣显示，不把服务器补播计为新的本地消费。
- 服务器提供一个 OwnerOnly 显示快照：Revision、Magazine、Reserve，整体序列化。
  Revision 在 Actor 生命周期内不回退，拒绝迟到旧快照覆盖新的显示基线。
- 每轮已有可靠最终通知携带该时点显示快照，不增加 per-shot RPC。
  最终通知原子地移除该轮显示扣减并更新基线；其他未结清轮的扣减继续保留。
- 活动显示记录存在时，属性快照暂存；全部结清后采用最新快照。
  避免属性先到时重复扣减，也避免结果先到时把旧弹药显示回去。
- Reject 清除对应显示扣减；Owner 清理作废全部显示记录。
  切枪保留有效旧轮结清，但旧武器不能向当前 HUD 推送。
- Weapon Getter / 推送、Character 激活及修复、Controller 初始绑定都使用统一显示值。
  Host / Standalone 直接显示本机权威结果，不维护客户端显示扣减。
- 换弹期间不预测补满或备弹转移；仅在服务器提交的新快照到达后显示转移。
  无新增输入恢复、射速调整或远端 Reload 相位字段。

## 五不变量含义覆盖确认

### Invariant 1 Owner Immediate Feedback

- 触及：拥有端真实预测 Shot 同帧更新弹药 HUD，不等待确认。
- 保持：四路射击反馈来源与现有预算门控，HUD 不触发反馈或 Gameplay。
- 新证据：真实 Widget 输入与同一客户端预测、确认日志时序，HUD 先扣减。
- 排除：B-light 的 Owner 确认补播属于历史实现；按现行契约，未预测但被接受的 Shot 不补播 Owner
  瞬时反馈，本计划只验证 HUD / Ammo 状态结清，不把 Owner cosmetic backfill 作为成功条件。

### Invariant 2 Local Prediction Obeys Weapon Rules

- 触及：显示扣减只跟随真实预测，Reject 及 phantom 不遗留显示债务。
- 保持：本地节拍、空弹、Reloading / Equipping / Dead 门控及服务器弹药预算。
- 新证据：不同到达顺序无重复扣减，拒绝退款，重复消息幂等，过期上下文作废。
- 排除：不预测换弹后的补满数量；不扩大空弹时的本地反馈资格。

### Invariant 3 Server Is Final Authority

- 触及：HUD 镜像与服务器真值分离，最终基线由服务器提供。
- 保持：Ammo、Reload 转移、Projectile、Hit / Damage / Death / Score 的权威入口。
- 新证据：预测期显示变化而 Magazine / Reserve 未改；接受与拒绝均最终收敛。
- 排除：伤害、计分及换弹时间协议没有变动，不新增其专用测试矩阵。

### Invariant 4 Remote Is Confirmed Only

- 触及：显示快照仅复制拥有者，本地 Widget 只属于本地 Controller。
- 保持：远端四路表现仍经服务器确认，HUD 不参与远端表现分发。
- 新证据：观察者无显示预测入口；旧武器不能覆盖当前 Widget。
- 排除：无头计数不能证明动画、声音和屏幕布局的主观质量。

### Invariant 5 Exactly One Authority Result

- 触及：刷新 Widget、重复最终通知与属性通知不触发额外射击或弹药事务。
- 保持：现有权威提交与弹丸唯一入口，每轮一次可靠最终通知。
- 新证据：同值快照、单发接受、迟到 Reject、FullAuto 多轮收敛且无 HUD 双扣。
- 排除：PredictionKey 回绕及完整网络重生 / 回池矩阵保持已登记的边界。

## 验证链

- 先用独立 Automation 构造属性先到、最终先到、并行 Activation、Reject 与旧消息。
- 复用真实 Widget 资产验证数值入口和备弹文本，不创建第二套 HUD。
- 网络六场景增加 Widget 显示采样、即时扣减与终态断言。
  Dedicated 高延迟丢包、Listen 主机加两客户端分别验证结清与角色边界。
- 编译、全量 Automation、正式文本与 include 门禁；新源码结构只刷新一次 VS 文件。
- 所有失败保留；最终结果、缺失证据和遗留项另记本轮开发记录。

## 执行结果

- 新增显示网络协议与源码结构，最终采用完整 RunAll 七阶段回归。
  汇总 `Saved/Automation/Runs/20261006_105829/Summary.json` 全部 Passed。
- Automation 148 项成功（89 无警告、59 带警告），Failed=0、NotRun=0。
  新增到达顺序、Reject / 生命周期测试均 Success；旧武器隔离与真实 Widget 测试通过。
- 最终 HUD 网络：Dedicated `20261006_110255`、Listen `20261006_110425`。
  每轮 Cases=6 Converged=6 Mismatches=0，全部 HUD 终态与服务器一致、无未结清记录。
  Dedicated PktLag=1000 / PktLoss=2；首枪显示比确认早 2042ms / 41ms。
- 修复已知旧裁决误清复用 key 新显示记录；先失败的回归日志保留。
  版本防御只拒绝比已知基线旧的裁决，不替代完整 PredictionKey 回绕协议。
- S-H1 的开火 HUD 显示闭环完成；权威 Ammo、预算资格与换弹转移未扩大。
  Phase B 相位补偿、换弹补满预测、Magazine 时间跳转仍有遗留边界；
  B-light 的 Owner 确认补播属于历史实现，不再是当前 Owner Fire Presentation 契约要求。
- 五不变量逐项结论、失败经过与最终证据见
  [本轮开发记录](../开发记录/2026-10-06-1047-HUD弹药预测与显示结清.md)。
  当前未提交；保留前两轮未提交改动与记录，不自动创建提交。
