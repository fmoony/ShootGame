# 方案 C 生产 AnimGraph 失效根因审计

日期：2026-10-10。范围：根因审计、临时图验证；不执行正式迁移。
引擎：本机 UE 5.6.1，CL 44394996。

## 结论与证据等级

1. **已证明：自定义节点漏执行 ExposedValueHandler，导致 Alpha 保留默认 0。**
   节点仍然 Update、Evaluate，并透传 Source；不能把没有诊断日志解释为节点未求值。
   保留 bool 转换、仅恢复 handler 执行即可获得实际 Fire 姿态。
   仅改为直接 float 输入，漏执行 handler 的问题仍然存在。
2. **已证明：Owner 整个上半身层置零会切断 WeaponAction / Reload 更新。**
   隔离负例精确复现网络视图 2→1、单机视图 1→0。
   Reload、ReloadRecovery 和 Magazine 三项同时丢失。
   历史迁移工具明确采用过该策略，因此它是确定的错误机制。
3. **尚未证明：历史“只改 Slot 名就恢复 Reload”的唯一因果链。**
   历史失败版本的完整 C++ 快照缺失，各次图和权重策略也发生过变化。
   不能将上述机制冒充每一次历史失败的唯一原因，更不能笼统归结为 Slot 冲突。
4. **保留自定义节点作为实验候选，不投入生产。**
   唯一推荐候选是保持 WeaponAction 层、按角色互斥消费同名 Arms。
   Owner 在 IK 后消费；Observer 保留原生 IK 前消费。
   本轮仍未关闭即时换枪清理、完整输入连发取消和历史改名因果缺口。

## 当前基线与保护范围

本轮开始和结束分别记录 SHA256；见证据目录的 baseline_hashes.json、final_hashes.json。
正式 TP 资产的 Git blob 与 HEAD 相同：

```text
81036eae3897efeeb098485df59c46610f62f149
```

FP 资产、ShooterCharacter.cpp 是任务开始前已有的方案 A 工作区修改。
它们的 SHA256 在本轮保持不变；历史迁移工具、编辑器节点和旧取证夹具也未改动。
正式 TP、FP、武器资产均未写入。
未修改生产 Fire、GAS、RPC、弹药或权威射击代码；未提交、未推送。

历史回滚报告 20261009_220848_ShootGame 确有 174 条 Success、0 条失败。
其中 succeeded=97，succeededWithWarnings=77；不能把 174 条说成全部无警告。
本轮没有重跑该大型集合，只运行两个相关 PIE 及其隔离变体。

## 图拓扑与原型差异

原始生产图的真实输入不同，不能把两个状态机当成同一个来源：

```text
LocomotioStateMachine ────────────────────────> Layer.Base
WeaponAction -> Arms Slot ────────────────────> Layer.Upper
Layer -> Aim IK -> LeftHand IK -> LocalPose -> Root
```

历史运行 CSV 可证明失败图 Root 实际指向自定义节点，Source 指向 IK 后的转换节点。
后期记录中，改名的是上游原生 Slot，名称为 ArmsOwnerPredicted。
后置自定义节点仍消费 Arms；这并不是把两个消费端一起改成同名新 Slot。
旧迁移工具还明确假设 Owner 的 WeaponUpperBodyWeight 由 C++ 压为零。
当前已回滚 AnimInstance 则按是否装备武器设置该权重，没有该 Owner 特例。

另一个独立风险是工具的 SlotSource 恢复逻辑：
丢失 Source 时用 BlendBaseOutput 替代，但前者应为 WeaponAction，后者是 Locomotion。
UE 动画图 Schema 对 Pose 输出的新连接会断开已有连接。
历史日志出现过 SLOT_SOURCE_RECOVERED，说明该路径不能只靠注释认定安全。
本轮未证明这一风险就是最终失败版本的唯一直接原因。

旧 FPostIKFireMask 原型在运行时初始化之后替换链接。
它没有生产节点的 Alpha 编译绑定，也不经过同样的节点初始化与缓存注册流程。
它绕开原生 Slot 后直接使用 Slot.Source，并在运行时处理 Slot 注册。
因此原型成功不能证明正式 AnimNode 的 handler、注册、源图连接也正确。

## Alpha 与生命周期：单变量证据

节点属性确实进入 GeneratedClass，Root.Result 指向该运行时节点。
绑定日志记录 RootIsMask=1，Source 非空。
临时类 CDO 的角色输入默认 0；夹具在实例安装后设置 Owner=1、非 Owner=0。
实例目标 AnimClass 同步设为临时类，防止装备幂等修复把它替换回生产类。

