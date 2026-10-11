# Blender 工具链与 Rifle 动画修正实验（2026-10-11）

本轮完成后台 bpy、FBX 往返、骨骼关键帧修正及 UE 隔离三态测量。
结论适用于本轮 Manny 动画，尚不构成完整 Gameplay 视觉验收。
未提交或推送，未修改生产动画、Skeleton、AnimBP、Gameplay 或 Socket。

## 工具状态与方案

- Blender：4.5.8 LTS，构建 d7359176d0d3。
- 路径：`D:/Blender/blender-4.5.8-windows-x64/blender.exe`。
- 内置 Python：3.11.11；实际后台 bpy、FBX 导入和导出均成功。
- UE：5.6.1，CL 44394996；路径 `E:/Unreal_Engine/UE_5.6`。
- 使用独立后台 Blender，参数 `--background --factory-startup`。
- Codex 配置含 unreal-engine、codegraph、node_repl，cua_repl 已禁用。
- 可调用 CodeGraph、node_repl 和应用连接器，未连接 Blender MCP。
- 视口运行期间 Codex Unreal MCP 实际完成初始化；编辑器退出后该端点离线。
- 关停前后一次 exists 工具调用返回 HTTP 502，未据此宣称业务调用成功。
- 未发现 Blender MCP 插件或 uv、uvx；后台 bpy 不依赖它们。
- 已有 Mixamo Converter 插件，本轮未启用或修改 Blender 用户配置。

已检查用户指定上游的 README、pyproject 和 add-on 实现：

