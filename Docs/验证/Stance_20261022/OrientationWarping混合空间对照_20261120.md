# Orientation Warping 旋转混合空间最小对照

日期：2026-11-20。工程 ShootGame_UE58，UE 5.8.4，分支 codex/ue-5.8。
本轮只测试前／左交接，不修改来源，不自动修复或扩展其他方向。

## 结论

**Mesh Space Rotation Blend 是该小腿翻转的重要诱因。**
同一精确输入下，仅关闭三个开关，小腿变化由 173.39° 降为 10.75°，
右脚位移由 61.33 cm 降为 4.57 cm。

但 Local Space 重新出现约 18～21 cm 的鞋面下沉及局部双鞋相交。
因此结论是“改变混合空间能显著影响异常，但关闭开关只能部分改善”，
不能将 Local Space 当作已通过的修复。

固定 Yaw=-55°、权重各 0.5，仅推进 Phase，也能触发 Mesh Space 翻转。
原异常不需要方向权重同时变化才出现，存在明确的相位依赖。
下一步优先研究混合阶段，来源兼容性仍须验证，不能只继续调方向平滑。
本轮没有实施下一步。

## A/B 实际设置

隔离目录：

~~~text
/Game/Experiments/Stance_20261022/OWBlendSpace_20261120/
~~~

A 为 ABP_BlendSpace_A，B 为 ABP_BlendSpace_B。
两者从当前磁盘 ABP_OW360 复制，不修改原图。
逐节点、引脚、连接、原生设置、事件图和默认值已比较。

| 全身分层节点 | A Mesh Space | B Local Space |
| --- | --- | --- |
| Forward／Right 组 | true | false |
| Back／Left 组 | true | false |
| FR／BL 最终组合 | true | false |

三者都保持 Root 分支、Depth=0。
Root Space Rotation Blend=false，Mesh Space Scale Blend=false，其他设置不变。
主图均为 31 个 AnimGraph 节点及 80 个 EventGraph 节点。
A 与原图相同，B 与 A 的差异只有三处 bMeshSpaceRotationBlend。
没有只关闭其中一个节点后再推断全树行为。

四来源、Evaluator、共享 Phase、权重计算和 Blend Tree 均不变。
OW 仍为 Manual、ComponentTransform、Z 轴，Alpha 与分配均为 1。
Spine 五节保持原配置，RotationInterpSpeed=0，MaxCorrectionDegrees=65。
没有新增 IK、Inertialization、时间重映射或动画修正算法。

本区域实际 MixFR=0，MixBL=1，MixGroups=Left 权重。
两个组分别提供 Forward 和 Left，再由最终组混合。
本轮同步关闭三处，所以不能单独证明某一个节点是唯一责任节点。

[完整设置与差异审计](OWBlendSpace_20261120/Audit.json)。

## 来源和同条件保证

四个序列仍在：

~~~text
/Game/Experiments/Stance_20261022/FourDirectionRootLockSource_20261117/
~~~

AS_RootLockSource_Forward、Back、Left、Right 均未修改。
Skeleton 仍为 FourDirection_20261117/SK_FourDirection，89 骨。
网格为生产 SKM_Manny_Simple；Root Motion 与 Force Root Lock 均保持 false。
本轮没有重导入、改 FBX、改 Skeleton 或改变身体基准。

四组采样对象均为同类 ShooterStancePreviewPawn，使用同一世界与网格变换。
主 A/B 同帧采集，逐点检查实际 Phase、角度和全部来源权重一致。
这避免了 AnimBP 因 Owner 类型不符而跳过原事件链的问题。
初次 Owner 不符的夹具数据已隔离，未纳入本报告。

通过原图已有测试入口暂时固定输入，不修改资产默认值。
使用记录的 SmoothedYaw 作为待重放角度，避免再次平滑后偏离原记录。
角度、权重仍由原生事件图计算；没有手写权重替换或骨骼求解。
每个条件等待原生求值后采集，Phase 按要求冻结或逐条按记录设置。
这里比较的是相邻输入的姿态差，不将采集等待时间冒充原播放时间。

精确重放中，A 的最终腿部姿态复现了上一轮记录。
原四方向、Skeleton、代码和历史报告保护见后文。

## 异常两帧重放

使用完整精度：

| 输入 | 第一帧 | 第二帧 |
| --- | ---: | ---: |
| SmoothedYaw | -55.49946212768555 | -54.74951934814453 |
| Phase | 0.366719800978899 | 0.3833865001797676 |
| Forward 权重 | 0.46257148968097617 | 0.5187821200652594 |
| Left 权重 | 0.5374285103190238 | 0.48121787993474063 |

Right 和 Back 权重均为 0。
两个输入对应原记录约 1/60 秒的相邻帧。
旋转差使用归一化四元数的最短角差，不比较欧拉角数值跨界。

