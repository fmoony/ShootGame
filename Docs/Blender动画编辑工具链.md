# Blender 动画编辑工具链

维护日期：2026-10-17；适用 ShootGame、UE 5.6.1、Blender 4.5.8 LTS。
当前生产采用 Local 收枪与已验收的 Compact 贴身跑姿，包含 Base、Transition、Additive。
角色动画位于 Characters/Mannequins/Anims/Rifle，Shooter/Animation 只保留 AnimBP。
生产由实际 Sprint 驱动，K 预览已退出；见 [Rifle 收枪动画表现](Rifle收枪动画表现.md)。
跑姿实验与采集资料已清理，Segmented50 地图恢复为原分段回退用途。
历史报告保持原貌；数据删除与清理结果见[清理记录](验证/RifleCleanup_20261011/清理与验收记录.md)。

## 保留的工具与输入

稳定入口：

```text
D:/Unreal_Projects/ShootGame/Scripts/Animation/RifleTools/Run.ps1
```

- blender_bridge.py：UE / Blender 坐标反射、Quaternion、骨骼 FK。
- blender_down.py：从真实端点参考制作 Down30 / Down60，输出烘焙关键帧。
- blender_shoulders.py：重放验收的 clavicle 局部调整与双臂协调，保持原手部目标。
- blender_seam.py：用全段五次平滑曲线匹配 LowReady 首帧，保留过渡首帧与时长。
- fbx_settings.json：已验证的轴向、单位、30 FPS 和 FBX 烘焙设置。
- ue_pose_io.py 与 ue_job.py：配置驱动的取样、误差比较和隔离导入。
- analyze_segmented.py：当前方案的选片锁定、正反向时间和姿态指标检查。
- Editor 夹具与 ShooterRifleSegmentedCommandlet：实际三态图预览和测量。

Inputs 保留 CorrectedGrip、Down30、Down60、LowReady_Relaxed、AimToLowReady_Relaxed 的 blend / fbx。
ShoulderSources / ShoulderChanges / SeamSources 是两套姿态调整脚本的必要输入。
sources.json 保留基准姿态与骨骼层级，reference.json 保留实际端点和枪械几何参考。
manifest.json 记录迁移前后路径与 SHA256；保留输入复制前后完全一致。
三段 UE 动画的导入源路径也更新到了 Inputs，不再引用已删除的 Saved。

```text
D:/Unreal_Projects/ShootGame/Scripts/Animation/RifleTools/Inputs
D:/Unreal_Projects/ShootGame/Scripts/Animation/RifleTools/Examples
```

这些输入是工具重放所必需的数据，不是旧 CSV、截图或临时执行脚本的归档。
原始 Mixamo FBX 与生产动画保留原位置，不能替换此工具的 Manny 基准输入。
已验证 Manny Simple 的 89 骨、26 点过渡和 258 点 LowReady；完整 Skeleton 有 161 骨。
Saved 只用于新的输出和缓存，不再作为工具输入依赖。

原生工具目录：

```text
D:/Unreal_Projects/ShootGame/Source/ShootGameEditor/Tests/BlenderRifle
D:/Unreal_Projects/ShootGame/Source/ShootGameEditor/Tests/RifleTransition
```

废弃的 Blender / SoftGrip / DownPitch / 早期 Transition 取证 Commandlet 已移除。
BlenderPreview 和 TransitionFixture 保留，供当前地图及分段工具继承。
Segmented Commandlet 现在只测量保留方案，拒绝旧 Prepare 操作。
这些类只供 Editor 实验使用，不进入生产角色和网络流程。

## Compact 稳定源文件

正式三段素材的 blend / fbx 位于 Inputs/RunCompact，Manifest.json 记录路径与 SHA256。
文件复制自用户已验收的 F4 版本，复制后逐文件校验；新 UE 资产导入源已指向该目录。

| 正式动画 | 用途 |
|---|---|
| MM_Rifle_LowReady_Compact | 258 点呼吸底姿态 |
| MM_Rifle_AimToLowReady_Compact | 26 点双向过渡，原生 Rate Scale=2.6 |
| MM_Rifle_Sprint_Compact_Additive | 54 点循环，Local Additive |