LegacyBool 的编译产物存在 PropertyAccess handler 和非空 BoundFunction。
函数名指向该节点的 EvaluateGraphExposedInputs，不是无绑定的 Alpha 引脚。
LegacyFloat 的编译产物存在 CopyRecords，CopyIndex=3。
两者分别覆盖转换函数路径和直接属性复制路径。

| 对照 | Owner Alpha | U / E 上限 | 合成次数 | 右手增量 |
|---|---:|---:|---:|---:|
| LegacyBool | 0 | 658 / 658 | 0 | 0° |
| LegacyFloat | 0 | 659 / 659 | 0 | 0° |
| FixedBool | 1 | 658 / 658 | 39 | 7.274882° |

表中计数取各运行已记录样本的最大值，不是每个实例或全运行总和。
三种图的节点 Initialize=1、CacheBones=1。
FixedBool 与 LegacyBool 的关键差别只有是否执行 exposed-input handler。

UE 的 FPoseLinkBase::Update 调用节点 Update，并不替所有节点执行 handler。
原生 TwoWayBlend 显式执行 GetEvaluateGraphExposedInputs().Execute(Context)。
审计开始时自定义 Update 缺少这一调用，Alpha 自构造时起维持 0。
因此它跳过 Slot 权重查询；Evaluate 先求值 Source，再因 Alpha=0 返回。
原有日志位于遮罩构建等条件路径，不能用其缺席证明 Update/Evaluate 没发生。

四种情况应分开判断：

- 未更新：隔离复现的 Update 计数反证了这一说法。
- 提前返回：Alpha=0 导致合成前返回，已证明。
- 未求值：Evaluate 计数反证了这一说法；Source 仍被求值。
- 结果被覆盖：修正后节点直接连接 Root，实测最终 TP 与 CopyPose 均有贡献。

历史每一次失败运行没有这些入口计数，所以不能补造其精确次数。
历史源码缺陷、图连接记录和单变量复现共同支持 Alpha 根因。

## Slot、Proxy 与 Reload 机制

本机引擎实现区分三个层次：

1. 注册按 SlotName 建表；同名重复注册会警告，并不会创建第二个独立命名空间。
2. GetSlotWeight 按 SlotName 从 MontageEvaluationData 计算权重。
   它不把动画资源作为“用一次就耗尽”的对象。
3. UpdateSlotNodeWeight 写入同名 tracker；多个写者会覆盖权重，而非相加。
   SlotEvaluatePose 消费 Proxy 的求值快照，不应在并行动画线程遍历活的 MontageInstances。

Montage 更新及求值数据刷新先于后续动画图并行更新。
本节点 Alpha 为 0 时连权重查询都不执行，所以观测到 0 不是 Montage 未创建的证据。
Slot 注册缺失会影响 tracker 查询与全局权重，也不能与上述查询混为一谈。

原生 Slot 即使位于当前不活动分支，也会在图初始化时注册。
候选保留这个唯一注册者；自定义节点不重复注册。
运行时按角色只让一个 Arms 消费端参与合成，避免两个活动写者和二次 Fire 叠加。

Duplicate 负向候选同时保留 IK 前与 IK 后消费，两个 Reload PIE 仍通过。
这否定了“同名第二消费端必然导致 Reload 少视图”的判断。
它不能证明双消费安全，尤其不能证明 Fire 不会叠加或 tracker 写入顺序无影响。
本轮不推荐这个候选。

真正可稳定复现的 Reload 故障来自整个 Layer.Upper 权重归零。
该层装的是 WeaponAction，不只是 Fire Slot。
零相关性使对应状态机不参与常规更新和求值，Sequence Notify 也不再按原路径传播。

| 变体 | Listen 三项视图数 | Standalone 三项视图数 |
|---|---|---|
| 原图 / FixedBool | 2 / 2 / 2 | 1 / 1 / 1 |
| OwnerLayerZero | 1 / 1 / 1 | 0 / 0 / 0 |
| LayerZero（全部副本） | 0 / 0 / 0 | 0 / 0 / 0 |

三项依次是 Reload、ReloadRecovery、Magazine detach。
OwnerLayerZero 与 FixedBool 相比只替换层权重的角色策略。
它与历史 20261009_214711 的失败数量一致。
这是权重策略的因果复现，仍不等于找回了历史所有二进制与 C++ 状态。

改名可能改变注册表命中、tracker 写入者或活动链路，但没有成对证据能选出唯一解释。
历史后期原生注册名与自定义查询名不一致，更增加了变量。
若要关闭此缺口，应恢复当时完整源码和图快照，并只改变名称做配对测试。
本轮未写入历史 migrated 资产，也未将其载入生产路径。