| 指标 | A Mesh Space | B Local Space |
| --- | ---: | ---: |
| thigh_r 组件旋转差 | 10.0622° | 9.4496° |
| calf_r 组件旋转差 | 173.3906° | 10.7527° |
| foot_r 组件旋转差 | 15.8505° | 12.4289° |
| foot_r 组件位移 | 61.3298 cm | 4.5651 cm |
| 两帧最小三维双踝距 | 82.1551 cm | 40.1489 cm |
| 两帧鞋面相交 | 均未检出 | 均未检出 |
| 最低鞋面，第 1／2 帧 | -2.403／-2.226 cm | -18.480／-18.418 cm |

“此两帧无相交”不代表局部扫描全程无相交。
B 的小腿不再翻转，但鞋面明显下沉，不能判定姿态合格。
foot_r 第 1 帧约 0.79 cm，并不代表鞋面接地良好。
鞋面使用既有网格蒙皮重建，双鞋表面用 BVH 检测；骨骼高度单独记录。

[两帧原生姿态对照](OWBlendSpace_20261120/Pair.png)。
[完整数值](OWBlendSpace_20261120/Analysis.json)。

## 局部扫描

有效输入共 739 个，均在前／左 -65～-45° 范围内。
每种空间采集完整 89 骨，另采相同条件的只读观察副本。

### 固定 Phase，仅改变 Yaw

分别固定 Phase=0.366719800978899、0.38、0.3833865001797676。
每个切片从 -65° 到 -45°，步长 0.1°，各 201 点。
本轮这些切片均没有复现近 180° 翻转。

| 固定 Phase | A 最大小腿角差／步 | B 最大小腿角差／步 |
| --- | ---: | ---: |
| 0.366719800978899 | 1.7731° | 1.4542° |
| 0.38 | 1.5991° | 1.4137° |
| 0.3833865001797676 | 1.5832° | 1.4041° |

A 的最坏相邻右脚位移约 1.036 cm，B 约 0.450 cm。
这些局部切片没有翻转，不证明所有 Phase 的方向扫描都正常。
B 在第一个切片最低鞋面约 -20.768 cm。

### 固定 Yaw，仅推进 Phase

为区分时间与方向因素，固定 Yaw=-55°，两源权重都保持 0.5。
Phase 从 0.34 到 0.42，步长 0.001，共 81 点。
这不改变来源相位，只逐点评价原动作时间。

A 在 Phase 0.372 → 0.373：

- Yaw 完全不变，Forward／Left 权重均为 0.5。
- calf_r 变化 179.8894°，foot_r 位移 62.4678 cm。
- 说明仅 Phase 推进就能触发该不连续，不是普通抬腿或方向权重变化。
- B 在该整个局部扫描中最大小腿角差约 0.2829°／步。
- B 最大右脚位移约 0.207 cm／步，但最低鞋面约 -19.436 cm。

同一临界步，Forward／Left Warped 小腿仅变化约 0.339°／0.892°。
最终 B 变化约 0.2829°，右脚位移约 0.2025 cm。
[临界步内部核对](OWBlendSpace_20261120/CriticalPhase.json)。
[固定角度时的最明显异常](OWBlendSpace_20261120/Scan.png)。

### 按原记录推进 Phase 与 Yaw

从上一轮无截图运行数据提取局部去程 27 点、回程 26 点。
保留原始完整精度及原时间信息，不用固定半周期偏移。

| 原记录局部段 | A 最大小腿变化 | A 最大右脚位移 | B 最大小腿变化 | B 最大右脚位移 |
| --- | ---: | ---: | ---: | ---: |
| 前往左 | 11.5784° | 7.455 cm | 11.9989° | 8.575 cm |
| 左返回前 | 173.3906° | 61.330 cm | 13.2143° | 7.103 cm |

A 的回程准确复现原两帧翻转。
B 的去程有 3/27 个采样检出双鞋相交，最低鞋面约 -13.757 cm。
因此 B 虽不出现同类翻转，仍不能作为来源兼容性已解决的证据。
不同步长的扫描只用于各自定位，不能直接将最大变化当作同速运动比较。

[Local Space 的下沉和相交](OWBlendSpace_20261120/LocalProblems.png)。
[逐点采样](OWBlendSpace_20261120/Samples.json.gz)。
[逐点鞋面指标](OWBlendSpace_20261120/Metrics.json.gz)。

## 内部原生输出观察

主 A/B 保持原拓扑；另建 A_Observed、B_Observed 两份独立观察副本。
使用原生 Observe Bone 读取 Forward／Left 的六个腿部骨骼。
观测点位于各来源原 CS→LS 后，通过原生 LS→CS 读回，再原样送回局部姿态。
这些副本只读，不对骨骼施加变换，不作为正式 A/B 输出。

通过引擎只读 OBJ DUMP 导出原生缓存，沿实际 PoseLink 追溯来源。
不是用原 FBX 离线旋转估算来代替 Warped Pose。
欧拉缓存转四元数也由 UE 原生 Rotator 完成。
共读取 1,478 条内部记录；零权重分支不作为有效内部样本。

全扫描中，观测副本与主图位置差小于 0.000005 cm，
旋转差小于 0.000014°，没有实质改变主输出。
两空间的有效上游位置一致，旋转差约 0.000003° 内。

精确两帧的上游：

