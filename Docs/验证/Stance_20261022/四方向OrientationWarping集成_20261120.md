# 四方向下蹲 Orientation Warping 完整集成实验

日期：2026-11-20。工作树 ShootGame_UE58，分支 codex/ue-5.8，UE 5.8.4。
本轮为隔离实验，未创建分支、暂存或提交。

## 结论

已建立四原始来源参与的原生 360° 对照图及手动预览。
固定左前保留已验收 B 的表现，固定右后也避开原四向混合的严重腿部对冲。
**完整连续转向未通过：前／左、后／右交接仍有挤脚，并确认一次小腿翻转。**
快速反向输入同样存在较大姿态跳变，不能作为正式 Locomotion 替换方案。

这是当前组合方法的限制，不是四段原动画需要统一身体基准的证据。
未修改骨盆高度、躯干前倾、步幅、支撑比例、原始相位或逐帧运动。
未添加 IK、接地补偿、手写骨骼算法或正式 Gameplay。

## 直接体验

地图根目录：

~~~text
/Game/Experiments/Stance_20261022/Orientation360_20261120/
~~~

打开 L_Orientation360 并运行，默认模式 2：

| 操作 | 行为 |
| --- | --- |
| 1 | 原始四向 BlendSpace |
| 2 | 本轮四来源 Orientation Warping |
| 3 | 上轮已验收 Forward 左前 B，固定方向参考 |
| WASD／组合键 | 原预览 Pawn 实际移动，150 cm/s |
| C | 定点连续方向模式开关 |
| Q／E | 连续模式中减／增角度，90°/s |
| V | 既有三个第三人称观察视角 |
| T | WASD 模式的既有暂停功能 |

松开移动键停止并冻结最后姿态，没有构造 Idle。
连续模式关闭 Pawn 位移，动画仍循环；用于完整角度扫描。
切换独立对照模式会重建 AnimInstance，该比较开关不作为步态过渡证据。
旧地图、旧八向 Demo、上轮 A/B/C 均保留。

## 原始来源与保护

UE 序列公共根目录：

~~~text
/Game/Experiments/Stance_20261022/FourDirectionRootLockSource_20261117/
~~~

| 来源 | 实际序列 | 当前导入副本 |
| --- | --- | --- |
| Forward | AS_RootLockSource_Forward | ImportReady/Forward.fbx |
| Backward | AS_RootLockSource_Back | ImportReady/Back.fbx |
| Left | AS_RootLockSource_Left | ImportReady/Left.fbx |
| Right | AS_RootLockSource_Right | ImportReady/Right.fbx |

导入副本根为 SourceAssets/FourDirectionRootLockSource_20261117。
原始目录为 D:/GameResource/Model/UETemplate/Animation/Crouch/rootLock。
文件分别为 Walk Crouching Forward、Backward、Left、Right.fbx。
既有 ImportReady 已纳入父层运动，本轮没有重新导入或烘焙。

四段均长 1 秒；EnableRootMotion=false，ForceRootLock=false。
使用实验 FourDirection_20261117/SK_FourDirection 和 SKM_Manny_Simple。
实验 Skeleton 与生产参考姿势及重定向设置一致，共 89 骨。
Root、Pelvis、五节 Spine、双腿、foot/ball、IK Foot Root 和双 IK 脚有效。

最终重新读取了四个序列的路径、Skeleton、导入记录及开关。
新图四个 Sequence Evaluator 逐一引用上述资产，不引用旧斜向或镜像序列。
基准图引用原 BS_FourDirection_Crouch；其四个样本同样引用上述来源。
原 ABP_FourDirection 当前引用旧八向镜像 BS，未将它误作四向基准。

[实际资产与连线](Orientation360_20261120/FinalAudit.json)。
[原资源及历史文件保护](Orientation360_20261120/Protection.json)。
[外部原始 FBX 重新核对](Orientation360_20261120/SourceProtection.json)。

## 准确 AnimGraph

每段来源独立执行：

~~~text
Sequence Evaluator：原序列，循环，共用 ExplicitTime=Phase
  -> Local To Component
  -> 原生 Orientation Warping，Manual
  -> Component To Local
