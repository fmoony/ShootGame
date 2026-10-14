# HeadForward 头部 Roll 隔离微调

日期：2026-10-14。保留现有 HeadForward，新增 Upright 副本供视觉验收。
生产资源、spine、Blend Mask、肩部动画、Local Space 收枪逻辑与原 IK 均未修改。

## 修正方法

复用 FAnimNode_RifleHeadForward 的头颈 Local Additive，仅增加默认关闭的 bUpright。
旧图保持关闭；新 ABP_Head_Upright 的 AimTransition／LowReady 节点开启。
先执行原有 Yaw 校准，再计算当前面部前向 F 与头部上方向 U。
目标上方向为角色 Up 在 F 的法平面上的归一化投影。
校正角取绕 F 从 U 到目标上方向的 atan2 有符号最小角。
按 neck_01／neck_02／head 的 20%／30%／50% 分配，并转换到各自 Local Delta。
三段旋转共用当前面部前向轴，因此不改变已校准的面部方向或 AO 仰角。
没有采用胸腔相对侧倾减少 80%，没有把头部固定到世界水平。
ArmedAim 不执行节点，过渡沿用真实播放器进度的平滑权重，反向同步恢复。

## 同相位数值对照

Body Component Space、Hold=0.25 s、稳定 LowReady，比较原 HeadForward 与 Upright：

| 输入 Pitch | 原 Roll | 新 Roll | 保持的头部 Pitch |
|---|---:|---:|---:|
| 0° | -16.185° | 近零 | -32.612° |
| -60° | -22.988° | 近零 | -71.404° |
| +60° | -2.355° | 近零 | +43.300° |

Roll 定义是头部上轴相对角色竖直投影的扭转，绕当前面部前向测量。
新 Roll 接近零意味着参考头部横轴水平；实际网格与自然度仍需视觉验收。
54 组、16,200 帧，覆盖三方案、三个 Pitch、三个 Hold 与完整／中途反向。
新副本九组中途反向通过，状态与累计时间和原 HeadForward 相同。
最大前向向量差 3.4e-10，Yaw 差 5.2e-8°，Pitch 差 1.8e-8°。
稳定 LowReady 最大 Roll 残差小于 2.0e-6°，ArmedAim 受测位置与旋转差为零。
root／pelvis／spine、肩部与双手的受测位置和旋转差为零。
头部骨骼最大位置变化 1.414 cm，新增状态边界单帧位移小于 0.005 cm。
以上数值来自实际 AnimGraph，关闭 IK；没有修改 Skeleton 或动画素材。

## 真实角色与 FP

复用原控制器复制体、原摄像机、FP CopyPose／Clearance、原 IK 和 K 网络预览接口。
真实蓝图按键选择逐例核对实际方案与 AnimClass，采集 90 图及 45 个 FP 视点。
截图覆盖 ArmedAim、收枪中段、LowReady、反向中段与返回 ArmedAim。
TP 通过实际 PIE→SIE 脱离取得，最后一轮使用正对角色的固定机位。
交互场景仍用 F8 自由观察，没有新增 TP 控制台相机。
最终 TP／FP 的 LowReady 头部 Roll 都接近零，摄像机朝向差为零。
额外视点位移相对原 HeadForward 为 0.193／0.275／0.009 cm，依次对应 0°／-60°／+60°。
这是新增 Roll 的位移，不能与此前 HeadForward 相对无修正基准的位移混淆。
运行时中段对照并非逐帧同步，最终最大头部 Yaw 采样差约 0.0032°。
严格同步的实际图测量已确认面部前向保持；不将采样差误归因于 Roll 改变 Yaw。
相机挂在 head，仍需用户观察自然播放、动态 Pitch 与快速反向时的镜头舒适性。
截图与骨骼指标不替代完整 Gameplay 遮挡验收。

## 实验资源与操作

根目录：`/Game/Experiments/RifleTransition/HeadUpright_20261014`。
地图：L_Rifle_HeadUpright；AnimBP：ABP_Head_Baseline、ABP_Head_Forward、ABP_Head_Upright。
新控制器与 GameMode 保留原协议，资产名称为 BP_HeadTestController／BP_HeadTestGameMode。
F6 无头部修正，F7 原 HeadForward，H 新 Upright，K 收枪／反向，P 切换三个 Pitch。
在所选视口运行单人 PIE，F8 或脱离按钮观察 TP，再次操作返回原 FP。
Debug 额外显示 Roll；脱离时 FP 暂时隐藏、返回恢复，沿用已验证的实验显示机制。
详情按钮沿用原函数名：PreviousRoll 对应本轮方案 2，FaceForward 对应本轮方案 3。
推荐使用 F7／H 和屏幕方案名核对当前资源。

所有截图、CSV、JSON 和交互对照页位于 Saved/RifleHeadUpright_20261014。
入口为 RifleHeadUpright_Comparison.html，支持三个 Pitch、五阶段、FP／TP 和头部放大。
编译与最终真实 PIE／脱离回归 Success，零错误、一条既有 GAS 路径配置警告。
正式交付证据采用 AutomationFinal 的报告与同轮 PNG。
Editor 工具新增可选开关与取证模式，没有新增源码文件，因此不需要刷新 VS 工程结构。
1,099 个既有文件的保护范围中，仅两份 Editor HeadForward 工具源码允许变化。
原实验资产、生产资源及所有历史文档保持哈希一致，未提交或推送。

本轮到此停止，等待用户验收端正程度、原俯仰保留、正反连续性和 FP 视野。
