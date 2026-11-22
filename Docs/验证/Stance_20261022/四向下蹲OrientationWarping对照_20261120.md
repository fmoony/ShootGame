# 四向下蹲 Orientation Warping 最小对照（2026-11-20）

本轮只验证左前 45°。工作树为 ShootGame_UE58，分支为 codex/ue-5.8。
引擎为 UE 5.8.4。没有建立分支、暂存或提交。
固定方向与有限过渡已采集，视觉质量等待用户验收。

## 结论与视觉观察

A 保留原来的前、左混合，仍可见步幅抵消、双脚挤向中线和鞋面隐入地板。
B 保留 Forward 的前后迈步与身体起伏，左前时腿部摆动清楚。
C 保留 Left 的侧移跨步与重心变化，左前时也没有 A 的严重挤脚。
B/C 都是值得人工验收的候选，仍保留来源动作自身的鞋面穿地。

建议先人工体验 B：其前进迈步经旋转后，左前的步态意图较清楚。
C 保留作对照；不能只因其鞋面最低值较高就判断其自然度更好。
本轮没有将任一候选替换到原有八向 Demo。

- [原生视口并排循环](OrientationWarping_20261120/Native_ABC.gif)。
- [连续关键姿态](OrientationWarping_20261120/Native_cycle_contact.png)。
- [A 原图](OrientationWarping_20261120/A.png)。
- [B 原图](OrientationWarping_20261120/B.png)。
- [C 原图](OrientationWarping_20261120/C.png)。

三组使用相同裁剪与尺度，展示不同时段的完整循环，没有做人为相位对齐。
原生截图约 6 Hz，动图重复显示已有截图，没有生成中间运动。
该材料用于复查姿态；流畅度和最终自然度仍应在可见编辑器中验收。

## 来源与骨架

当前 UE 序列根目录：

`/Game/Experiments/Stance_20261022/FourDirectionRootLockSource_20261117/`

| 方向 | 实际资产名 | 原始 FBX |
| --- | --- | --- |
| 前 | AS_RootLockSource_Forward | Walk Crouching Forward.fbx |
| 后 | AS_RootLockSource_Back | Walk Crouching Backward.fbx |
| 左 | AS_RootLockSource_Left | Walk Crouching Left.fbx |
| 右 | AS_RootLockSource_Right | Walk Crouching Right.fbx |

原始 FBX 根目录：`D:/GameResource/Model/UETemplate/Animation/Crouch/rootLock/`。
当前导入记录指向项目的 `SourceAssets/FourDirectionRootLockSource_20261117/ImportReady/`。
这些既有导入副本已纳入父层运动，本轮没有重导入、重新烘焙或修改它们。
四段均为 1 秒，Root Motion=false，Force Root Lock=false。

A 的实际输入是原 `BS_FourDirection_Crouch`，Forward=0.5、Right=-0.5。
该 BlendSpace 的四个样本确实引用上述四段 rootLock 来源。
B 只使用 Forward；C 只使用 Left。没有引用旧斜向、镜像或 Refined 动画。

三组网格均为 `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`。
AnimBP 与序列使用实验 `FourDirection_20261117/SK_FourDirection`。
当前重新读取参考姿势与重定向设置，与生产 SK_Mannequin 完全相同，共 89 骨。
Root、Pelvis、五节 Spine、双腿、foot/ball、ik_foot_root 与左右 IK 脚均有效。
没有重建或修改生产 Skeleton。

- [来源核对，插件启用前](OrientationWarping_20261120/Audit.json)。
- [当前 Skeleton 对照](OrientationWarping_20261120/Skeleton.json)。
- [原始 FBX 当前 SHA](OrientationWarping_20261120/SourceProtection.json)。

## 准确拓扑与配置

A：

```text
BS_FourDirection_Crouch (X=0.5, Y=-0.5, 原配置 0.2 秒过滤)
  -> Output Pose
```

B/C：

```text
Sequence Player (B=Forward，C=Left；循环，Rate=1)
  -> Local To Component
  -> Orientation Warping (Manual)
  -> Component To Local
  -> Output Pose

原生 BlueprintUpdateAnimation
  -> FInterpTo(WarpAngle, WarpTargetAngle, DeltaTimeX, 10)
  -> 写 WarpAngle
  -> Orientation Warping 的 OrientationAngle 输入
```

