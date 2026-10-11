# Rifle 上半身分层与 AimTransition 混合机制专项审查

日期：2026-10-10；Git HEAD：63f9d351f0e4b46bba1eede838b801265a3b0885。
本机引擎为 UE5.6.1，CL 44394996。
本轮只读，不接阶段 B，不修素材，不改变受保护基准，也没有提交或推送。

首发提示词已完整记录在本轮唯一工作区进度文档。
流程结束后按规范清理该临时文档，另保留附件原文副本。
原有“Rifle 动画状态机实施进度”属于另一份进行中计划，本轮没有改动。

证据分为：当前编辑器读取、本机源码、历史静态样本、数学推导、尚未完成的运行时实验。
本轮没有取得真实 P0/P2 骨骼数值；下文不会把 AO 样本改称为真实 ArmedAimPose。
已有未提交文件保持原状，新增交付仅为本报告及工作区外的取证材料。

## 1. Layered Bone Blend 核实结论

### 1.1 当前图与实际边界

重新读取编辑器中的 ABP_TP_Player，主图仍为 25 个节点。
WeaponAction 仍只有 ArmedAim / Reload，没有已接线的 AimTransition / LowReady。
四份派生 AnimSequence 已存在，目录为：

/Game/Shooter/Animation/ThirdPerson/Rifle/

实际连接为：

~~~text
动态 Hold → 动态 AO → Save Cached Pose: ArmedAimPose
→ WeaponAction → Arms Slot → Layered Bone Blend
→ Local To Component → Aim IK → LeftHand IK → Component To Local → Output
~~~

Layered Bone Blend 的 BasePose 来自 LocomotioStateMachine。
ArmedAim 状态的 Result 来自 Use Cached Pose。
该状态内部旧 Hold/AO 节点未连接到 Result，不能据它们判断输出。

本轮读取的外层分层配置：

- BlendMode=BranchFilter，只有一个 BlendPose。
- 唯一过滤分支 BoneName=Spine_01，BlendDepth=1。
- Mesh Space Rotation Blend=True。
- Root Space Rotation Blend=False，Mesh Space Scale Blend=False。
- CurveBlendOption=Override，Blend Root Motion Based On Root Bone=True。
- LODThreshold=-1；权重引脚由 WeaponUpperBodyWeight 驱动。

C++ 在正常更新中根据 bHasEquippedWeapon 将该权重写为 1 或 0。
读取到的 CDO 默认权重为 0，不是有效装备时的运行时值。
因此“有效装备时权重=1”是代码行为，不能把本轮 CDO 查询称为实例实测。
[当前权重写入](D:/Unreal_Projects/ShootGame/Source/ShootGame/Characters/Animation/ShooterThirdPersonAnimInstance.cpp:549)

本机 CreateMaskWeights 的规则为：
后代深度 d 从过滤根的 0 开始，单过滤器权重为 clamp((d+1)/BlendDepth, 0, 1)。
非后代权重为 0；当前 BlendDepth=1，过滤根自身已经是满权重。
它不是只覆盖一层，也不是 spine_01 半权重。

准确集合是 Skeleton 中 spine_01 自身及全部后代，并经 RequiredBones 映射到当前 LOD。
包括实际躯干链、颈头、锁骨、肩臂、手和这些分支的后代。
root、pelvis、腿部属于外部集合，由 BasePose 选择。
辅助骨是否包含必须看实际父子关系，不能根据 ik_、twist 等名字猜。
本轮未导出完整 ReferenceSkeleton 父索引表，额外辅助骨不另列未核实名单。

源码位置：

- [过滤权重生成](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp:2398)
- [分层节点求值](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_LayeredBoneBlend.cpp:207)

官方文档说明分支过滤和旋转混合空间。
上述具体公式和当前配置以本机 5.6.1 实现及编辑器读取为依据。
[Epic 5.6 混合节点](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-blend-nodes-in-unreal-engine?application_version=5.6)

### 1.2 pelvis 被选择为 BasePose，不等于其输入差异毫无影响

定义 L 为 Locomotion 输入，W 为 Arms Slot 输出。
没有 Montage 时，W 才对应 WeaponAction。
骨骼 i 的最终分层权重为 w_i，父骨为 p(i)。

