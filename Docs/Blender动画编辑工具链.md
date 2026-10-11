# Blender 动画编辑工具链

维护日期：2026-10-11；适用 ShootGame、UE 5.6.1、Blender 4.5.8 LTS。
用户已接受当前 -50° 隔离方案的视觉验收；三段素材已迁入正式 Rifle 动画目录。
生产角色已接入临时 K 键动画切换，使用边界见[Rifle 收枪动画表现](Rifle收枪动画表现.md)。
旧实验资源已清理，当前仅保留一个地图及其必要依赖。
历史报告保持原貌；数据删除与清理结果见[清理记录](验证/RifleCleanup_20261011/清理与验收记录.md)。

## 保留的工具与输入

稳定入口：

```text
D:/Unreal_Projects/ShootGame/Scripts/Animation/RifleTools/Run.ps1
```

- blender_bridge.py：UE / Blender 坐标反射、Quaternion、骨骼 FK。
- blender_down.py：从真实端点参考制作 Down30 / Down60，输出烘焙关键帧。
- fbx_settings.json：已验证的轴向、单位、30 FPS 和 FBX 烘焙设置。
- ue_pose_io.py 与 ue_job.py：配置驱动的取样、误差比较和隔离导入。
- analyze_segmented.py：当前方案的选片锁定、正反向时间和姿态指标检查。
- Editor 夹具与 ShooterRifleSegmentedCommandlet：实际三态图预览和测量。

Inputs 目录保留 CorrectedGrip、Down30、Down60 各一份 blend / fbx。
sources.json 保留基准姿态与骨骼层级，reference.json 保留实际端点和枪械几何参考。
manifest.json 记录迁移前后路径与 SHA256；保留输入复制前后完全一致。
三段 UE 动画的导入源路径也更新到了 Inputs，不再引用已删除的 Saved。

```text
D:/Unreal_Projects/ShootGame/Scripts/Animation/RifleTools/Inputs
D:/Unreal_Projects/ShootGame/Scripts/Animation/RifleTools/Examples
```

这些输入是工具重放所必需的数据，不是旧 CSV、截图或临时执行脚本的归档。
原始 Mixamo FBX 与生产动画保留原位置，不能替换此工具的 Manny 基准输入。
当前仅验证 Manny Simple 的 89 骨、26 点素材；完整 Skeleton 有 161 骨。
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
Measure 默认读取当前 -50° 方案，CSV 输出到 Saved/RifleTools/Measure。
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

## 当前唯一地图与选片规则

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
没有新增跨片混合或 IK 求解算法；生产收枪过程释放既有 Aim IK 的表现权重。
UE 5.6.1 SetSequence 不主动重置时间，新入场仍由原状态机初始化。
此前官方依据见[UE 5.6 动画节点函数](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-node-functions-in-unreal-engine?application_version=5.6)。
本夹具采用 Native 更新，实际时序已在保留方案中复验。

在地图运行 Simulate，选择 Segmented50 LowToReady Actor。
Toggle Rifle Test Goal 控制收枪、抬枪和中途反向。
Cycle Segment Pitch 覆盖 -45、-50、-50.01、-55、-60 等输入。
Cycle Rifle Test Natural Pitch Strength 对比强度 0 / 1；默认 IK 关闭。
另一个 Actor 固定 CorrectedGrip，保留作同地图对照。
用按钮操作，避免编辑详情属性导致 Actor 重建和时间重置。

用户已接受当前方案；严格分段的跨角度素材差异及快速改角度后的姿态追赶仍客观存在。
此结论不扩大为完整 Gameplay 或新 IK 算法验收。
生产接入的本轮证据单独记录，不沿用历史实验绿色结果。
历史数值通过[验证索引](验证/README.md)查阅；清理后的旧 Saved 链接不再可用。