B 的默认角度与目标均为 -45°；C 均为 +45°。
固定模式中二者相等，不产生额外角度插值。
过渡只改变测试图参数，不改变序列相位、关键帧或播放速率。

共同配置：

- Mode=Manual，RotationAxis=Z，WarpingSpace=ComponentTransform。
- Alpha=1，DistributedBoneOrientationAlpha=1，RotationInterpSpeed=0。
- SpineBones=spine_01 至 spine_05，原生分配权重各为 0.2。
- IKFootRootBone=ik_foot_root，IKFootBones=ik_foot_l、ik_foot_r。
- 没有 Leg IK、接地补偿、额外骨骼修正、AimOffset 或 WeaponAction。
- 没有伪造 Root Motion；Graph 模式未使用。

角色前进为世界 +X，左移为世界 -Y；网格相对角色 Yaw=-90°。
按前／左动作语义计算的目标为世界 (+X,-Y)，不假定原始脚轨迹严格为直线。
实测 B 的 Root/Pelvis 相对来源约 -45°，C 约 +45°，验证了实际符号。

本地实现首先将 Root 旋转 Angle × DistributedBoneOrientationAlpha。
因此这里腿链完整旋转 45°，五节脊柱逐节反向分摊 9°。
IK 脚根的额外分量为 1-alpha=0，不需要新 Leg IK 把剩余角度传给 FK 腿。
这与默认 alpha=0.5 的配置不同，不能把本轮结论推广到默认配置。

官方一般示例推荐在节点后接 Leg IK。
本实验为遵守禁止新增 IK 的边界，采用上述有明确限制的原生配置。
Manual 模式接受直接角度；Graph 模式依赖有效 Root Motion 属性流。