本机旋转路径先分别累积两个完整输入的 Mesh Space 旋转。
它没有先把 W 的 pelvis 换成 L，再累积 W 的上半身旋转。

关键运算：

~~~text
Q_i^L = Q_parent^L × q_i^L
Q_i^W = Q_parent^W × q_i^W
Q_i^O = normalize(FastLerp(Q_i^L, Q_i^W, w_i))
q_i^O = inverse(Q_parent^O) × Q_i^O
~~~

在 w=0/1 的端点，源码分别直接选择来源/目标旋转，再还原 Local。
Translation 始终在 Local Space 选择或混合。
当前 Scale 也在 Local Space 处理。
源码位置：

- [输入旋转累积与还原](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp:1927)
- [当前 Mesh Space Rotation 路径](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp:2190)
- [ISPC 对应路径](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.ispc:268)

由此可区分四件事：

1. W 自身的 pelvis Local 旋转参与计算 Q_spine_01^W 及后续目标旋转。
2. 输出 pelvis 的 Local Transform 选择 L，W 的 pelvis Translation 不直接写入输出。
3. 输出 spine_01 的 Local 旋转会补偿 L 的父旋转，以恢复目标 Mesh Space 旋转。
4. Component Space 手部位置还要用最终父链、Local Translation 和 Scale 重建。

当前满权重时：

~~~text
Q_pelvis^O = Q_pelvis^L
Q_spine_01^O = Q_spine_01^W
q_spine_01^O = inverse(Q_pelvis^L) × Q_spine_01^W
~~~

所以，Mixamo pelvis 的旋转虽未作为输出 pelvis 旋转被直接选中，
仍可能经目标脊柱的 Mesh Space 旋转影响整条上半身。
它在分支边界被转移成了输出 spine_01 的补偿旋转。

单位 Scale 条件下，位置重建为：

~~~text
p_i^O = p_parent^O + rotate(Q_parent^O, t_i^O)
~~~

一般情况还需包含父 Component Scale。
首个上半身偏移使用最终 L pelvis 的位置、旋转和 Scale。
更远的手部偏移使用已混合得到的各个父骨 Component Transform。

不能用“删除 Mixamo pelvis 再做普通 Local FK”代替当前 Mesh Space Rotation 路径。
也不能在两个已经重建的 Component Pose 中只替换 pelvis Transform，
却不重新计算所有后代。

一个纯数学反例：L pelvis 为 0°，W pelvis 分别为 0°/90°。
spine_01 的 Local 偏移为 (0,0,1)，后代偏移为 (10,0,0)，Scale 均为 1。
保持输出 pelvis 不变，仅改变 W pelvis 旋转，最终手部可从 (10,0,1) 变为 (0,10,1)。
14.142cm 是该合成反例的数学结果，**不是项目 P2 测量值**。

在此限定下，仅改变 W pelvis Translation 不会直接搬动输出 pelvis 或上半身原点。
但若改变的是 L pelvis，它作为最终父骨仍会影响边界位置。
两类来源不能混称为“pelvis 贡献”。

### 1.3 满权重时仍不能忽略基础姿态

w=1 的上半身骨骼使用 W 的目标 Mesh Space 旋转与 Local Translation/Scale。
但 root/pelvis/腿部仍来自 L，边界位置依赖这些父骨。
分层节点也实际更新、求值 BasePose。

CurveBlendOption、曲线骨骼映射、属性和 Root Motion 有独立处理规则。
它们不会把该骨骼权重自动变成另一个 Alpha，却可能影响下游求值。
Arms Slot 的输出还可能来自 Montage，而非 WeaponAction。
因此权重=1 不等于所有基础姿态、曲线和插槽影响都消失。

若两个候选状态通过同一外层节点，使用相同权重、LOD 和 Skeleton，
它们拥有相同的过滤集合；边界一致不代表端点姿态或手部位置一致。
内层状态过渡先混 Local Pose，外层再混 Mesh Space 旋转。
不能把“先分别分层，再混合”自动视为与当前顺序等价。

### 1.4 对旧归因的复核

已实际读取以下历史脚本与 JSON，没有修改原件：

