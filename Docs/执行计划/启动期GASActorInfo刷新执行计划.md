# 启动期 GAS ActorInfo 刷新执行计划

- 日期：2026-10-06；基线：`023d76d` 加上一轮未提交的 B-light 收敛改动。
- 用户确认编辑器内容已保存，本轮不会自行修改；本轮仅完成一个初始化闭环。

## 问题与实现依据

上轮压力日志 `Saved/Automation/Network/20261005_184923/Client1.log` 记录：
武器已就绪，单帧 Fire 未建立激活，GAS 报 LocalPredicted ability not local。
此前增加的夹具就绪等待避免误测，但没有修复生产初始化顺序。
旧日志未记录具体缓存 Controller；本轮缺陷复现不等于唯一归因该历史失败。

当前 Character 只在 PossessedBy / OnRep_PlayerState 初始化 ASC。
相同 Avatar 会直接返回，未刷新后到的 PlayerController。
本机 UE5.6 ActorInfo 从 Owner 链查找并缓存 PlayerController。
PlayerState 早到而它的 Owner 晚到时，已有 Avatar 不代表本地上下文已完整。

官方 RefreshAbilityActorInfo 文档说明，它保留 Avatar 并重新查找 PlayerController 等引用。
本机 UE5.6 核对该函数仅刷新 ActorInfo，不重新初始化 Health，也不清项目输入意图。
官方网页当前为 UE5.8；实际签名及执行行为按本机 UE5.6 核对。

- [Epic 刷新 ActorInfo](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/GameplayAbilities/UAbilitySystemComponent/RefreshAbilityActorInfo)

## 最小实现

- Character 在 Controller 复制和 PawnClientRestart 后补做幂等上下文初始化。
- 相同 Owner / Avatar 只刷新 ActorInfo；新 Avatar 保持既有初始化及输入作废行为。
- PlayerState Owner 后到时刷新已建立的 ASC，补齐 Owner 链上的 PlayerController。
- 不新增输入队列、不重放过期按下沿、不绕过 Dead / Reloading / Equipping 门控。
- 不修改权威弹药、预测预算、补播、HUD、Reload 相位或资产。

## 五不变量含义覆盖确认

### Invariant 1 Owner Immediate Feedback

- 触及：本地上下文已关联但 ASC 缓存未更新时，首个输入无法启动本地动作。
- 保持：本地预测仍是即时表现来源，服务器确认不得代替预测入口。
- 新证据：晚到 Owner / Controller 后，首次单帧输入只激活一次。
- 排除：B-light 未预测发仍是确认补播，原即时性缺口不由初始化修复消除。

### Invariant 2 Local Prediction Obeys Weapon Rules

- 触及：初始化刷新不应丢掉同 Avatar 的 Held / Buffered 输入。
- 保持：本地射速、动作 Tag、输入过期及新 Avatar 作废规则。
- 新证据：相同 Avatar 刷新保留输入，新 Avatar 清除旧输入，不重复激活。
- 排除：首次 Owner / Controller 关联之前的输入不新增恢复协议。

### Invariant 3 Server Is Final Authority

- 触及：首个合法本地请求能进入原 GAS 服务器裁决路径。
- 保持：Health 初始化仅在新 Avatar 发生，Ammo / Projectile 等结果仍由服务器产生。
- 新证据：同 Avatar 刷新不重置受损 Health；首枪权威发数及弹丸各为一。
- 排除：不修改伤害、死亡、得分或换弹提交协议。

### Invariant 4 Remote Is Confirmed Only

- 触及：远端 Pawn 生命周期也会调用刷新，不能获得本地玩家输入资格。
- 保持：Owner 链解析遵循引擎，不把 Avatar 的远端控制器冒充本地玩家。
- 新证据：无本地 Controller 的上下文保持非本地；观察者无新增拥有端反馈。
- 排除：远端动画、音效的主观质量不属初始化闭环。

### Invariant 5 Exactly One Authority Result

- 触及：重复 Controller 通知和 ClientRestart 不能重新初始化或补发输入。
- 保持：已有 Ability 实例、输入处理及权威提交入口。
- 新证据：重复刷新不重复消费 Press，不重置 Health，不清同 Avatar 输入。
- 排除：不新增逐发身份或 PredictionKey 回绕协议。

## 验证链与证据缺口

- 原夹具等待 ASC 本地就绪，不能证明原始启动顺序已修复。
- 运行时 Automation 构造 Owner 晚到、重复回调及 Avatar 更替，直接驱动生产入口。
- 定向网络使用原六场景，启动模式只等待真实 Pawn / Controller 和 Ability Spec 就绪。
  首枪不得等待 ASC 缓存自愈；记录真实本地 ActorInfo 后提交单帧 Press / Release。
- Dedicated 高延迟丢包验证真实复制顺序，Listen 验证主机与观察者隔离。
- 编译及定向 Automation 足以覆盖规则；本轮不重复七阶段完整回归。
- 保存实际失败与成功报告，不用历史整体绿色代替本轮证据。

## 执行结果

- 已完成上述三个刷新入口及相同 Avatar 的刷新分支，无新源码或资产。
- 最终编译成功；Automation 共 146 项，Passed=87 / Warnings=59 / Failed=0。
  报告：`Saved/Automation/Reports/20261006_102607_ShootGame/`。
- Dedicated 双客户端 PktLag=1000 / PktLoss=2 六场景收敛，零不匹配。
  日志：`Saved/Automation/Network/20261006_102647/`；未结清峰值为 7，最终为 0。
- Listen 主机加双客户端六场景收敛，零不匹配。
  日志：`Saved/Automation/Network/20261006_102826/`；主机和观察者均无拥有端补播。
- 两轮首次短按均不等待 ASC 缓存就绪，首枪权威射击与弹丸各为 1。
  Dedicated 日志明确显示预测早于服务器确认；重复刷新和输入保留由 Automation 覆盖。
- Owner 的初版测试被临时世界分发门控挡住，不能计为该入口的修复前独立证据。
  失败报告、测试修正与完整结论见本轮开发记录。
- 启动期上下文闭环完成；B-light 原即时表现缺口及启动前输入恢复不在本轮解决范围。
- 记录：`Docs/开发记录/2026-10-06-1010-启动期GAS本地上下文刷新.md`。
