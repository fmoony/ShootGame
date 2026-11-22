# 四方向下蹲 BlendSpace 实验

日期：2026-11-17；UE 5.8.4，ShootGame_UE58，codex/ue-5.8。
本轮配置和隔离预览完成，视觉质量未通过，停止等待人工验收。

## 入口与操作

打开以下新地图后 Play，先点击预览画面取得键盘焦点：

/Game/Experiments/Stance_20261022/FourDirection_20261117/L_FourDirectionPreview

- W／S／A／D：前／后／左／右，组合按键产生斜向混合。
- 松键：暂停最后的移动姿态，不是 Idle。
- V：切换侧面、前方、后方观察。
- T：暂停／恢复动画。
- O：开启／关闭每三秒切换方向的八方向演示。
- Esc：退出 PIE。

原 L_StancePreview 的旧实验路由保留；本轮使用独立地图，避免误验旧 Refined。
新地图复制原 Stance 的地面、灯光和出生环境，显式指定四方向实验 GameMode。
仅提供第三人称身体移动预览，没有接持枪、上身分层或第一人称。

## 原生资产与真实拓扑

新资产：

- BS_FourDirection_Crouch：原生 UBlendSpace。
- ABP_FourDirection：原生 UAnimInstance 父类。
- L_FourDirectionPreview：复用原实验场景的独立地图。

上述资产均位于 FourDirection_20261117。
Skeleton 使用本次新导入的 SK_FourDirection，预览网格为 Manny Simple。

AnimGraph 实际只有一个 BlendSpace Player 和 Output Pose；另有两个方向变量读取节点。
MCP 读回共四个节点，连接如下：

```text
Forward ──→ BlendSpace Player.X
Right   ──→ BlendSpace Player.Y
             BlendSpace Player.Pose ──→ Output Pose.Result
```

事件图从原 Stance 预览 Pawn 读取 PreviewForward、PreviewRight。
没有 SequencePlayer、Idle、WeaponAction、Slot、Layered Blend Per Bone、AimOffset 或 IK。
C++ 只提供输入、观察相机、资产建图和只读记录，没有骨骼回写或自定义 FK／IK／混合。

## 四个样本

| 方向 | 新导入序列 | Forward | Right |
| --- | --- | ---: | ---: |
| 前 | AS_NewCrouch_Forward | 1 | 0 |
| 后 | AS_NewCrouch_Back | -1 | 0 |
| 左 | AS_NewCrouch_Left | 0 | -1 |
| 右 | AS_NewCrouch_Right | 0 | 1 |

没有中心样本、旧单膝跪 Idle、旧实验动画或新斜向序列。
采用原生三角化，横轴 Forward、纵轴 Right，范围均为 -1～1。
两个输入轴均为 0.2 秒 SpringDamper 平滑，默认阻尼为 1。
样本 Rate Scale 均为 1，不添加 Sync Marker 或相位偏移。
四段原始时长与轨道未修改，前行约 1.166667 秒，其余各 1 秒。

方向输入按绝对值之和归一，斜向为正负 0.5，保持在四端点的菱形边界。
引擎 GetSamplesFromBlendInput 实测：
四个主方向各自权重为 1；四个斜方向只使用相邻两段，权重约为各 0.5。

零输入保留最后的方向，用组件原生时间倍率 0 暂停播放。
初版检查发现输入平滑器默认中心导致启动时前后混合，已用原生 SnapToPosition 初始化前方输入。
该调用只定位输入过滤器，不处理骨骼。
修正后重新采集；初版 Runtime_InitialZeroInput_Rejected.csv 明确废弃。

## 原始运动保留与 Root 行进限制

四个序列、Skeleton、原始 FBX 和既有实验资产均没有修改。
保留原始骨盆、躯干、步态、重心、脚部变化及 Root 平移／旋转。
没有抬角色、抬 Root、制作 Idle、重生成动作或添加接地补偿。

本轮没有另加固定速度的 Pawn 位移，以免与源 Root 行进叠加。
前、后、左包含净行进，右移是原地素材；方向输入在本轮表示混合比例。
因此原始 Root 混合的世界行进并不保证是严格的八方向匀速移动。
循环仍会回到源轨道起点；这与步态相位、接地和正式速度匹配分别评价。
没有接入 CharacterMovement 或用程序改成原地动画。

## 数值检测

重新使用当前 0.2 秒平滑采集，未引用旧零平滑接地结果作为通过依据。
实际运行 26 秒，60 FPS，隔帧记录，共 780 个姿态帧、69,420 条骨骼记录。
包括启动暂停一秒、八方向各三秒、最后暂停一秒。
启动暂停保持 Forward=1、Right=0、phase=0；末尾暂停相位和 Root 均不再变化。

四个主方向分别取稳定段 66 帧，与对应序列的原生压缩姿态逐骨对比。
最大位置差小于 0.00018 cm，说明没有错误引用或额外姿态加工。
MCP 持续按键测试确认 W、S、A、D、W+A、S+D 的实际方向值和播放状态正确。
松键后时间倍率回到 0。

地面实际包围盒顶面约为世界 Z=0，没有使用预览自动抬地面。
鞋部数据使用已保留的 Manny Simple 参考蒙皮做离线线性蒙皮估算。
选择 foot／ball 权重大于 0.5 的鞋部顶点；不把 foot／ball 骨骼高度当作鞋底。
表面相交来自左右鞋 BVH 候选，不代表全身碰撞检测，也不等同视觉自然度结论。