- [分组替换脚本](D:/Unreal_Projects/ShootGame/Saved/MCP/split_armedaim_chain.py)
- [逐关节脚本](D:/Unreal_Projects/ShootGame/Saved/MCP/diff_armedaim_vs_mixamo.py)
- [分组结果](D:/Unreal_Projects/ShootGame/Saved/RifleAnimDerive/ArmedAimChainSplit.json)
- [逐关节结果](D:/Unreal_Projects/ShootGame/Saved/RifleAnimDerive/ArmedAimVsMixamo.json)
- [历史参照更正记录](D:/Unreal_Projects/ShootGame/Docs/开发记录/2026-10-10-1601-更正ArmedAim端点参照与数值.md)

| 数字 | 实际来源与适用范围 |
| --- | --- |
| 26.132cm | AO_CC 单独样本与 Mixamo 起点的静态手部差 |
| 20.792cm | 从 Mixamo 起点替换 root/pelvis/五节 spine 后的距离 |
| 10.666cm | 同一起点只替换右侧锁骨至手链后的距离 |
| 14.86cm | 逐关节顺序替换到 pelvis 时的手部增量 |
| clavicle_r 43.98° | 两个静态样本的 Local 旋转角差 |

旧 FK 的 q_parent × q_local 与父位置加旋转偏移的次序是正确的。
但它忽略 Scale，成立范围应限定为单位 Scale，或已有足够小的闭合残差。
历史 JSON 在所选样本中报告闭合残差为 0，支持该次静态计算自洽。
这不证明它执行了当前分层节点。

“躯干链”包含 root 和 pelvis，不是仅五节 spine。
14.86cm 同时替换 pelvis 的旋转与平移，而且在 root 已替换后的上下文中计算。
它不是单独旋转的纯因果量，也不是与替换顺序无关的独立贡献。

顺序归因的向量可以望远镜求和。
复算历史已舍入向量，右手总向量约为 (-20.150,-16.512,2.053)cm。
其范数为 26.132cm，而各步范数之和为 85.970cm。
不能把 85.970cm 当作总偏差。

分组单独替换还有骨骼间的非线性交互。
20.792cm 与 10.666cm 不仅不能相加，也不能直接转成“贡献百分比”。
必须记录向量、替换上下文和交互残差。

最关键的证据更正：
这些脚本直接采样 AO 的 Animation 资产，没有驱动完整 ArmedAimPose 子图。
匹配参考序列及单个 AO 锚点，只能支持参照假设，不能把样本升级为真实 P0。
历史记录中将该样本称为真实 ArmedAim 输出的部分，本轮不采纳。
历史原件按保护规范保持原貌，本报告提供当前证据口径。

因此：

- “这些静态样本的躯干差异明显”可以保留为线索。
- “最终 TP 躯干贡献一定大于右肩链”尚未被证明。
- pelvis 最终贡献为 14.86cm 或零，均不能据当前资料下结论。
- clavicle_r 角差较大，不足以证明它应优先于 upperarm 修正。
- hand 自身旋转不搬动 hand 原点，但仍影响枪械 Socket 朝向。

后续应先测分层边界的 Component 旋转/位置，再做相同节点算子的反事实替换。
先检查 spine_01 边界补偿与肩部链；是否修 spine、clavicle 或 upperarm 由测量决定。
本轮没有排序出一个已证实的“最大责任骨骼”，也没有修改 Mixamo 或受保护 ArmedAim。

### 1.5 正确比较点与缺失数据

统一目标 Mesh 为 SKM_Manny_Simple。
固定相同 AimPitchN、Hold 时间、Locomotion 时间、LOD、帧 Delta 和装备权重。
主比较使用 Local 与 Component Space；World 只用于核对组件变换。

- P0：当前 Hold Player + AO 节点实际求值后的 ArmedAimPose。
- P1：MM_Rifle_AimToLowReady 的 Player 在指定累计时间求值的姿态。
- P2-H/P2-M：分别把 P0/P1 送入相同外层分层节点后的输出，IK 前。
- P3：两项生产 IK 后的输出，只在最终观感比较需要时读取。