## 唯一推荐的最小候选

```text
WeaponAction -> 编译生成的 SaveCachedPose
                 ├─ UseCache -> 原生 Arms Slot -> Observer 分支
                 └─ UseCache ─────────────────> Owner 分支
Locomotion + 角色选择结果 -> 原有 UpperBody Layer（装备权重不变）
                         -> Aim IK -> LeftHand IK
                         -> 后置 MaskedFireSlot（仅 Owner Alpha=1）
                         -> Root -> Owner FP CopyPose
```

缓存只放在 IK 之前的 WeaponAction。
SaveCachedPose 必须由 AnimBP 编译器登记更新顺序，不能只在运行时塞一个裸缓存节点。
后置节点对 Source 只 Update/Evaluate 一次，再复制所得姿态做遮罩混合。
Spine_01 / Depth=1 和 Mesh Space Rotation Blend 复用原上半身配置。
Observer Alpha=0，后置节点透传；保留确认 Fire 的原生 Slot 链路。

本轮 LeftHandIK 探针与后置节点的 Evaluate 计数增量始终相同。
累计差为 1，来自探针安装前已经发生的一次初始化求值。
没有因 Fire 分支再求值一次 LeftHandIK；Aim IK 也只处于该唯一上游链。
本轮直接计数覆盖 LeftHandIK，Aim IK 的结论来自拓扑，不伪称另有独立计数器。

本方案仅针对当前两枪的 additive Fire 与角色 Alpha=0/1。
不能外推为任意非 additive Montage、任意中间 Alpha 或任意 Slot Group 的通用节点。

## 姿态、Notify 与残留验证

临时 AnimBP 在 /Temp 包内复制并编译，SkipSave；没有写入 Content。
无命令行审计开关时，原 PIE 测试使用原生产图。
审计开关将 Owner 原测试 Fire 的表现重定向到 TP，原 Gameplay 请求仍由原测试触发。
额外 Rifle/Pistol 姿态脉冲直接播放 Cosmetic Montage。
这不是生产 0 RTT 路由或 GAS 连发入口已迁移的证据。

FixedPose_v4 的两个 PIE 均 Success，原 Reload 与 Magazine 断言未删除或放宽。
同帧采样在 TP 后置节点输出与 FP CopyPose 输出之间比较 hand_r 四元数：

| 模式 / 武器 | TP 增量 | FP CopyPose 增量 | 最大差 |
|---|---:|---:|---:|
| Listen / Rifle | 2.034412° | 2.034412° | 0.000002° |
| Listen / Pistol | 7.274817° | 7.274817° | 0.000002° |
| Standalone / Rifle | 2.034414° | 2.034414° | 0° |
| Standalone / Pistol | 7.274843° | 7.274843° | 0.000002° |

每组有 39～56 个满足同帧条件的 Fire 样本；pelvis 额外位移均为 0。
FixedBool_v3 在记录窗口的 Owner Slot 权重达到 0.866674。
这是真实节点查询值，但该窗口不是整个 Montage 的峰值采样窗口。
最终 FP Mesh 变换也被记录；上表测的是 CopyPose 边界，不是全部 FP 修正后的独立差分。
NullRHI 不提供可视阴影、遮挡或摄像机构图验收。

Rifle 三个 Cosmetic 脉冲停止后 Montage 自然结束；Pistol 单次也收敛。
换枪即时采样仍为 FP_Rifle_Shoot_Montage，1 秒后没有活动 Montage。
**只能证明最终收敛，不能声明换枪当帧清理通过。**
完整 Full-auto 输入松开、GAS Cancel、Reject、死亡和延迟网络的方案 C 测试尚未覆盖。

Observer 的新增实测结果见本报告末尾补测记录。
原两项 PIE 同时保留确认 Montage 次数与 Owner 不重复确认的既有断言。

## 用户要求的简短对照

“失败图”列只填写历史确有记录或明确注明复现的事实。

| 项目 | 原始生产图 | 失败迁移图 | 修正隔离图 |
|---|---|---|---|
| Owner Fire | FP 方案 A | 后置贡献缺失 | TP 与 CopyPose 有增量 |
| 自定义求值 | 无 | 无入口计数 | I=1，U/E 持续增长 |
| Arms 权重 | 原生链 | 后置读数 0 | Owner Alpha=1，有权重 |
| Reload 视图 | 网络 2，单机 1 | 网络 1，单机 0 | 网络 2，单机 1 |
| Magazine | 两模式通过 | 各少 Owner 一份 | 两模式通过 |
| Observer Fire | 原生确认链 | 未独立证明最终姿态 | 保留原生链并补测 |

