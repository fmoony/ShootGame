# 原生行走参考与下蹲步态修订

2026-11-15；UE 5.8.4；Blender 4.5.8 LTS；实验地图 Stance_20261022/L_StancePreview。

## 本轮结果

参照项目实际八向 MF_Rifle_Walk 资源，在 Blender 修订蹲走腿部。
原来的 88 段移动已更新，另补 309.375° 的 Aim / LowReady 两段。
当前共 92 段动画、92 个 Action，两套 BlendSpace 各 46 个样本。

原生参考用来指导膝盖弯曲、腿骨轴向旋转、脚步中心与摆腿曲线。
保留原下蹲高度、骨盆及上身局部轨道，未恢复运行时腿部接地修正。
本轮没有替换正式 AnimBP、正式动画或生产角色。

同一份分析脚本对照本轮开始前的实际运行与最终运行：

| 稳定移动指标 | 修改前 / 128 方向 | 最终 / 128 方向 |
| --- | ---: | ---: |
| 最差场景双膝水平弯曲方向夹角 P95 / ° | 137.534 | 25.441 |
| 双膝弯曲方向夹角超过 90° 的帧 | 1,908 | 0 |
| 最大场景均值脚步中心偏移 / cm | 31.613 | 16.590 |
| 最差场景固定前掌脚滑 P95 / cm/s | 3.602 | 2.547 |

双膝夹角由 hip、knee、foot 的实际位置求弯曲平面，再投影到水平面。
它不是解剖膝关节角度，不能单独证明自然度。
脚步中心是两个 ball 骨相对 pelvis 的水平偏移，再对每个场景取均值。
该指标用于识别脚步偏离骨盆，没有把自然站姿要求成零偏移。

原生八向 RAW 参考的夹角 P95 约为 8.7°～36.9°，详见 NativeReferenceSummary.json。
最终仍是较深下蹲的改编动作，不等同于原生站立行走直放。
画面和人工键鼠试走继续用于自然度验收。

## 为什么原来不自然

早期制作把支撑和摆腿轨迹重建成统一步幅，并用固定空间点指定膝盖方向。
前次平脚修订沿用了这些腿部弯曲平面。
因此接地门禁通过时，仍可能出现膝盖向两侧分叉、横移脚步偏离骨盆。

本轮先实际求值项目的八向 Rifle Walk，再与五段 Source 下蹲及当前轨道比较。
原生资源带根位移，且包含两个步态循环，不能直接拿世界位置作为脚步目标。
前掌轨迹极值也不能直接作为触地事件，脚掌翻滚会让极值偏离支撑时刻。

处理在真实 Blender Action 中完成：

1. 去除参考的水平根位移，统一原生循环和左右脚相位。
2. 以脚骨低高度区间识别支撑参考，再适配实验的 65% 支撑比例。
3. 取原生膝盖弯曲方向，同时迁移大腿、小腿轴向旋转。
4. 让脚步中心回到原生相对骨盆的位置，按实验移速安排支撑行进。
5. 采用原生摆腿曲线，并匹配起落脚边界速度、保留支撑期平脚。
6. 烘焙为关键帧，实际 bpy 求值导出，再由 UE 资产系统导入。

## 失败迭代与处理

只迁移膝盖方向的 V2 已消除反向分叉，但局部脚滑 P95 达 7.194 cm/s，
稳定鞋底最低为 -0.713 cm，门禁失败，没有降低阈值。

V3 连同原生腿骨轴向旋转迁移，脚滑降至 2.966 cm/s，
仍在左前方样本间出现 -0.791 cm 的最低点。
新增 309.375° 的两段 Blender 动作后，保持原门禁通过。
V2Failed.flat.json、V3Failed.flat.json 保留这些失败结果。

C++ 只修改新增样本登记、导入数量计算和有界方向采样参数。
FixtureChangeAudit.json 将这三处改动反向还原后，哈希与前次夹具完全一致。
运行姿态求值、骨盆、腿部接地与瞄准处理代码没有随本轮改变。

## 最终运行验证

两档独立实际运行，每帧采集 14 个 Body 骨骼或参考点：

- 128 方向：264 场景、43,808 帧，用于与修改前逐行对照。
- 256 方向：520 场景、85,203 帧，覆盖新增样本两侧的更细方向。
- 合计 129,011 帧；两个档位的平脚门禁均通过。

两档中的最差场景指标一致：

| 鞋底参考面指标 | 实测 | 门禁 |
| --- | ---: | ---: |
| 支撑三点绝对高度 P99 / cm | 0.382 | ≤1 |
| 支撑三点高度差 P99 / cm | 0.278 | ≤0.6 |
| 鞋底法线倾角 P99 / ° | 1.026 | ≤2 |
| 固定前掌世界脚滑 P95 / cm/s | 2.547 | ≤5 |
| 稳定移动最低点 / cm | -0.450 | ≥-0.5 |
| 稳定双脚高于 1 cm 的帧 | 0 | 0 |