~~~

四个结果再组合：

~~~text
Warped Forward -> FR Blend 的 Base
Warped Right   -> FR Blend 的 Layer，MixFR

Warped Back    -> BL Blend 的 Base
Warped Left    -> BL Blend 的 Layer，MixBL

FR -> Final Blend 的 Base
BL -> Final Blend 的 Layer，MixGroups
   -> Output
~~~

三组均为 Layered Blend Per Bone，Root 分支、Depth=0，全身覆盖。
Mesh Space Rotation Blend=true，Root Space Rotation Blend=false。
这不是持枪上下身分层；先分别 Warping 再混合，仍不能保证来源姿态兼容。

基准图：原 BS_FourDirection_Crouch → Output，保留原 0.2 秒方向过滤。
B 参考：Forward Player → LS/CS → Manual OW(-45°) → CS/LS → Output。
均未接 WeaponAction、AimOffset、Arms Slot、Aim IK、LeftHand IK。

### 方向、权重与时间

原生 BlueprintUpdateAnimation：

~~~text
DesiredYaw = atan2Degrees(Right, Forward)
SmoothedYaw = RInterpTo(CurrentYaw, DesiredYaw, DeltaTimeX, 10)，最短路径
Angle(source) = Clamp(NormalizeAxis(SmoothedYaw - SourceHeading), -65, 65)
Phase = Fraction(Phase + DeltaTimeX)
~~~

SourceHeading：F=0°，R=90°，B=180°，L=-90°。
统一周期保留来源各自逐帧时间，没有固定半周期偏移或时间重映射。
共同数值时钟只避免切换时重置播放，不代表左右脚支撑语义已对齐。

A=abs(SmoothedYaw)，S 为 t²(3−2t)，t 为给定区间内的 Clamp 比例：

~~~text
WF = 1 - S(45,65,A)
WB = S(115,135,A)
Side = 1 - WF - WB
WR = Side if Yaw >= 0 else 0
WL = Side if Yaw < 0 else 0
MixFR = SafeDivide(WR, WF+WR)
MixBL = SafeDivide(WL, WB+WL)
MixGroups = WB+WL
~~~

交接带为 +45～65、+115～135、-65～-45、-135～-115 度。
没有固定扇区硬切；50/50 只在曲线交接中点出现。

| 标准方向 | 有效来源 | OW 角度 |
| --- | --- | --- |
| 前 | Forward | 0° |
| 左前 | Forward | -45° |
| 右前 | Forward | +45° |
| 左 | Left | 0° |
| 右 | Right | 0° |
| 后 | Back | 0° |
| 左后 | Back | +45° |
| 右后 | Back | -45° |

### Warping 与脊柱配置

四个 OW 均为 Manual、Z 轴、ComponentTransform 空间。
Alpha=1，DistributedBoneOrientationAlpha=1，RotationInterpSpeed=0。
MaxCorrectionDegrees=65；方向平滑放在上述 RInterpTo，避免重复滤波。
Spine 为 spine_01 至 spine_05，原生祖先链分配各 0.2。
IKFootRootBone=ik_foot_root，IKFootBones=ik_foot_l、ik_foot_r。
没有 Graph Root Motion 输入、伪造 Root Motion Velocity 或额外 Leg IK。

左前相对原 Forward 的累计 Yaw 实测：
Root/Pelvis 约 -45°，五节 Spine 约 -36/-27/-18/-9/0°。
上段补偿后仍播放原有身体起伏和局部运动，没有逐骨锁定躯干。
下腰承担转向；靠近 65° 时扭转更大，需人工判断自然度。
来源切换也会改变原始胸肩运动，最终持枪朝向不在本轮验收范围。

## 固定八方向：数值证据

无截图运行共 5,365 帧，通常约 60 Hz；个别长帧保留真实 dt。
固定方向各播放 2 秒，表中排除前 0.65 秒的收敛；原 B 单独播放 3 秒。

鞋面数据为同一网格的蒙皮重建和双鞋三角面 BVH 相交。
foot/ball 骨骼位置单独记录，不能代替鞋面接地。
鞋面低点受原来源影响，不据此统一骨盆、抬高 Root 或增加补偿。
未验证支撑脚滑速度与正式 CharacterMovement 的匹配。