失败机制复现中自定义节点仍被求值，只是 Alpha=0 早退。
不得把表内历史缺少计数写成历史 E=0。
历史报告中的 0.150 来自其 upperarm 指标，不能直接称为本轮 hand_r 比值。

## 不变量覆盖与结果边界

### Invariant 1：Owner Immediate Feedback

- 触及：TP 后置 Fire 与 FP 继承的骨骼可见贡献。
- 不应改变：本地输入时机、声音、Muzzle FX、Recoil 和确认排除。
- 证据：同帧 CopyPose、两枪偏转及单次原测试；姿态子集通过。
- 未覆盖：完整输入到表现的网络时间证据，因生产路由本轮没有迁移。

### Invariant 2：Local Prediction Obeys Weapon Rules

- 触及：动画持续时间、换枪后旧 Montage、Reload 并存。
- 不应改变：Tag 门控、RefireRate、预测预算和未知服务器真值的处理。
- 证据：Reload 三项、Cosmetic 脉冲收敛、换枪即时与延后采样。
- 未覆盖：真实 Full-auto Cancel / Reject；即时清理存在缺口，不能整体判 PASS。

### Invariant 3：Server Is Final Authority

- 触及：隔离运行仍借用原射击测试产生一次 Gameplay 请求。
- 不应改变：Ammo、Projectile、Hit、Damage、Death 与武器身份裁决。
- 证据：受保护生产代码哈希不变，原测试断言仍运行。
- 未覆盖：完整权威边界矩阵；本轮是图审计，没有授权修改对应逻辑。

### Invariant 4：Remote Is Confirmed Only

- 触及：Observer 仍消费原生 Slot，后置节点透传。
- 不应改变：远端只能由服务器确认驱动，不使用 Owner 预测入口。
- 证据：原确认次数断言、非 Owner Alpha=0，以及补测的 Slot 前后姿态。
- 未覆盖：Observer 四种反馈的完整批次归因；本轮只取证动画。

### Invariant 5：Exactly One Authority Result

- 触及：测试中的原 Pistol 单次射击，额外探针是纯 Montage 播放。
- 不应改变：一次权威结果、伤害和死亡归因。
- 证据：未改 Gameplay 路径，保留原测试计数；不把 Cosmetic 脉冲计为权威射击。
- 未覆盖：完整 Authority Result 集合；本轮没有运行对应大型集合。

## 正式迁移范围与停止条件

未来若获准正式迁移，最小范围应限于：

- 自定义运行时节点：handler 修复、清晰注册前提；移除审计热路径采样。
- 对应编辑器节点：仅必要的引脚与验证契约，不新增动画框架。
- TP AnimInstance：线程安全的 Owner 角色输入，保留原装备层权重。
- ABP_TP_Player：上述角色互斥图与编译缓存，保留原状态机、Notify 和 IK。
- ShooterCharacter 的表现入口及停止路径：Owner TP 预测与旧 Fire 清理。
- 定向测试：正式输入、取消、换枪、两枪与 Observer 的同帧归因。

FP 资产、武器资产、GAS、RPC 和权威射击不属于当前推荐的必要迁移范围。
若实现被迫涉及这些边界，应停止并重新界定任务，不顺手扩大改造。

以下任一条件成立即不得进入正式迁移：

1. Alpha handler、Slot 注册或编译后链接仍无法证明正确。
2. WeaponAction 来源被替换，或通过压零整个层来绕过原生 Slot。
3. IK 每次图求值被重复调用，或运行时裸缓存没有编译更新顺序。
4. 两个 Reload PIE 的任一原断言失败。
5. 换枪、Cancel、Reject 或真实连发停止仍产生不可接受的残留。
6. 同帧姿态、Observer 来源或完整 FP 最终修正的证据不足。
7. 需要用 Slot 改名或放宽断言才能通过。

本轮审计停止在候选可行性证据处，不宣称方案 C 已具备生产验收条件。

## 证据索引与复核方法

本轮证据根目录：

```text
Saved/Automation/MaskedFireAudit_20261009/
```

- before/：审计前用户节点与工具源码副本。
- baseline_hashes.json、final_hashes.json：生产保护及已有 WIP 哈希。
- matrix_summary.json：主要单变量结果和计数摘要。
- Baseline_v3、FixedBool_v3：原图与候选的两个 PIE。
- LegacyBool_v3、LegacyFloat_v3：漏执行 handler 的两种编译绑定。
- LayerZero_v3、OwnerLayerZero_v4：层权重故障负例。
- FixedPose_v4：两枪同帧 CopyPose、脉冲与换枪结果。
- Matrix_v3：有 Duplicate 两模式完成记录，但整批被工具中断，无完整 index.json。
- 每个完整目录的 index.json、Editor.log、args.json 保存判定、原始日志和启动参数。