参考点由实际 foot 位置、旋转和参考鞋底局部坐标重建。
后跟重建与实际 heel 行最大差小于 0.000002 cm，腿部骨长误差也低于该值。
这是骨骼参考平面，不是网格逐顶点碰撞或地面 Trace。
支撑平贴的相位范围和阈值沿用前轮，新增逐场景最低点输出没有改变判定。

92 段资源 RAW / COMPRESSED、循环、骨长及源上身局部轨道保护通过。
92 个 Action 回读误差最大约 0.0000303 cm、0.0000665°。
UE 轨道与 bpy 世界位置往返差最大约 0.001360 cm。
两段 idle 的 Authored 轨道保持相等；29 项正式、来源与原生参考资产哈希未变。

新增样本会细分 BlendSpace 三角形，原上身源轨道保持，但运行姿态并非逐位一致。
43,808 帧中骨盆、头、双手位置差最大 0.00683 cm（约 0.0683 mm），
旋转差最大 0.00989°，集中在新样本附近及动态压力场景。
严格 0.001 的旧一致性检查未通过；UpperDifference.json 保留实测，标为 Measured。
不把它改标为完全一致，源轨道保护结果另见 VerifyBlender.json。

最终 9 张 D3D12 移动画面已重新采集，配套 1,054 帧移动采样。
方向、握持和 FP 不回写 TP 检查通过。
截图只辅助观察，不替代运行数据或人工自然度验收。

## 工具、依据与复测

NativeGaitReference.json.gz 是实际 UE RAW 求值的稳定输入，包含 13 段参考。
五段 Source 仅用于已导入状态对照，步态曲线取自八向 Rifle Walk。
export_gait_reference.py 只读导出；blender_native_gait.py 在新的 Saved 目录制作候选。
analyze_gait.py 测量膝盖弯曲方向和脚步中心，analyze_flat_feet.py 保持接地门禁。

Epic 的 [Sync Groups 说明][sync] 要求混合动作的触地相位对应。
本轮夹具是 PoseableMesh，采用离线相位统一，没有接入 AnimBP Sync Group。
BlendSpace 权重来自本地 UE 5.8.4 GetSamplesFromBlendInput，结果由运行骨骼检查。

[sync]: https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-sync-groups-in-unreal-engine

~~~powershell
$engine = & './Scripts/Development/ResolveShootGameEngine.ps1'
$python = "$engine/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
& './Scripts/Animation/Stance/Run.ps1' -Mode Verify
$csv = './Saved/StanceGait_20261115/Runtime.csv'
& $python './Scripts/Animation/Stance/analyze_flat_feet.py' $csv --require-flat-feet
& $python './Scripts/Animation/Stance/analyze_gait.py' $csv
~~~

加密采集命令使用 -StanceGroundDirections=256，并写入新的 Saved 输出。
复核 Ultra.csv 时，两份分析脚本均指定 --directions 256。
分析压缩证据时建议用 --output 将步态分析结果写到 Saved，保留历史证据原貌。

## 文件与边界

- Runtime.csv.gz / Ultra.csv.gz：两档完整原始运行骨骼数据。
- Before.gait.json / Runtime.gait.json / Ultra.gait.json：自然度线索对照。
- Runtime.flat.json / Ultra.flat.json：逐场景平脚和接地门禁。
- NativeReferenceSummary.json：原生八向行走的实际求值统计。
- NativeGaitSummary.json / ExtraDirectionSummary.json：88 段及新增两段的 Blender 制作结果。
- BlendReadback.json / VerifyBlender.json：92 段源文件和 UE 轨道回读。
- Protection.json / UpperDifference.json / FixtureChangeAudit.json：资产和源码边界。
- AssetChanges.json / Images / MoveSummary.json：包变化、最终画面和移动检查。
- Build.log / IncludePaths.log / TextLayout.log / Manifest.json：构建、检查与来源校验。

启停最低鞋底约 -4.062 cm，前次同口径为 -3.832 cm，过渡穿地仍未通过。
需要专用过渡动作；稳定步态修订不能替代该验收。
生产 AO / AimIK 复刻、坡面、碰撞、网络、换弹、贴墙与 Cook 未在本轮验收。
验收编辑器以正常可见窗口恢复，后台检查进程完成后退出。
源码头文件路径检查与 Staged 文本版式检查通过，三条解析警告已人工核对。
使用独立临时 Git 索引，真实暂存区保持空白，未创建提交。