[mcp-for-blender 上游](https://github.com/ahujasid/mcp-for-blender)

检查时版本为 2.1.9，要求 Python 3.10 以上，依赖 mcp 和 httpx。
它支持实时场景查询和通用 Python 执行，可操作 Pose、Action 与 F-Curve。
但动画往返仍须自行核对骨架、坐标、采样和关键帧。
默认本机端口 9876 无认证与加密，通用代码执行权限较大。
安装器可能改变客户端配置、插件和偏好设置，本轮未运行安装器或开放端口。
后台 bpy 已满足批量 FBX、离线辅助求解、关键帧烘焙和重复执行需求。
因此本轮选择后台 bpy，未让 MCP 成为唯一阻塞条件。
现有 Skill 未发现专用 Blender 动画工具，使用 karpathy-guidelines 控制实现范围。

UE 外部依据如下，并核对本机 UE5.6 实现：

[FBX Animation Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-animation-pipeline-in-unreal-engine?application_version=5.6)

[AnimPoseExtensions](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AnimPoseExtensions?application_version=5.6)

[FbxAnimSequenceImportData](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/FbxAnimSequenceImportData?application_version=5.6)

Blender 官方网页本轮获取失败；实际选项以本机 FBX 插件源码和运行结果为准。

## 基线与编辑来源

已读取项目规则、MCP 外部 Agent 契约及已有 Rifle 审查和过渡实验记录。
审计覆盖 AimToLowReady、LowReady、MF_Rifle_Idle_ADS、AO_Rifle、Manny 和 Rifle 握点。
数据见 `ue_baseline.json`、`original_blender.json`、`ue_metadata_preview_grip.json`。
所有证据文件均在 `Saved/BlenderRifleExperiment_20261011/`。

UE 元数据指向 `D:/Game Model/UETemplate/RifleAimToDown.fbx`。
它已有 Manny 名称的 89 骨骼，不能称为未经转换的原始 Mixamo Skeleton。
本轮未找到并验证转换前的 Mixamo FBX。
该 Action 为 30 FPS、1–46 帧、1.5 秒，549 条 F-Curve、18954 个关键点。
对象存在旋转、平移及 0.01 缩放；通道关键点数量为 1 或 46。
89 骨骼名称和父子关系与 SKM_Manny_Simple 一致。
完整 SK_Mannequin Reference Skeleton 则有 161 骨骼，二者不能混为一谈。
Rest Pose、轴向、对象矩阵、Root 和 pelvis 已记录。

UE 派生动画为 26 点、30 FPS、0.833333313 秒，RateScale 为 1。
原导入使用 Interchange，偏移零、导入缩放 1、自动采样、Timeline 范围。
实际使用 Manny Skeleton 和其运行时重定向设置。
本轮同时测量 raw、retargeted 姿态，避免仅靠导入设置推测效果。

编辑来源选用 UE 导出的派生动画 `RifleAimToLowReady_UE_Reference.fbx`。
它保留 UE 已截取、转换和采样的姿态，不重新推导 Mixamo 转换过程。
UE 导出 FBX 为 7300；虽然版本不同于官方常用版本，实际往返已验证。

## 往返验证

UE 导出后直接重新导入的控制组最大 Component 位置误差为 0.000134 cm。
随后执行 Blender 导入、原样导出和 UE 隔离导入。
F-Curve 摘要在导出前后一致，未编辑关键点。

初始管线发现两个实际问题：

1. Blender 将 FBX root 转成 Armature 对象，只保留 88 个 Pose Bones。
   改名为 Armature 会失去 UE 所需 root 映射，首次导入因此失败。
2. UE 导出在 Blender 显示 1–27 帧，含额外端点。
   必须显式导出 1–26 帧，才保持 26 点及原动作长度。

从源 FBX 截取再导出虽然能够导入，却丢失约 0.099358 cm root 偏移。
因此未采用该路径作为编辑来源。
保留对象名 root、对象动画和正确帧范围后，管线通过。

最终往返产物为 `RifleAimToLowReady_RoundTrip_RootPreserved.blend/.fbx`。
UE 资产名为 `MM_Rifle_AimToLowReady_RootPreserved_Test`。
FBX 导出为 -Y Forward、Z Up、scale 1、apply unit、FBX_SCALE_NONE。
关闭自动骨骼定向、leaf bones、NLA、all actions 和 simplify，逐帧烘焙。
完整参数见 `ue_roundtrip_fixed_blender.json`，失败版本保留用于追溯。
导入使用实验 Skeleton 副本，禁止更新生产 Reference Pose。

比较 89 骨骼首尾及全部 26 点，并在独立 UE 进程以 60 Hz 复核 51 点：

- 最大 Local 平移误差：0.000228 cm；旋转：0.000785°。
- 最大 Component 平移误差：0.000338 cm；旋转：0.000797°。
- 最大缩放误差：约 1.8e-6；握点位置误差：0.000274 cm。
- 时长、采样数和 Root Motion 设置一致，压缩后样本有效。
- root 恒定 Z=0.099358 cm，首末 root 位移差为零。

参考取自 UE 原动画，以 AnimPoseExtensions 求值。
Local 为父骨骼空间；其 WORLD 枚举在独立动画求值中是累积 Component 空间。
该含义已核对本机 AnimPose.cpp，并非关卡 Actor World。
误差以同一 Manny Simple Mesh、UE 厘米和四元数夹角计算。
握点使用实际角色及 Rifle Socket，运行时转到同一世界空间比较。
Blender 到 UE 的反射 Y、米到厘米及骨骼方向映射也已逐帧验证。
映射最大位置误差 0.000353 cm、旋转误差 0.000657°。
往返通过后才修正关键帧；独立复核见 `ue_final_readback.json`。

## 修正方法与产物

ArmedAim 参考来自既有实验 AnimBP 实际 ArmedAimPose 缓存求值。
夹具在 pitch 0°、IK 关闭、Natural Pitch Strength 0 下保持约 1 秒。
记录缓存帧 59 的全部 Local Transform，而非 ADS 首帧或静态 AO 样本。
没有修改 MF_Rifle_Idle_ADS、AO_Rifle 或 ArmedAimPose。

调整 13 骨骼：spine_01–05，双侧 clavicle、upperarm、lowerarm、hand。
帧 0 对齐 ArmedAim，以五次平滑曲线在帧 18 前衰减到原动作。
保留 root、pelvis、下肢、局部平移和缩放；帧 18–25 保持原动作。
旋转逐帧烘焙为骨骼四元数关键点，导出不依赖 Blender Constraint。

第一版仅修端点，0° 左手握点峰值从 6.414 cm 增至 9.218 cm。
因此第一版保留为失败对照，不作为最终推荐。
第二版离线求解左 upperarm、lowerarm、hand 的双骨骼关系。
在帧 0–17 衔接真实 ArmedAim 与原动作的左手相对右手轨迹，保留肘部方向。
无不可达目标截断或退化平面；结果同样烘焙为旋转关键点。
没有新增或修改 UE Aim IK、LeftHand IK 算法。

最终 Blender 文件：`RifleAimToLowReady_Corrected_Grip.blend/.fbx`。
所有新 UE 资产位于 `/Game/Experiments/RifleTransition/Blender_20261011`：

- 动画：`CorrectedGrip/MM_Rifle_AimToLowReady`。
- AnimBP：`ABP_RifleBlender_GripCorrected_Test`。
- 对照场景：`L_RifleBlender_GripCompare_Test`。

动画短名保留是既有夹具查找 Transition Player 的要求，目录完全隔离。
新 AnimBP 仅替换一处过渡动画引用，三态、转换条件和 Blend Mask 权重均不变。
新动画最终引用既有 SK_Mannequin 供实验求值，没有修改 Skeleton 文件。
中间往返控制组仍引用实验 Skeleton 副本。

## UE 测量与局限

原版与最终版分别测试 0°、+30°、−30°，Natural Pitch Strength 0 和 1。
每例包含 ArmedAim、正向降枪、LowReady、反向抬枪及 ArmedAim 恢复。
12 例的状态、动画时间和播放速率记录完全一致。
保持 0.833333 秒及正向、反向播放，正式播放倍率和状态机规则未改。

0°、额外补偿关闭时：

- 进入过渡第一步右手位移：1.925→0.102 cm；左手：3.591→0.172 cm。
- 降枪最大手部单步位移：12.055→4.235 cm。
- 反向抬枪最大手部单步位移：11.708→2.825 cm。
- 左手对实际 Rifle 握点的过渡峰值：6.413722→6.413688 cm。
- hand_r 首帧 Component 方向对 ArmedAim 误差：54.658→0.00154°。
- spine_05：33.008→0.00107°；upperarm_l：66.539→0.00093°。

帧 18–25 最大 Component 位置差小于 0.00041 cm，旋转差小于 0.0008°。
局部平移和缩放仅有往返浮点误差。
相邻关键帧最大旋转 10.06°，原版 8.75°，未发现瞬间翻转。
低姿态末端及 Root Motion 保持原状，未改变动作意图和时间方向。

Natural Pitch Strength 1 在 0° 也保留改善，握点峰值约 6.639 cm。
−30° 仍有局限：关闭补偿时峰值 9.938→10.667 cm。
启用现有补偿时峰值 9.922→10.673 cm，该角度握持关系尚未改善。
无 IK 的 ArmedAim 本身约有 5.68 cm 握点间距，LowReady 约 7.02 cm。
不能宣称双手已贴合握点，不能代替完整 Gameplay 视觉验收。
下一轮优先继续本段帧 0–18 的左上臂、前臂及手掌轨迹，并覆盖上下俯仰。

测量汇总：`analysis_summary_grip.json`。
逐帧证据：`corrected_grip_runtime_poses.csv`、`corrected_grip_runtime_metrics.csv`。
实际视口结果记录于 ScreenshotsVerified/capture.json，仅作隔离场景静帧证据。
左侧为原版，右侧为最终修正版；每张图记录实际状态、动画时间和速率。
最初视口未按预期推进动画，第一次显式推进又受编辑属性通知重建影响。
因此新增夹具采用直接设置意图与固定 60 Hz 推进，避免状态机被属性编辑重置。
前两轮截图保留作失败证据，不用于证明正反播放。
最终 305 个状态、时间和速率记录两侧完全一致，六张图片已生成并检查。
反向截图时间为 0.066667 秒，速率为 −1；所有本轮启动的编辑器均已退出。

## 重复执行与保护边界

隔离工具位于 `Source/ShootGameEditor/Tests/BlenderRifle/`。
脚本、日志及产物均位于 `Saved/BlenderRifleExperiment_20261011/`。
执行顺序：

1. `ue_audit_export.py` 导出 UE 基线。
2. `blender_roundtrip.py` 输入 UE Reference FBX，模式 `ue_roundtrip_fixed`。
3. `ue_import_verify.py`、`ue_import_followup.py` 导入并测量往返。
4. `ShooterRifleBlender -Reference` 记录实际 ArmedAimPose。
5. `blender_correct.py -- --grip` 生成最终修正版。
6. `ue_import_corrected.py -GripCandidate` 导入最终版。
7. `ShooterRifleBlender -Prepare -GripCandidate` 创建新实验 AnimBP。
8. `ShooterRifleBlender -Verify -GripCandidate` 执行三态测量。
9. 内置 Python 执行 `analyze.py --grip`；`ue_final_readback.py` 独立复核。

Blender 参数：`--background --factory-startup --python <script> -- <args>`。
UE Python 参数：`-run=PythonScript -EnablePlugins=PythonScriptPlugin -script=<script>`。
Commandlet 参数：`-run=ShooterRifleBlender` 加上述模式。
UE 测量使用 `-unattended -nop4 -nosplash -nullrhi`。
缓存用 `-DDC=NoZenLocalFallback`，LocalDataCachePath 指向实验目录下 DDC。
导入及生成资产默认拒绝覆盖目标；重跑需新隔离目录或只读复核。
保存操作限制到本轮实验根目录。

环境问题及恢复均保留日志：

- 默认 Zen 缓存权限失败，改用工作区缓存，未改配置。
- NullRHI 下 Mesh FBX 导出断言，取消不必要的 Mesh 导出，仅导出动画。
- 工程刷新遇到日志及引擎 Intermediate 权限，授权提升后恢复成功。
- 新源码已刷新工程，未操纵 VS；若提示重载，由用户自行处理。
- 实际视口首次启动遇到 Shader 临时目录权限，授权提升后重试。

Editor 编译、Source include 路径检查和文本差异检查通过。
1024 个既有文件 SHA256 未变化，见 `protection_final.json`。
原 FBX、正式 uasset、已有实验、配置、Socket 和 Gameplay 均未覆盖。
本轮未 commit 或 push，结束后停止，不推进其他武器动画。

当前具备本轮骨架和动画的非破坏性编辑、导出及 UE 自动测量能力。
仍需含 IK 的 Gameplay 视觉验收，也不能保证任意 Mixamo 骨架自动兼容。