- [最终节点、引脚与事件连线](OrientationWarping_20261120/FinalGraphs.json)。
- [Epic Pose Warping](https://dev.epicgames.com/documentation/en-us/unreal-engine/pose-warping-in-unreal-engine)。
- [Epic 原生节点 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/AnimationWarpingRuntime/FAnimNode_OrientationWarping)。

本地核对文件位于 Engine/Plugins/Animation/AnimationWarping 下：
Source/Runtime/Private/BoneControllers/AnimNode_OrientationWarping.cpp。
当前安装版本的源码是旋转分配与 Manual 行为的最终依据。

## 骨盆、上身与循环

相对各自来源，B/C 的骨盆改变约 -45°/+45°。
spine_01 至 spine_05 的累计方向变化依次约为 36°、27°、18°、9°、0°。
因此骨盆与上胸之间新增约 45° 的腰背扭转，而不是让整个上身跟着转 45°。

上胸、肩臂与头继续保留来源自身的逐帧运动，并非固定静态上身。
腰背仍参与逐节协调，但原生反向分配可能有拧腰感，需要人工判断。
本轮没有接持枪动作、Aim IK 或 LeftHand IK，不能据此宣称武器握持兼容。

B/C 均观察到三次循环回绕，没有新增整圈旋转。
来源 Forward 的首尾脚骨差约 0.002 cm，Left 约为零。
没有主动统一骨盆高度、躯干前倾、步幅或支撑比例。

## 无截图数值验证

主要判定来自独立的无截图采集，共 1327 帧。
以下固定方向结果排除前 0.5 秒过滤阶段，每组覆盖约三个周期。

| 指标 | A | B | C |
| --- | --- | --- | --- |
| 固定段有效帧 | 170 | 169 | 169 |
| 最低鞋面世界 Z，cm | -19.71 | -7.39 | -6.08 |
| 鞋面三角形相交帧 | 29 | 0 | 0 |
| 最小双踝三维距离，cm | 2.23 | 19.24 | 21.20 |
| 左 foot 最低 Z，cm | -6.34 | 8.00 | 8.12 |
| 左 ball 最低 Z，cm | -15.38 | 0.49 | 0.30 |

地面实际顶面约 Z=0。鞋面值由参考蒙皮重建，区别于 foot/ball 骨的位置。
三角形相交检测不等于完整物理碰撞验收；没有由数值替代视觉判断。

Forward 来源最低鞋面约 -7.391 cm，Left 来源约 -6.077 cm。
B/C 的结果基本保持来源接地水平，而非通过抬高 Root 获得改善。
A 的额外下沉来自不兼容腿姿混合；本轮旋转单一完整步态绕开了该对冲。

按实际腿姿拟合来源相位，逆旋转后的腿部误差最大约 B=0.20 cm、C<0.001 cm。
这里包含对来源 60 FPS 组件样本插值的拟合误差，不是资产修改误差。
原序列字节未变，膝踝屈伸、周期与支撑节奏继续来自原始动作。

- [固定周期分析](OrientationWarping_20261120/CleanAnalysis.json)。
- [完整运行骨骼数据](OrientationWarping_20261120/Runtime.json.gz)。
- [来源压缩姿态](OrientationWarping_20261120/Sources.json.gz)。

## 有限过渡结果

仅测试 B：前进 0° ↔ 左前 -45°。
仅测试 C：左移 0° ↔ 左前 +45°。
两组各做两次往返，序列持续播放，没有切换主源或重置相位。

90% 角度变化约需 0.217 秒，最终均到达目标。
采样中的最大单帧角度变化约 7.5°，没有直接跳过完整 45°。
B 的过渡最低鞋面约 -7.39 cm，C 约 -6.09 cm，没有恢复 A 的严重下沉。

最大单帧脚骨变化约 8–12 cm，含正常摆腿与少量帧时间波动。
尚不能把这些数据解释为无脚滑或所有转向均自然。
尤其没有验证 B 与 C 之间的主源切换、B 到左移、C 到前进或全部八向连续性。

带截图采样曾出现约 0.14 秒的停顿，使单帧脚骨变化被放大。
上述结果采用后续无截图数据，两类证据分别保留。

[过渡实测](OrientationWarping_20261120/CleanTransitionAnalysis.json)。

## 手动验收

实验目录：`/Game/Experiments/Stance_20261022/OrientationWarping_20261120`。
地图：`L_OrientationWarping`，打开后 Play 即可，不依赖 Saved 脚本。

- 1：A，原前左混合。
- 2：B，Forward 修正为固定左前。
- 3：C，Left 修正为固定左前。
- B 模式中 ↑：前进；↓：回到左前。
- C 模式中 ←：左移；↓：回到左前。
- V：复用原预览相机观察角度；Esc：退出。

2/3 会重新进入各自固定左前配置。
A/B/C 之间是独立测试切换，其硬切不属于本轮过渡验收。
角色固定位置，不处理实际位移、速度匹配或脚滑速度校准。

## 修改范围与保护

新增资产均在本轮隔离目录：

- ABP_OW_A、ABP_OW_B、ABP_OW_C。
- BP_OWPreview：原生数字键与有限过渡控制。
- L_OrientationWarping：由原场景模板创建，复用地面、灯光及预览 Pawn。

项目文件仅新增 Editor 范围的 AnimationWarping 启用项。
移除该项后的字节 SHA 与本轮开始时完全一致，保留原有未提交修改。
既有 1243 文件保护检查只发现这一项目配置变化，没有缺失文件。
四段序列、原 BlendSpace、Skeleton、旧八向资产、生产代码与生产动画图均未改。

新增本报告和本目录证据；仅更新当前实验入口及唯一工作区进度文档。
临时采样、分析脚本和完整原生截图保留于 Saved/OrientationWarping_20261120_01。
没有制作新动画序列、没有新增 C++、没有刷新或操纵 Visual Studio。

[保护检查](OrientationWarping_20261120/Protection.json)。

## 环境问题与验证边界

原生插件初始 DLL 与当前引擎 Build ID 不一致，直接启用后被拒绝加载。
已备份并针对当前预编译引擎重编两个原生模块，未修改插件源码。
通过 UBT 的受限 WriteMetadata 步骤生成该插件清单，未手工篡改 Build ID。
引擎主模块清单与版本文件 SHA 保持不变。
引擎目录内更新了两份插件 DLL、PDB、插件清单及正常构建中间产物。

正常退出时曾发生一次 AnimationEditor 清理崩溃，退出前无脏资产。
首次直接复制并加载内存 World 触发引用清理崩溃，改用原生地图模板流程恢复。
初次更新图未实际接入角度插值，修正为直接创建原生更新事件后重新采集。
桌面窗口录像受到遮挡，已作废并清理；不作为视觉证据。

三个测试 AnimBP 编译通过，最终无错误节点、无脏地图或内容包。
没有运行生产联机、Gameplay、武器动作或完整 Automation 回归。
这些系统本轮未修改，也不属于本轮通过范围。

[原生插件构建与版本保护](OrientationWarping_20261120/BuildEvidence.json)。

本轮到此停止。保留可见编辑器中的隔离预览，等待人工验收。