| 方向 | 稳定采样帧 | 鞋部最低 Z，cm | 鞋表面相交候选帧 |
| --- | ---: | ---: | ---: |
| 前 | 72 | -6.01 | 0 |
| 后 | 72 | -6.44 | 0 |
| 左 | 72 | -5.79 | 0 |
| 右 | 72 | -5.30 | 0 |
| 前左 | 72 | -14.94 | 15 |
| 前右 | 72 | -8.03 | 0 |
| 后左 | 72 | -6.45 | 0 |
| 后右 | 72 | -17.78 | 24 |

这是离线几何诊断，不能作为人工视觉通过的替代。
主方向也仍有鞋面穿地，不能因相交候选为零就宣布合格。

同相位的原生序列核对进一步确认了混合新增的问题：
后右最差采样相位约 0.781，混合后 ball_l 约为 -15.49 cm；
同相位后退序列的 ball_l 约 13.64 cm，右移序列约 2.94 cm。
前左最差采样相位约 0.674，混合后 ball_r 约为 -13.07 cm；
同相位前行序列的 ball_r 约 13.65 cm，左移序列约 1.39 cm。

因此当前原生局部姿态插值确实新增了下沉，不能仅归因于原动画鞋面厚度。
后续需要审查支撑相位和局部旋转插值兼容性；本轮没有据此改动作或统一骨盆高度。

## 可见观察

已在实际 PIE 观察八方向，保留连续运行中的三张关键画面及暂停后清晰画面。
这些是原生 UE 截图，不是重新渲染的推导姿态；没有生成连续视频。

- 前：左右腿交替屈伸，保留原始俯身和起伏；仍见鞋面接地问题。
- 后：保留后退步态，整体行进与前行不同；部分相位脚部下沉。
- 左：保留源动作侧向身体变化，没有转成镜像或统一躯干。
- 右：保留原地侧移步态，不能据此判断正式移动速度匹配。
- 前右：可插值出中间姿态，部分鞋面下沉，相比前左没有同程度挤脚。
- 后左：仍有接地问题，但未见后右同程度的挤脚与陷地。
- 前左：部分相位脚步挤压、下沉明显，未通过。
- 后右：脚步挤压、下沉明显，未通过。

鞋面接地、脚滑、完整动作自然度及身体过渡连续性仍需用户亲自体验。
数值没有作为“自然”的判据，也没有把骨盆高低差当作源动作错误。

## 验证与保护

项目 Editor 编译通过，原生资产建图成功，实际 PIE 可手动控制。
文本检查、include 路径检查完成；最后一次源码调整仅为 Printf 参数换行。
未新增 Source／Plugins 文件，因此按项目规则没有刷新 VS 工程。
没有提交、新建分支或修改真实暂存区。
保护哈希见本轮 ProtectionAfter.json，四个新序列和 Skeleton 均未脏。

## 证据

Saved/FourDirectionBlend_20261117_01：

- Assets.json、AnimGraph.json、Weights.csv：实际引用、连线和原生权重。
- Runtime.csv、Frames.json：本次有效运行数据。
- RuntimeAnalysis.json：鞋部估算、相交候选和暂停验证。
- NativeChecks.json：主方向原生对比、混合最差相位和实际地面。
- ManualInput.json：真实持续按键及组合输入的读回。
- VisualCapture.json、VisualPaused.json：八方向截图对应的输入与状态。
- 各方向 _0.png～_2.png：运行关键画面，有原场景运动模糊。
- 各方向 _Paused.png：暂停后较清晰画面。
- BuildAssets.log、VerifyAssets.log、CaptureFixed.log：创建与运行记录。
- ProtectionBefore.json、ProtectionAfter.json：资产与源码保护边界。

旧 Runtime_InitialZeroInput_Rejected.csv 不用于本版本结论。
没有自动延伸为修复、相位调整、镜像、Idle 或上身重构。

## 外部依据与本地差异

Epic 文档说明原生二维输入、三角化样本及 AnimGraph BlendSpace Player 的使用方式。
本地 UE 5.8.4 已核对 BlendSpace 和播放器实现，并实际读回八方向权重。
项目额外约束是没有 Idle，且源动作保留完整 Root 行进；
这两点由隔离预览显式处理，没有套用包含中心 Idle 的普通移动模板。

https://dev.epicgames.com/documentation/unreal-engine/blend-spaces-in-unreal-engine
https://dev.epicgames.com/documentation/unreal-engine/blend-spaces-in-animation-blueprints-in-unreal-engine

## 可直接打开的证据

以下画面在实际 PIE 暂停后采集，分别对应八个方向；不是各方向最差相位的专门截图。

- [前](FourDirectionBlend_20261117/Forward_Paused.png)
- [后](FourDirectionBlend_20261117/Back_Paused.png)
- [左](FourDirectionBlend_20261117/Left_Paused.png)
- [右](FourDirectionBlend_20261117/Right_Paused.png)
- [前左](FourDirectionBlend_20261117/ForwardLeft_Paused.png)
- [前右](FourDirectionBlend_20261117/ForwardRight_Paused.png)
- [后左](FourDirectionBlend_20261117/BackLeft_Paused.png)
- [后右](FourDirectionBlend_20261117/BackRight_Paused.png)

- [BlendSpace 配置](FourDirectionBlend_20261117/BlendSpace_Config.png)
- [运行分析](FourDirectionBlend_20261117/RuntimeAnalysis.json)
- [原生对照与地面](FourDirectionBlend_20261117/NativeChecks.json)
- [原始骨骼 CSV 压缩包](FourDirectionBlend_20261117/Runtime.csv.gz)
- [键盘输入验证](FourDirectionBlend_20261117/ManualInput.json)

BlendSpace 编辑器允许把预览点放在中心；该中心是相反方向插值，不代表 Idle。
本轮零输入暂停的行为由新地图的隔离预览负责，人工体验请使用 PIE。
