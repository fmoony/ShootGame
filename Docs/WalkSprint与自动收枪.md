# Walk / Sprint 与自动收枪

适用 UE 5.6；本轮基线为 GitHub origin/main 的 38be3be。

## 玩家行为与优先级

- 默认 Walk 250 cm/s，按住左 Shift 为 Sprint 600 cm/s，松开恢复 Walk。
  参数位于角色的 CharMoveComp → Shooter / Movement。
- Sprint 只在地面、有移动输入且确实移动时生效；站立、腾空和蹲伏不收枪。
- Fire 按下立即退出 Sprint；未抬到位时缓存最多一次首发，真实播放器到达瞄准端点后提交。
  即使提前松开也保留首发；持续按住则首发后按原全自动节拍继续。
  首发窗口为 0.55 秒，重复点击不叠加或续期；超时、动作中断或武器失效即取消。
- Fire Held 始终优先于 Shift，包括单发武器按住、空弹匣和动作阻塞期间。
  Fire 松开后等待首发消费或作废；随后仍按 Shift 且条件允许时恢复 Sprint。
- Reload、Equip、Dead 的 GAS 标签均阻止 Sprint；Reload / Equip 结束后按现有输入恢复。
  死亡、失去占有和 EndPlay 清空本 Pawn 的移动意图，不带入新 Avatar。
- 无武器也允许 Sprint，但没有 LowReady。
  Rifle、AWP、GrenadeLauncher 沿用已验收的 Rifle 素材；Pistol 保持原持姿。

## 实现边界

ShooterCharacterMovementComponent 替换角色的默认 CMC，保留原有行走物理和网络协议。
WalkSpeed / SprintSpeed 是本地类配置，服务器不接受客户端提交速度数值。
Shift / Fire Held 使用 FSavedMove 的 Custom_0 / Custom_1 原生压缩位。
首发等待使用 Custom_2 位，提前松开 Fire 后仍阻止 Sprint；服务器不接受客户端速度数值。
输入边沿不能合并；纠正重放恢复历史输入，重放结束再恢复当前按键及首发等待位。
服务器每次移动独立检查动作状态，认可后只向 Simulated Proxy 复制实际 Sprint。
Owner 自行预测；位置同步、误差校验和网络平滑继续由原生 CMC 承担。
服务器另向 Owner 复制 Reload / Equip / Dead 的移动阻塞结果。
这是移动门控的确认状态，不建立新的 GAS 标签或修改 Ability 事务。
现有 ServerOnly Equip 的短暂标签不保证到达 Owner，不能据此假定客户端已知道该窗口。
Owner 在收到阻塞前可能短暂误预测，收到后按 Walk 上限重放并收敛。

Enhanced Input 复用用户创建的 /Game/Input/Actions/IA_SprintAction。
IMC_Default 将 LeftShift 映射至该资产，角色蓝图的 SprintAction 配置同一资产。
Controller 沿用现有 DefaultMappingContexts 加载机制，优先级仍为 0。
IMC_Weapons 的旧 Shift 切枪映射已由用户删除，滚轮切枪继续生效。
运行时不再创建 SprintAction 或独立 SprintMappingContext。
输入 Started / Completed / Canceled 覆盖完整生命周期。
Pawn 结束时只回收移动与首发意图，不移除 Controller 的共享 IMC_Default。
WASD、Projectile、弹药预测与伤害权威保持原链路。
Character 在 PC 的 PostProcessInput 中解释首发，到位后只把 Edge 或 Held 交给原 ASC。
已松开时同帧交付 Edge 与 Release，不建立 Held；一次尝试即消费，不自行重试权威拒绝。
GA_Fire 的本地资格额外检查真实姿态，防止已有 Held 在抬枪期间绕过门控。
服务器沿用原身份、弹药、动作与射速裁决；Dedicated 不依赖渲染动画来决定权威结果。
动画等待是本地确定的输入规则，不新增服务器动画时钟或开火事务路径。
Reload、Equip 输入当帧取消首发；外部动作标签、当前武器变化及 Avatar 失效在解释前作废。
首发尚未执行就中断或超时时，需重新按下 Fire；已开始的全自动仍沿用原输入规则。

正式姿态目标 bRifleLowReadyRequested 来自实际 Sprint；本地与服务器分别计算。
Observer 只消费权威 Sprint，不存在第二套预览目标。
现有 ArmedAim ↔ AimTransition ↔ LowReady 图不变。
Local Space 分层、真实 t/T 播放器、头颈修正、IK 和中途反向继续由原图执行。
正式 Fire 请求抬枪并等待端点；原预览模式的 Fire / Montage 硬跳回分支已删除。
过渡序列 MM_Rifle_AimToLowReady_Relaxed 的原生 Rate Scale 保存为 2.6。
在序列编辑器的 Asset Details → Animation → Rate Scale 调整，不配置代码时长。
约 0.833333 秒素材双向全程约 0.320513 秒；运行时播放器只选择 +1 / -1 方向。
中途反向保留累计时间和选片，原始骨骼采样、Local Space 与 IK 均不改动。
输入最早读取上一帧已完成的真实端点，因此完整抬枪到首发含一至两帧调度余量。
Local Space、头颈、肩部、IK、原始骨骼轨迹及状态机图均保持原样。
Reload、Equip、Dead 和不支持武器仍通过原 Abort 边界交还动作状态机。