早期无效试跑没有删除：
初版实例被装备流程恢复原类；v2 探针对转换节点的假设错误导致断言崩溃。
v5 在固定等待后读到空武器崩溃，不作为有效测试。
这些属于夹具问题，不是生产动画回归的证据。
Matrix_v3 的 Baseline 单机曾漏一次 Recovery，后续干净 Baseline_v3 两项通过。
该不稳定记录保留，不以重跑覆盖原记录，也不归因于自定义节点。

复核主要源码：

- [自定义节点][node]：Update 显式执行 handler；Evaluate 透传、合成与采样。
- [隔离图夹具][fixture]：临时图、单变量、实例设置、IK 与 CopyPose 探针。
- [原 PIE 测试及审计入口][pie]：原 Reload / Magazine 断言保留。
- [历史迁移工具][migration]：Owner 层权重策略与 SlotSource 恢复假设。

本机引擎证据位于 Engine/Source，范围限定如下：

- Runtime/Engine/Private/Animation/AnimNodeBase.cpp：FPoseLinkBase::Update。
- Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_TwoWayBlend.cpp：handler 执行。
- Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_Slot.cpp：注册、权重与 Source。
- Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_LayeredBoneBlend.cpp：相关性门控。
- Runtime/Engine/Private/Animation/AnimInstanceProxy.cpp：注册及 Slot 权重 / 求值。
- Runtime/Engine/Private/Animation/AnimInstance.cpp：Montage 与并行更新顺序。
- Editor/AnimGraph/Private/AnimationGraphSchema.cpp：Pose 树连接断链规则。
- Editor/AnimGraph/Private/AnimBlueprintExtension_CachedPose.cpp：缓存更新顺序编译。

[UE 5.6 官方 Slot 文档][epic]给出 Slot Source、上半身分层与 Slot Group 中断语义。
本轮候选据此保留 Slot 和 Source 的职责，再以本机实现核对具体时序。
官方文档并未保证任意两个同名自定义消费端安全；角色互斥方案由本轮隔离证据支持。

[node]: ../Source/ShootGame/Characters/Animation/AnimNodes/AnimNode_ShooterMaskedFireSlot.cpp
[fixture]: ../Source/ShootGameEditor/Tests/ShooterMaskedFireAudit.cpp
[pie]: ../Source/ShootGameEditor/Tests/ShooterTPPlayerPIETests.cpp
[migration]: ../Source/ShootGameEditor/Migration/ShooterTPOwnerFireWiringCommandlet.cpp
[epic]: https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-slots-in-unreal-engine?application_version=5.6

## 补测与交付状态

FixedPose_v6 两项 PIE 均 Success，编译 build7.log 为 Succeeded。
Observer 探针采样 863 次，原生 Arms Slot 前后右手最大差为 7.274859°。
这是同次求值的 Slot 贡献；不是 Montage_Play 返回值，也没有再求值上游。
该采样位于原生 Slot、IK 之前；Observer 最终骨骼变换另见 SAMPLE 日志。
本轮未对 Observer 的最终 IK 后姿态建立独立无 Fire 配对差分。

补测 Owner 的同帧结果：

| 模式 | Rifle TP / FP | Pistol TP / FP | 最大 CopyPose 差 |
|---|---:|---:|---:|
| Listen | 2.034411° | 7.274873° | 0.000002° |
| Standalone | 2.034414° | 7.274859° | 0.000002° |

每项的 TP 与 FP 值一致，pelvis 额外位移为 0。
换枪即时旧 Rifle Montage 仍存在，补测没有掩盖或修复这个缺口。
新增等待按实际 Owner 武器身份确认复制到达，超过 5 秒记错误。
完整 v6 报告和日志保存在同一证据根目录。

本轮新增两个隔离辅助源码文件，并修改 PIE 的可选审计入口。
未投入生产的自定义节点保留 handler 修复和诊断字段。
原节点源码在 before/ 中可逐项比较；诊断字段及热路径采样不应原样发布。
已按源码文件新增约定刷新一次 VS 工程；未操纵 VS 会话。

定向编译、源码 include 路径检查、工作区增量版式及 git diff --check 均通过。
新增文件另检查 CRLF、显示宽度与空白，不冒称 WorkingTree 门禁覆盖未跟踪文件。
没有运行音频输出或渲染验收；NoSound / NullRHI 的限制保留。
最终以 final_hashes.json 核验生产 TP、FP、ShooterCharacter 和保留工具未变。