历史 AnimPoseSpaces.WORLD 字段来自独立资产采样，没有关卡 Actor World Transform。
不能与游戏 World Space 骨骼坐标直接相减。
用其作模型空间参照时，必须明确 Mesh 与变换条件。

当前 MCP 能读取图、编辑属性和 CDO，不能取得实例节点 FPoseContext。
它也没有 PoseWatch 注册、骨骼数组导出、逐帧运行时采样 action。
阶段 B 没有接线，因此当前生产图根本没有 Mixamo 状态的 P2 路径。
缺的不只是更多 Sequence 样本，而是节点级、同帧运行时输出接口与隔离候选图。

本轮没有启动新的 PIE、Commandlet 或资产写入流程。
未取得真实 P0/P2 数值，不宣布完成运行时验证。

## 2. A / C 机制比较

这里的 A/C 指本轮 AimTransition 候选，与历史 Fire 方案的同名字母无关。
比较先限定 Standard Blend，无 Blend Profile、Custom Transition 或 Inertialization。
C 内部采用普通双姿态 Blend；若改变混合空间或骨骼掩码，应另行定义方案。

| 项目 | A：原生 Transition | C：内部 Cached Pose Blend |
| --- | --- | --- |
| 姿态混合 | 状态机混源与目标 | 内部混合，再叠外部过渡 |
| 源持续求值 | 普通过渡期间是 | 内部分支相关时是 |
| 目标时间 | 进入并更新时推进 | 还受内部 Alpha 更新条件影响 |
| 动态 AimPitch | 源仍有权重时跟随 | 内部源仍有权重时跟随 |
| 中途反向 | 同 Player 改速率符号 | 仍需相同逻辑 |
| 时间保留 | 取决于初始化/相关性 | 没有额外保留保证 |
| Slot 满覆盖 | 来源可能停止更新 | 内部 Cache 不能绕过 Slot |
| 节点成本 | 一个动作 Player | 多一个 Blend、Cache 使用及 Alpha |
| 主要风险 | 端点、重入、Slot 恢复 | 同左，加双重混合/分支停更 |

### 2.1 A 的源不是冻结快照

本机 UpdateTransitionStates 在 Standard Blend 中更新源、目标两状态。
源以 Inactive 上下文更新不等于冻结姿态，它仍推进节点与数据输入。
EvaluateTransitionStandardBlend 每次求值相应状态输出。

ArmedAimPose 的 Hold、AO 在其被更新和求值时仍是当帧结果。
Save Cached Pose 是同次求值的复用机制，不是进入过渡时自动拍摄 Pose Snapshot。

目标状态在过渡被接受后成为 CurrentState。
若目标此前未激活或强制重置，会先初始化，再在该次更新中参与推进。
默认并不是“等外部混合结束再播放 Mixamo”。
资产时间通过 Tick Record 推进，受到 PlayRate、RateScale、Sync 等实际设置影响。
混合 Alpha 的时钟与 Sequence 累计源时间是两套量。

例如外部混合为 B 秒，总播放速率 v：
没有同步或其他时间控制时，混合结束时目标大致已推进 v×B 的源时间。
起始权重极低不代表目标始终停在第 0 帧。
若需要短时保持首帧，应显式定义 Player 时间控制，A/C 都不能靠节点名字保证。

源码位置：

- [过渡期间更新两状态](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimNode_StateMachine.cpp:858)
- [普通状态过渡求值](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimNode_StateMachine.cpp:1004)
- [Player 初始化与更新](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimNode_SequencePlayer.cpp:47)

官方资料同样区分传统混合的双源求值与惯性混合的来源停止求值。
不能把后者的机制套到本轮 A。
[Epic 5.6 混合节点](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-blend-nodes-in-unreal-engine?application_version=5.6)

### 2.2 A 的中断不是把旧序列自动逆播

找到新合法出口后，状态机记录旧过渡的中断通知，追加新的 Active Transition。
新目标已有权重会参与新的混合时长调整。
标准求值按活动过渡顺序把中间输出继续混到新目标。
它不是每次都丢掉当前混合，也不是自动保存一张冻结姿态。