blend 保存完整烘焙骨骼关键帧，可在副本中继续调整并按已验证设置导出 FBX。
重导入仍先用隔离资产验证，不能直接覆盖正式素材或 Skeleton。
Additive 的 UE 参考必须为 Compact LowReady 第 0 帧；FBX 本身不保存 UE Additive 配置。
旧 Run.ps1 的分段重放入口不负责重新生成 Compact，不能拿旧算法覆盖这三段素材。
本轮只保留可编辑源文件，没有把依赖 Saved 的一次性生成脚本包装为通用工具。

## 运行方式

已测试的本机工具路径：

```text
D:/Blender/blender-4.5.8-windows-x64/blender.exe
D:/Blender/blender-4.5.8-windows-x64/4.5/python/bin/python.exe
E:/Unreal_Engine/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe
```

使用后台 bpy；不需要连接 Blender MCP，也不占用打开的 Blender 场景。
长任务开始前按 AGENTS.md 确认 UE 已保存，且用户本轮不会自行更改内容。
离线保存或编译前确认本项目 Editor 进程完全退出，不操作其他项目或 VS。

从项目根目录运行：

```powershell
& './Scripts/Animation/RifleTools/Run.ps1' -Mode Blender `
    -Job './Scripts/Animation/RifleTools/Examples/blender.json'
& './Scripts/Animation/RifleTools/Run.ps1' -Mode Sample `
    -Job './Scripts/Animation/RifleTools/Examples/sample.json'
& './Scripts/Animation/RifleTools/Run.ps1' -Mode Import `
    -Job './Scripts/Animation/RifleTools/Examples/import.json'
& './Scripts/Animation/RifleTools/Run.ps1' -Mode Measure
& 'D:/Blender/blender-4.5.8-windows-x64/4.5/python/bin/python.exe' `
    -B './Scripts/Animation/RifleTools/analyze_segmented.py'
```

复制 Examples 后改为自己的新输出路径，不能原样重复覆盖已有导入或取样。
Blender 需要 source_blend、sources_json、reference_json、output_dir 四个绝对路径。
Sample 需要 operation、asset、output_json；Import 还需 fbx、destination、skeleton。
Sample / Blender 输出必须是本项目 Saved 下的新文件或目录。
Import 复制 Skeleton 后导入，只允许新的 /Game/Experiments/RifleTransition 子目录。
不覆盖生产 Skeleton 或既有动画，26 点 / 0.833333 秒不匹配时拒绝继续。
Measure 默认读取保留的 -50° 分段夹具，CSV 输出到 Saved/RifleTools/Measure。
分析默认阈值 -50°；若指定自定义目录，第二个参数为该实验的 Down60 阈值。
Measure 可以更新测量 CSV，不保存 UE 资产；缓存位于 Saved/RifleTools/DDC。
兼容旧 -Down50 参数，但不再提供已清理的旧 -60° 方案测量入口。

## 坐标与输出验证

- 保留 Armature 对象名 root，禁用自动骨轴和额外叶骨。
- 使用已验证的 -Y / Z 轴向和单位设置。
- 烘焙步长 1、简化 0、30 FPS、帧 1–26；输出不依赖 Blender Constraint。
- 修改旋转前验证坐标桥，保护未编辑骨骼的 Local Translation / Scale。
- 导入后分别比较 Raw、Retargeted、Root、Socket、时长与采样频率。
- 枪械与手部统一转换到 Body Component Space，使用真实附着关系。
- 实际 AnimGraph 验证端点、混合、累计时间及正反向；静态预览不替代它。

误差容限只能识别管线损坏，不能自动判定动作自然。
LowReady 是动态素材，端点比较必须指定参考时间和相位。
手指及父空间变化仍需视觉检查；无 IK 实验不代表完整 Gameplay 验收。

## 当前正式方案