## 正式整合与旧预览退出

ShooterRifleReadyPreviewComponent 已删除，Character 不再创建 RifleReadyNetworkPreview。
Toggle / Cancel RPC、复制预览目标、OnRep、组件 Tick 和 K 蓝图入口一并退出。
ShootGame.RifleReady.DebugPreview 与 NetPreview 均不再注册。
AnimInstance 仅消费正式 Sprint，射击就绪不再有调试模式直接放行分支。
没有新增复制姿态组件；Owner 预测、服务器移动校验、Observer 状态继续由 CMC 承担。
纯动画采样使用 Editor 测试夹具，生产对象没有测试目标覆盖入口。

## 依据与当前项目差异

Epic 的网络移动说明要求拥有端记录 SavedMove，服务器复演移动并校验位置。
本轮用其现有压缩标志扩展输入，没有另建移动 RPC 或自定义序列化协议。
本机 UE 5.6 源码已核对 MoveAutonomous、ClientUpdatePositionAfterServerUpdate。
前者先解压输入再模拟，后者先 PrepMoveFor 再重放；原生只恢复 Jump / Crouch 输入。
本项目额外恢复 Shift / Fire，避免纠正后历史输入覆盖当前松开沿。

原角色 MaxWalkSpeed 为 600，BlendSpace 已读取 LocomotionGroundSpeed 与 MoveDirection。
Walk 档继续复用该 BlendSpace 和速度输入。
正式整合只调整过渡序列原生速率，并保存 K 入口及旧组件引用清理后的蓝图。
原 K 网络组件只同步姿态 bool；正式 CMC 链路接管后，该组件及旧入口已移除。

- [Epic UE 5.6 网络移动](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine?application_version=5.6)
- [Epic UE 5.6 Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine?application_version=5.6)

## 验证入口

ShootGame.Movement.Sprint.SavedMoves 验证压缩位、输入边沿与池化清理。
ShootGame.Movement.Sprint.Network 分别创建真实 Dedicated NetDriver 和两个客户端世界。
Emulated 在服务器和客户端均启用 100 ms 延迟与 2% 丢包。
各端使用原 CharacterMovement 与正式 AnimBP，真实 W / Shift 输入驱动走跑。
夹具只增加隔离地板，避免生产地图边界打断长距离移动；不修改生产地图。
故意污染拥有端位置后，要求观察到实际纠正与位置收敛。
无延迟时可能已没有待重放 Move；延迟场景额外要求真实 SavedMove 重放次数增加。
新增首发验证涵盖到位前零射击、提前松开一发、重复点击不续期、超时和动作取消。
RifleReady.GameplayNetwork 使用真实 W / Shift / Fire，覆盖三种支持武器的网络模式。
RifleReady.PIE 覆盖六个俯仰的正式走跑与反向，不再调用 K 或预览 RPC。

走跑接入证据见[原开发记录](开发记录/2026-10-16-1115-WalkSprint与正式收枪.md)。
前一版加速与首发结果见[原记录](开发记录/2026-10-16-1309-RifleReady加速与首发缓冲.md)。
正式整合与资源配置证据见[本轮记录](开发记录/2026-10-16-1613-RifleReady正式整合与资源速率.md)。
肩臂观感、头颈自然度、第一人称遮挡及滑步程度仍需人工视觉验收。

## 加速与首发的依据

Epic Sequence Player API 允许负速率反向；本机 UE5.6 更新播放率时保留累计时间。
当前素材原生 RateScale 为 2.6；运行时代码不计算加速倍率，也不写资源播放速率。
Epic 的 TryActivateAbility 先检查资格，再执行一次激活；LocalPredicted 仍由服务器最终裁决。
本项目把等待放在输入层，未把 GA_Fire 改为跨动画窗口存活的 Ability。

- [UE5.6 动画序列原生 Rate Scale](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AnimSequenceBase?application_version=5.6)
- [Sequence Player 速率](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/Animation/FAnimNode_SequencePlayer_Standal-/GetPlayRate)
- [UE5.6 Gameplay Ability](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-gameplay-abilities-in-unreal-engine?application_version=5.6)

## 第一版人工游玩验收

- 对比正常 Shift 收枪和松开 Shift 抬枪，确认原生资源速率 2.6 的双向观感是否合适。
- 跑动中单击后立即松开，确认抬到位后只打一发，仍按 Shift 时随后恢复跑步。
- 跑动中持续按 Fire，确认首发到位后接原全自动；松开后停止连发。
- 连续快速点按 Shift，并在抬枪一半重新跑步，观察肩臂、头颈与握把是否自然。
- 平视及向上、向下瞄准重复上述操作；第二客户端观察远端动画和滑步程度。
- 首发未执行时立即换弹或切枪，确认取消后没有突然补枪。

自动化证明输入、播放器时间、次数与网络收敛；不代替主观手感和姿态自然度验收。

输入资产接入与最新回归见[开发记录](开发记录/2026-10-16-1640-Sprint输入资产接入.md)。
