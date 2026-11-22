# Stance 下蹲来源

当前默认实验使用 StanceAware_20261116/Refined 的原生 AnimBP。
NativeGraph 与下述来源分层保留为 StanceBaseline 对照。
[本轮姿态、步态与 AO 制作输入](StanceAware_20261116/README.md)。
C++ 不再采样、混合或回写骨骼，来源文件均保留。
[当前运行方式与结果][stance]。

## 原始 Source

五份原始 FBX 按用途命名，字节不变，校验值见 Manifest.json。
Source 导入资产用于来源对照，不修改正式 Skeleton。

- CrouchIdle：原始单膝跪姿 idle。
- CrouchLeft／CrouchRight：左右半蹲横移。
- CrouchBack：半蹲后退。
- CrouchForwardLeft：半蹲前左。
- 名字含糊的 Walk Crouching Right 不参与派生。

## 完整父层烘焙

ParentBaked 保存五份完整来源动作的 blend、FBX、Expected 和 BakeSummary。
父层对象运动烘焙到真实 root，保留初始 XY、朝向和水平净位移。
通过标准 FBX 导入，直接绑定生产 SK_Mannequin。
完整版本用于来源对照，不在原地 locomotion 中直接累加位移。
本轮未修改该目录。

## SourceLayer 原地播放对照输入

LayerPlayback 从 ParentBaked 派生五段原地输入，并镜像补前右。
只移除移动片段净水平行进，保留原始高度、偏航及肢体动作。
原 idle 保留为单膝跪姿，没有伪装成半蹲 idle。
标准 FBX 导入资产位于 SourceLayer，AnimBP 使用原生 BlendSpace Player 播放。

NativeGraph 的原生 TP 图负责序列、方向混合、持枪分层和 AO。
FP 原生 Copy Pose 读取最终 Body，L 可对照仅下半身和完整分层。
生产自定义 AimIK／左手 IK 没有接入本实验，生产实现未修改。

EightWay 保留上一轮四斜向和补充正前；本轮没有重新制作这些来源。
Mirrored 新增右前→左前、左后→右后的两段直接镜像，并接入当前 BlendSpace。
当前仍为八个移动方向加中心 idle，左前与右后引用 Mirrored，其余样本保留。
镜像保持 60 FPS、61 点、1 秒循环，没有 IK、接地、优化、平滑或时间移相。
源片段与原始 Source、生产 Skeleton 均保持不变。

本次运行主方向采样中，左前与右后的双脚表面相交为零。
来源鞋面穿地和相邻方向混合穿地仍存在，完整接触质量未通过。
按用户约束，没有以额外求解掩盖这些问题。
[当前镜像证据](../../Docs/验证/StanceMirror_20261116/README.md)。

## 保留的旧步态制作输入

BlenderGroundingSeed.json.gz 保存初次实验骨骼轨道。
NativeGaitReference.json.gz 保存原生八向 Rifle Walk 及来源对照。
BlenderGrounded/StanceGrounded.blend 保留 92 个 Action。
其 FBX、Authored.json 和旧步态均保留作历史对照，当前 AnimBP 不引用这 92 段。

blender_grounding.py、blender_flat_feet.py、blender_native_gait.py 是既有 Blender 工具。
修改源文件前应复制候选，--output-dir 必须指定 Saved 中新目录。
旧脚掌与自然度报告只适用于各自版本，不适用于当前原始动作分层。

旧 C++ ApplyResources／Prepare／Rebuild 等制作入口及 LegacyLower 回退已退役。
子目录中既有制作说明按当时流程保留，当前运行入口以主现状文档为准。
当前只支持原生 AnimBP 的 PrepareNative、Preview、Verify、CaptureNative。

[stance]: ../../Docs/验证/Stance_20261022/滑铲与下蹲动画实验.md