正式采用单 Relaxed 过渡、动态 ArmedAimPose 与 Local 肩臂分层，网络 K 接口保持原样。
HeadForward + Upright 已移入 Runtime，五轮微调的 Editor 原型类和实验资源已移除。
操作、网络限制、关闭与移除方法统一维护于[Rifle 收枪动画表现](Rifle收枪动画表现.md)。
旧分段 ProductionApply 及一次性 Local 接入工具已移除，避免覆盖当前正式图。
只读 ProductionMeasure 与 Blender 重放工具保留；下节分段规则仅用于回退夹具。


## 肩部与衔接的可复用调整

从项目根目录运行；输出必须是 Saved 下尚不存在的目录，脚本拒绝覆盖：

```powershell
& 'D:/Blender/blender-4.5.8-windows-x64/blender.exe' `
    --background --factory-startup --python-exit-code 1 `
    --python Scripts/Animation/RifleTools/blender_shoulders.py `
    -- LowRaised D:/Unreal_Projects/ShootGame/Saved/RifleTools/NewShoulders
& 'D:/Blender/blender-4.5.8-windows-x64/blender.exe' `
    --background --factory-startup --python-exit-code 1 `
    --python Scripts/Animation/RifleTools/blender_seam.py `
    -- SmoothSeam D:/Unreal_Projects/ShootGame/Saved/RifleTools/NewSeam
```

两条命令已实际重跑并成功导出。RoundTrip 模式仅复现各自参照，不应用调整。
肩部脚本使用 ShoulderChanges 的局部 Quaternion；现有值来自相同 Hold 的 ArmedAim 参照。
衔接脚本使用 SeamSources 的 transition / low 数据，并不自动读取新导出的肩部 FBX。
下一轮改变 LowReady 后，应先按原取样工具更新参照，再重新匹配端点与隔离导入验证。
这两条命令不修改 UE 资产；不将生成 FBX 自动覆盖正式动画。
正式两段动画的 Import Source 已指向 Inputs 中对应 FBX；blend 用于继续编辑烘焙轨道。

## 保留的分段回退夹具与选片规则

```text
/Game/Experiments/RifleTransition/Segmented50_20261011/L_RifleSegmented_LowToReady
/Game/Experiments/RifleTransition/Segmented50_20261011/ABP_RifleSegmented_LowToReady
```

| 入场俯仰 P | 素材 |
|---|---|
| P < -50° | Down60 |
| -50° ≤ P ≤ -30° | Down30 |
| P > -30° | CorrectedGrip |

沿用 ArmedAim ↔ AimTransition ↔ LowReady 三态图。
收枪新入场从 0 秒正向播放；从 LowReady 抬枪时按当前俯仰选片并从末端反向播放。
活动过渡中只翻转速率，不更换素材或重置时间；新入场才重新选片。
选片在 NativeUpdateAnimation 中、状态机更新前执行。
没有新增跨片混合或 IK 求解算法；原生产收枪期 Aim IK 权重释放机制继续沿用。
UE 5.6.1 SetSequence 不主动重置时间，新入场仍由原状态机初始化。
此前官方依据见[UE 5.6 动画节点函数](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-node-functions-in-unreal-engine?application_version=5.6)。
本夹具采用 Native 更新，实际时序已在保留方案中复验。

在地图运行 Simulate，选择 Segmented50 LowToReady Actor。
Toggle Rifle Test Goal 控制收枪、抬枪和中途反向。
Cycle Segment Pitch 覆盖 -45、-50、-50.01、-55、-60 等输入。
Cycle Rifle Test Natural Pitch Strength 对比强度 0 / 1；默认 IK 关闭。
另一个 Actor 固定 CorrectedGrip，保留作同地图对照。
用按钮操作，避免编辑详情属性导致 Actor 重建和时间重置。

用户此前接受分段方案；其跨角度素材差异及快速改角度后的姿态追赶仍客观存在。
此结论不扩大为完整 Gameplay 或新 IK 算法验收。
生产接入的本轮证据单独记录，不沿用历史实验绿色结果。
历史数值通过[验证索引](验证/README.md)查阅；清理后的旧 Saved 链接不再可用。