| 阶段 | thigh_r 角差 | calf_r 角差 | foot_r 角差 | foot_r 位移 |
| --- | ---: | ---: | ---: | ---: |
| Forward Warped | 9.8425° | 5.8503° | 5.9955° | 2.4193 cm |
| Left Warped | 1.5218° | 12.6487° | 68.0553° | 6.6669 cm |
| 最终 A | 10.0622° | 173.3906° | 15.8505° | 61.3298 cm |
| 最终 B | 9.4496° | 10.7527° | 12.4289° | 4.5651 cm |

单个 Warped Pose 的小腿都没有这次 173° 翻转，
而同一上游经不同空间组合后，最终结果明显不同。
这支持异常在当前姿态组合阶段被触发或放大。
Left 自身脚旋转也有较大变化，不能据此声称所有来源都完全平滑。

本轮原生两源 calf_r 夹角约为 153.23°、165.20°。
此前报告中的近 180° 数值属于离线近似，不是本轮原生内部观察结果。
不能用旧点积估计或点积符号直接宣布引擎 Bug。
本轮尚未定位引擎内部具体插值路径，也没有将其写成已证实原因。

[内部姿态](OWBlendSpace_20261120/Internal.json.gz)。
[原生导出原文](OWBlendSpace_20261120/NativeObserver.log.gz)。

## 人工预览与视觉证据

打开本轮 L_BlendSpaceCompare 并运行：

| 控制 | 行为 |
| --- | --- |
| 1 | A：Mesh Space |
| 2 | B：Local Space |
| J | 上一轮精确第一帧 |
| K | 上一轮精确第二帧 |
| Q／E | 固定当前 Phase，在 -65～-45° 内改变 Yaw |
| R | Yaw=-55°、Phase=0.38 |
| V | 既有 Pawn 观察视角 |

本轮预览不做 WASD 位移，不播放新 Idle，不自动扫描其他方向。
选择 A/B 后保留同一个角度与 Phase，便于查看相同输入。
SharedPhase、FreeYaw、CurrentMode 也在隔离控制 Actor 的 Details 中可查。

实际编译的 1/2、J/K 事件处理链已重放验证，最终骨骼与数值样本一致。
这是事件处理验证，不声称已完成 OS 物理键盘投递验收。
最终人工键盘体验仍由用户检查。

截图用同一网格、地面、角色变换及共同观察相机。
相机复制既有视角 2：位置 (595,220,155)，目标 (15,0,85)，FOV=58。
每张检查实际相机位置及骨骼姿态，未移动 Root、角色或地面掩盖问题。
普通手动预览仍由 Pawn 相机控制；新增相机只供观察。

为免静态画面出现渲染拖影，两组截图共同关闭运动模糊与时域抗锯齿。
这些只影响证据渲染，数值采样与截图分开。
捕获前后保持同一姿态；完成后将两个渲染选项恢复原值 4/4。
对照图只做固定裁剪与文字标注，没有修改姿态或生成中间动作。
完整原生 PNG 与最大姿态复现误差见证据目录。

[截图来源与一致性](OWBlendSpace_20261120/Evidence.json)。
[预览事件验证](OWBlendSpace_20261120/PreviewValidation.json)。

## 文件与保护

新增资产仅在本轮隔离目录：

- ABP_BlendSpace_A.uasset。
- ABP_BlendSpace_B.uasset。
- ABP_BlendSpace_A_Observed.uasset。
- ABP_BlendSpace_B_Observed.uasset。
- BP_BlendSpacePreview.uasset。
- L_BlendSpaceCompare.umap，含隐藏只读采样对象与共同观察相机。

新增本报告和同级 OWBlendSpace_20261120 证据目录。
更新当前实验入口和唯一忽略的工作区进度文档。
Saved/OWBlendSpace_20261120_01 保留原始数据和本轮采集脚本。
没有新增或修改生产 C++，没有提交、暂存、重写历史或建立新分支。

对照本轮开始时 2,280 个已有文件，原资产、源码、配置及历史报告保持。
四个外部原始 FBX 也重新计算 SHA。
没有修改原 ABP_OW360、四段动画、Skeleton、旧实验或生产系统。
[保护清单与实际变更](OWBlendSpace_20261120/Protection.json)。
[外部 FBX 核对](OWBlendSpace_20261120/SourceProtection.json)。

## 实现依据及验收边界

官方 [Blend Nodes][blend] 区分网格空间与局部空间旋转混合。
官方 [Observe Bone][observe] 定义其为指定空间骨骼观察节点。
本地 UE 5.8.4 已核对观察节点仅缓存显示值，不输出骨骼变换。

这只支持本轮对照方法，不代表任一空间天然适合当前四个来源。
已证实的是空间设置对异常具有显著影响；来源兼容性与姿态质量仍未解决。
本轮完成后停止，不自动调整开关，不继续其他算法，不替换原实验。

[blend]: https://dev.epicgames.com/documentation/unreal-engine/animation-blueprint-blend-nodes-in-unreal-engine
[observe]: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AnimGraphRuntime/FAnimNode_ObserveBone