新混合剩余时间不能简单称为“沿原 B 秒倒着走”。
本机用已有目标权重、曲线的逆 Alpha 和时间调整计算新 CrossfadeDuration。
实际出口、优先级、每帧过渡上限仍由图规则决定。

返回 ArmedAim 是状态拓扑改变。
Mixamo Player 负速率推进是动画时间方向改变。
两者可以分别发生；前者不保证后者。

源码位置：

- [新过渡与中断](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimNode_StateMachine.cpp:1250)
- [已有权重对时长的调整](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimNode_StateMachine.cpp:72)
- [活动过渡逐层求值](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimNode_StateMachine.cpp:947)

官方规则资料提供混合参数与中断设置。
连续逆播的累计时间语义仍以本机 Player 和初始化路径为准。
[Epic 5.6 状态过渡](https://dev.epicgames.com/documentation/en-us/unreal-engine/transition-rules-in-unreal-engine?application_version=5.6)

### 2.3 C 的 Cache 也需要真实更新请求

SaveCachedPose 接收使用者的更新上下文。
PostGraphUpdate 选择最高权重上下文更新其子图。
实际 Evaluate 首次访问时求值并缓存，同一求值计数内重复使用结果。

因此：

- Cache 不承诺每个游戏帧独立求值，也不承诺每个显示帧产生新骨骼数组。
- 无使用者更新时，不会自行推动 Hold/AO 时间。
- 同帧多次 Use Cached Pose 通常不会重复计算整个 Hold/AO 子图。
- 但 C 会增加内部混合和使用链路，并可能把 Hold/AO 求值延续到 A 已停止使用源之后。
- 更新权重选最高使用者，不是简单把每条使用路径权重相加。

普通 TwoWayBlend 默认按内部 Alpha 的相关性选择更新分支。
Beta=0 时，Mixamo 分支可能停止更新。
Beta=1 时，ArmedAim 分支可能停止更新。
AlwaysUpdateChildren 可以维持隐藏分支更新，但不会自动让外部 Slot 更新整个状态机。
ResetChildOnActivation 若开启，分支重新相关还可能重新初始化。

C 尚未实施，因此上述选项不能称为当前 C 的已配置行为。
它们是需要定义和测量的额外条件。
读取来源：

- [Cache 更新与求值](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimNode_SaveCachedPose.cpp:112)
- [内部 Blend 更新条件](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_TwoWayBlend.cpp:36)

### 2.4 等价的条件与双重混合

令 H 为同帧真实 ArmedAimPose，M 为同时间 Mixamo Player 输出。
B(H,M,a) 表示一次相同空间、相同权重的普通双姿态混合。

~~~text
A = B(H, M, alpha)
C内部 = B(H, M, beta)
C进入期 = B(H, B(H, M, beta), alpha)
~~~

若只比较一次混合，输入、有效 Alpha、时间、空间、骨骼权重和归一化都相同，
A 与 C 的普通 Blend 没有“实时来源”导致的本质差异。
两条本机路径都采用最短旋转符号的加权累积与归一化。
曲线、属性和通知/更新上下文还需单独控制，不能宣称整体图行为完全相同。

若 C 内外两层同时生效，Translation 的目标权重为 alpha×beta。
alpha=beta=0.5 时，M 的位置权重为 0.25，H 为 0.75。
这会改变节奏；不能拿它与 A 的 0.5 权重直接比较。

旋转包含每层的四元数归一化，一般不满足混合结合律。
不能把整个 C 的旋转严格压缩成一次权重 alpha×beta 的混合。
数学反例：0°→90°，两次 0.5 的普通 nlerp 得到 22.5°。
单次 0.25 则约为 21.598°。
这是合成计算，不是项目观感测量。

若 beta=1，C 内层退化为 M，外层与 A 相同。
若外层没有混合，只由内部 beta 执行同样的混合计划，也可构造同等结果。
这时多放一个普通 Blend 本身没有不可替代收益。

C 在外部过渡结束后仍保留 H 的非零权重，确实可以延续动态俯仰影响。
但输出已成为 H/M 混合动作，需明确持续区间、端点权重与返回策略。
它不是对原 Mixamo 的无损修复，也不能保证握持质量改善。

源码位置：

- [A 的逐骨骼累积](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimNode_StateMachine.cpp:1065)
- [普通双姿态混合](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp:509)

### 2.5 动态俯仰、逆播及相关性

平视、上仰、下俯进入时，A 的源读取当时 H；C 也只能在来源相关时读取 H。
A 的外部混合完成后，纯 Mixamo 不继续消费 H 的 AimPitch。
C 若保留 H 权重可以继续消费，但若 beta=1，同样只剩 Mixamo。
下游生产 IK 的效果属于 P3，不能据其结果反推 P0/P2 原本一致。

持续改变 AimPitch 时，应比较实际源权重尚存的区间与其消失后的区间。
不能笼统写成“A 不实时、C 实时”。

在 Mixamo 已推进 25%/50%/75% 时请求返回：
两方案都可以保持在同一 AimTransition 中，仅改变同一 Player 的 PlayRate 符号。
此时无重新初始化，累计时间可以保留，动画沿原路径倒退。
到达对应端点后再退出，而不是立即用状态反向 Transition 冒充逆播。

刚进入时立即返回，要特别区分：

- 留在同一状态改速率：A/C 都可以不换 Player。
- 立即退到 ArmedAim 后又进入：取决于原状态是否仍有混合权重与 Reset 选项。
- 状态完全失活后重入：通常会初始化 Player；False 不保证永久保留历史时间。

本机 SetState 在目标不再激活或 AlwaysResetOnEntry=True 时初始化状态。
当前机器还启用 Reinitialize On Becoming Relevant。
因此 Slot 停更后恢复可能重初始化整个机器。
这不能由 C 内部 Cache 消除。

连续同节点逆播不需要每帧手动设置 Accumulated Time，也不需要 Pose Snapshot。
新动作入口需设明确起点；跨失活恢复若要求续播，才需要保存动作身份/时间并恢复。
修改 StartPosition 本身不会移动已经运行的累计时间。
不能在每次 Become Relevant 都无条件归零。

不论采用 A/C，必须用方向对应的时间出口。
本机自动规则使用 Length-AccumulatedTime，不宜用于该负速率动作出口。
Player 更新、速率换向、出口规则与状态重入是独立的验收项。

### 2.6 当前 Arms Slot 对两者的共同限制

当前 Always Update Source Pose=False。
Slot.Update 只有在该项为 True 或 SourceWeight 相关时才更新来源。
满覆盖且 SourceWeight=0 时，来源 WeaponAction 可以停止更新。
“Montage 满权重”应同时检查 SourceWeight，不能仅凭 Montage 名称判断。

Slot.Evaluate 也只在来源权重相关时求值来源。
即使改成 AlwaysUpdateSourcePose=True，也只是保证更新，不保证满覆盖时求值 P0/P2。
这是实验记录必须区分 Update 与 Evaluate 的原因。

C 的 Cache 位于 Slot 来源内部，不能跨越该更新门控。
两方案恢复后都存在状态机重新相关/初始化的风险。
建议先在隔离候选中单独比较该选项 False/True；本轮不修改生产 Slot。

[本机 Slot 更新与求值](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_Slot.cpp:66)

## 3. 最小验证实验

### 3.1 本轮观测能力与授权边界

已有 MCP 只读图和 CDO 不足以导出 P0/P2。
Asset Python 采样不能补足这一缺口。
本轮只完成机制与历史算法复核，没有执行以下实验。

UE 自带 Pose Watch 可观察节点输出；Rewind Debugger 可记录并回看节点和 Player 时间。
官方说明要求录制前启用相关节点的 Pose Watch。
本机 RegisterWatchedPose 也保存骨骼数组、RequiredBones、曲线与组件变换。
这些是可用的取证来源，但当前 MCP 没有调用或导出接口。

[Epic 5.6 Pose Watch](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-nodes-in-unreal-engine?application_version=5.6)
[Epic 5.6 Rewind Debugger](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-rewind-debugger-in-unreal-engine?application_version=5.6)
[本机 Pose Watch 数据记录](E:/Unreal_Engine/UE_5.6/Engine/Source/Runtime/Engine/Private/Animation/AnimInstanceProxy.cpp:3451)

实验需要单独授权的具体操作：

1. 在独立测试项目/隔离副本创建 A/C 测试 AnimBP 和测试夹具，并编译测试资产。
2. 只给隔离图增加 AimTransition / LowReady，保持 ArmedAim 组成和外层分层一致。
3. 在测试图启用 Pose Watch；必要时仅在隔离副本启用调试插件和重启。
4. 若原生调试数据无法数值导出，增加隔离 Editor/Test 导出器，不写生产诊断代码。
5. 启动测试预览或隔离 PIE，录制节点姿态、权重与时间。
6. 对原生产实例添加临时 Pose Watch/开启录制也需明确纳入授权，不保存其元数据。

这是实验设计所需范围，不是本轮已获实施授权。
保存确认只允许本轮需要的会话管理，不扩大这些写入边界。

### 3.2 最小夹具与测点

先用原生产图的已有实例取 P0 和 ArmedAim 路径 P2-H，作为参照录制。
无 Fire/Reload 时记录 Hold 时间、AO 当前坐标、权重、Mesh、LOD 与组件变换。
仅看 Pose Watch 截图不能量化 cm，需取得其骨骼数组或对应 Trace 数值。

隔离夹具只包含 ArmedAim、AimTransition、LowReady。
两候选共享同一份生产 Hold/AO 输入构成，同一 Mixamo Player 设置和同一 Locomotion。
保持四份动画和受保护基准不变，不加入 Draw/Holster 或装备事务。

测试输入需覆盖生产 Native 更新实际写入的字段。
不能只在预览面板设置权重，却让 Native 更新因缺少 Owner/装备把它重新写为 0。
也不能让生产角色的固定 AnimClass 收敛逻辑把测试实例替换掉。

可用隔离的 Editor/Test AnimInstance 子类，在调用原 Native 更新后注入录制输入快照。
只用于复制图，生产父类不改；目标字段与图读取逐项核对。
先证明测试 ArmedAim 的 P0/P2-H 可复现原生产参照，再比较 Mixamo。
若无法证明，结果只能称为测试夹具输出，不升级为生产运行时输出。

最少观测节点：

- Hold Player 与 AO 后的缓存来源：区分实际累计时间及真实 P0。
- Mixamo Player：P1，记录 Accumulated Time 和实际速率。
- WeaponAction/Arms 来源：区分状态混合与 Slot 覆盖。
- Layered Bone Blend 后、Aim IK 前：P2。
- 仅当 A/C 观感排序需要时，再观察生产 IK 后的 P3。

每条记录至少含：

~~~text
Frame/EvaluationId, InstanceId, Mesh, LOD, DeltaTime
AimPitchN, HoldTime, MixamoTime, PlayRate, RateScale
StateWeight, TransitionAlpha, InternalBeta, SlotSourceWeight
NodeId, BoneIndexMap, ParentIndices
Local Translation/Quaternion/Scale, Component Transform
Curves, ComponentToWorld, Update/Evaluate/Initialize 是否发生
~~~

骨骼集合至少包含 root/pelvis、spine_01–05、左右 clavicle/upperarm/lowerarm/hand。
头颈只作边界检查，不在本轮改动。
导出要复制当次姿态数据，不保留临时 FPoseContext 指针跨线程/跨帧使用。
骨骼索引必须经 RequiredBones 映射，不能将 CompactPose 索引当作 Skeleton 索引。
Pose Watch 的 Local 数组可按真实父索引重建 Component，并标注为坐标转换结果。
输入必须是同次真实节点输出，不能重新用 Sequence/AO 样本拼出所谓 P0。

### 3.3 最短测试顺序与判据

1. 固定 Locomotion 与 LOD，分别采平视、上仰、下俯。
   同一输入时间下取真实 P0、P1，再记录两个状态的 P2。
   状态端点比较先无外部过渡、无 Montage，避免混入其他权重。
2. 在隔离夹具复用同一 L、Skeleton 与引擎分层算子做骨骼替换实验。
   改 W 的 pelvis 旋转/平移、spine 链、锁骨、肩臂链时分别重跑分层与 FK。
   同时测试只替换输出父骨的另一种操作，明确两者并非同一归因问题。
3. 对每项保存差向量、旋转、顺序及交互残差。
   用 P2 手部/握持点位移评估影响，不按 Local 角度大小排序。
4. A 先测正常降低/抬起与持续改变 AimPitch。
   在 Mixamo 25%/50%/75% 处反向，以及进入首帧立即返回。
   判据为同一 Player 的时间连续，符号反转后时间单调反向，端点出口正确。
5. C 只测一个明确反例需求：外部过渡结束后仍需 H 参与的特殊区间。
   先用 beta=1 作与 A 同等输入/权重的对照，再测额外区间。
   不能拿更慢的 alpha×beta 混合宣称观感天然更好。
6. 在夹具播放现有 Fire Montage，制造同样的 Arms Slot 覆盖。
   仅测试动画覆盖，不触发射击 Gameplay。
   分别记录 Slot 选项 False/True 下的停更、恢复、状态重入和 Player 时间。
7. 同帧姿态差以导出的未舍入骨骼数据计算。
   先标定重复录制的数值误差，再判断 A/C 差异；不虚构项目观感合格阈值。

默认 60Hz 先确定机制，再以 30Hz 检查事件边界。
不需要全量网络回归，也不扩大到完整武器切换。
没有新增 Gameplay 状态，不修改 Fire/Reload/Socket/FP 的生产职责。

结束后移除隔离测试资产、临时调试设置与导出器。
恢复原测试会话设置，核对生产文件与已有用户变更。
只保留原始录制、测量方法和新的结论报告；不回写历史记录。

## 4. 最终技术判断

### A. 旧 pelvis / spine / clavicle 归因是否反映最终 TP 分层

**不能直接反映。**
旧 FK 的层级次序在单位 Scale 范围内正确，静态样本替换计算自洽。
但输入并非真实 P0，也没有执行当前 Mesh Space Rotation 分层。

pelvis 输出使用 Locomotion 的事实成立。
“因此 Mixamo pelvis 对上半身贡献为零”不成立。
“最终贡献为 14.86cm”同样未经实测。
旧躯干/肩臂排序只能作为线索，暂不用于决定自动修骨顺序。

### B. 原生 Transition Blend 是否足以完成当前目标

**机制上足以作为首选原型。**
A 能持续混合实时源与目标；同一 Player 在不中断初始化时可反向保留时间。
还需要明确入口、方向出口、重入及 Slot 停更恢复逻辑。
状态机单独不会替用户完成这些设计。

自然程度、动态俯仰端点与实际 P2 差异尚待隔离运行时测量。
本轮不宣称阶段 B 已可直接接入。

### C. 内部 Cached Pose 混合是否有不可替代价值

**当前没有证据证明。**
C 不独占实时源姿态，不独占累计时间保留，也不能绕过 Slot 门控。
它会增加内部 Alpha、双重混合及隐藏分支更新条件。

只有明确需要外部过渡结束后的特殊姿态混合区间，
并且同条件运行时证据证明 A 的低成本方案达不到要求，才值得进一步考虑 C。
“理论更灵活”或“显式使用 Cache”不足以作为引入理由。

### D. 建议采用哪种实现

**倾向 A，先做最小隔离测量，再决定是否进入阶段 B。**
使用一个 AimTransition Player，同状态中反转速率，保留 ArmedAim 受保护基准。
先证明真实 P0/P2、端点和 Slot 恢复行为；不为 C 增加生产节点。

若 A 的特殊区间需求经测量确实无法满足，再用相同源/目标与权重对照 C。
本轮审查完成，停止在报告与实验设计，等待下一轮明确授权。

本轮取证入口：

- [编辑器图与节点快照](C:/Users/HP/.codex/visualizations/2026/10/10/01a123fe-b50e-7a92-a254-9edece4ab99e/RifleLayerBlendEditorEvidence.json)
- [合成数学反例](C:/Users/HP/.codex/visualizations/2026/10/10/01a123fe-b50e-7a92-a254-9edece4ab99e/RifleLayerBlendMathCheck.json)
- [首发提示词原文](C:/Users/HP/.codex/visualizations/2026/10/10/01a123fe-b50e-7a92-a254-9edece4ab99e/Rifle上半身分层专项审查初始提示词.txt)