| 方向 | 基准鞋面最低 cm | 新图鞋面最低 cm | 新图最小踝距 cm | 新图鞋相交帧 |
| --- | ---: | ---: | ---: | ---: |
| 前 | -7.39 | -7.28 | 19.32 | 0 |
| 右前 | -6.90 | -7.28 | 19.33 | 0 |
| 右 | -5.54 | -5.52 | 21.83 | 0 |
| 右后 | -17.95 | -6.44 | 18.93 | 0 |
| 后 | -6.47 | -6.44 | 18.93 | 0 |
| 左后 | -6.86 | -6.49 | 18.93 | 0 |
| 左 | -6.09 | -6.00 | 21.19 | 0 |
| 左前 | -19.49 | -7.42 | 19.09 | 0 |

原四向左前／右后分别有 22/82、16/82 帧鞋面相交。
新图固定八向未检出鞋面相交，不等于来源鞋面穿地已修复。
右前、左后现在来自 Forward、Back，与已验收混合版不同。
原六方向的人工验收只属于原基准，不自动转移到新图。

主方向对来源 61 点插值重建，腿位置误差最大约 0.30 cm。
这是采样重建误差，不能写成逐帧零差。
集成左前对 B 独立原生循环拟合：脚位置差约 0.18 cm 内，脚旋转约 1.19° 内。
胸、头、手位置差约 0.06 cm 内；没有人为重设相位。
这些证据支持左前未明显退化，最终仍等待人工回归验收。

[骨骼与鞋面分析](Orientation360_20261120/RunAnalysis.json)。
[B 回归方法与误差](Orientation360_20261120/BRegression.json)。
[完整运行骨骼](Orientation360_20261120/RunFrames.json.gz)。

## 连续方向：未通过项

权重在 0～1 内，和为 1；Phase 增量与真实 dt 一致。
未发现时钟因来源交接而重置，但输入连续不保证关节姿态连续。

| 范围 | 本轮观察 |
| --- | --- |
| 前 ↔ 左前 ↔ 左 | 确认小腿翻转和脚跳变，不能通过 |
| 前 ↔ 右前 ↔ 右 | 邻向去回未检出鞋相交；自然度待验收 |
| 后 ↔ 左后 ↔ 左 | 邻向去回未检出鞋相交；自然度待验收 |
| 后 ↔ 右后 ↔ 右 | 回程约 126° 相交，环绕 127～129° 再现 |
| 主向及斜向反向 | 最大方向步进约 30°/帧，有明显姿态跳变 |
| 正反两圈 ±180° | 未检出整圈角度误跳；不代表脚部连续 |

### 已确认的前／左翻转

FL_back 的相邻真实采样：

- 角度 -55.499° → -54.750°，间隔约 1/60 秒。
- Phase 0.36672 → 0.38339，时钟正常。
- Forward 权重 0.46257 → 0.51878，曲线正常。
- calf_r 组件旋转变化约 173.39°，foot_r 位置变化约 61.33 cm。
- 两源 Warped 小腿旋转接近 180°。
- 对应四元数点积估计从 -0.0158 变为 +0.0310。

实测证明最终姿态不连续，不能用“方向已平滑”排除。
诊断推断：两来源在该相位与空间下的旋转不兼容，
跨过近 180° 插值关系时，原生网格空间混合也可能改变插值路径。
来源对估计使用 61 点插值，不能直接据此认定引擎缺陷。

原始动作循环首尾的脚位置差最大约 0.041 cm。
因此 61 cm 跳变不能笼统归因于原动画循环接缝。
共同 Phase 只保证数值时间，不解决姿态兼容性。

R_L 反向在 -55.14° 相交；BR_back 在 126.00° 相交。
逆时针两圈在 127.49～128.99° 累计四帧相交，与进入时相有关。
此前 -55° 与 125° 的 61 相位初探也各有两帧相交。
没有通过改变原始相位或修改动画避开失败时相。

四种快速反向测试最大脚／脚掌位移约 48～67 cm/帧。
平滑速度 10 在 180° 反转时，首步仍可达到约 30°。
窄交接带可能一帧被大幅跨过，来源姿态也不兼容，不能标记自然连续。

四处 Yaw 跨 ±180° 时，归一化角差约 1.5°。
对应 Root 四元数变化约 0.91～2.16°，没有 360° 数值误跳。
同帧脚位移仍约 9.39～10.21 cm，不据此宣布步态连续通过。

[逐骨翻转证据](Orientation360_20261120/HandoffFailure.json)。
[环绕与反向数据](Orientation360_20261120/TransitionExtra.json)。
本轮保留失败结果，没有自动增加算法或重制来源。

## 视觉证据与判断边界

- [八向新图原生循环](Orientation360_20261120/Warp_Eight.gif)。
- [原四向基准八方向](Orientation360_20261120/Baseline_Eight.gif)。
- [左前／右后并排循环](Orientation360_20261120/FL_BR_Comparison.gif)。
- [上一轮 B 与集成左前](Orientation360_20261120/B_Regression.gif)。
- [四处来源交接循环](Orientation360_20261120/Handoff_Cycles.gif)。
- [四组邻向去回](Orientation360_20261120/Transitions.gif)。
- [正反向一圈](Orientation360_20261120/Circles.gif)。
- [连续关键姿态](Orientation360_20261120/Contact.png)。
- [跳变参数的原生重放](Orientation360_20261120/FailureReplay.png)。

固定组为同一网格、地面、既有视角 2；连续组为既有视角 1。
Shot 约 6 Hz，截图会卡顿；GIF 统一 170 ms 显示已有截图，没有生成中间帧。
各组独立循环，不人为对齐相位。截图请求与渲染可能相差一帧。
跳变重放使用记录的角度和 Phase，不能冒充实时无截图录像。

视觉上，固定左前／右后前后迈步清楚分开，原混合缩步减轻。
四主向保留原动作；右前／左后变为单源扭转，仍需重新验收。
交接、腰背扭转和快速反向不能仅依据最低鞋面数值判断。

## 文件清单与最终状态

新增 UE 资产仅在 Orientation360_20261120：

- ABP_OW360.uasset：四源原生图。
- ABP_OW360_Baseline.uasset：引用原四向 BS。
- BP_OW360Preview.uasset：独立模式和连续输入控制。
- L_Orientation360.umap：复用原环境的独立地图。

文档新增本报告和 Orientation360_20261120 证据目录。
更新当前实验入口及单份忽略的工作区进度文档。
Saved/Orientation360_20261120_01 保存原始截图、运行数据和脚本。

三份蓝图 BS_UP_TO_DATE，无错误节点，地图与内容无脏包。
原有 2,227 文件对照，仅当前入口和忽略的进度文档有预期改动。
原动画、Skeleton、FBX、副本、生产图、上轮 B、源码及历史报告保持。
四个外部原始 FBX 另行重新计算 SHA；没有改正式 Gameplay 或引擎代码。

真实按键已验证 W、WA、SD 的方向和 150 cm/s 位移。
E 半秒增加约 45°，Q 半秒回到约 0°。
自动采样结束，临时相位和强制输入标志不保存到资产。
交付可见 UE 本轮地图，默认 2、WASD，等待人工验收。

## 实现依据与限制

官方 [Pose Warping][pose] 说明 Manual Orientation Angle 和 Spine 补偿职责。
本地 UE 5.8.4 的 AnimNode_OrientationWarping.cpp 核对 Manual 分支、
Root 旋转、脊柱分配及 IK Foot Root 剩余分配。
原地序列没有采用依赖 Root Motion 的 Graph 求角路径。

四来源选择、共享时钟及网格空间交接是本轮实验设计，
并非 Epic 推荐的完整 Locomotion 架构；数据已证明仍有来源兼容问题。
值得保留的是固定方向单源 Warping，尤其已验收左前 B。
本轮停止等待审查，不更换六个已验收方向，不进入生产接入。

[pose]: https://dev.epicgames.com/documentation/en-us/unreal-engine/pose-warping-in-unreal-engine
