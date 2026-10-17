# Rifle 动画验证索引

本页维护报告位置与迁移说明，不回写历史报告的正文、状态、结论和链接。
2026-10-11 将 Docs 根目录的 10 份 Rifle / Blender 实验报告移到对应验证目录。
各报告迁移前后 SHA256 一致；开发记录和已完成计划继续保留在原目录。
可复用工具的当前入口是[Blender 动画编辑工具链](../Blender动画编辑工具链.md)。

## 2026-10-17 的 Compact 跑姿接入与清理

已验收的 Compact 三段素材进入 Characters/Rifle，生产 ABP_TP_Player 使用标准 Additive。
SprintSway、RifleRun、RifleRunCompact 共 16 个实验资产已删除，原 Segmented50 地图恢复。
正式动画、稳定 blend / fbx、现有自动化测试和历史报告保留。
四个专用 Saved 目录及当天 RifleReadyIntegration CSV 已清理，旧 HTML / 截图链接不再有效。
历史报告原文与路径保持，以[清理核对](RifleRunCleanup_20261017/验收清理.md)为准。

## 当前方案与验收清理

2026-10-13 用户接受单动画 Local 肩臂分层及 K 网络预览，正式行为见[当前说明][current]。
本轮只保留一个 Segmented50 回退地图，Local / Mesh / Boundary 原型及一次性取证工具已移除。
重复截图、JSON、Mask 转储和旧 Saved 采集数据已清理；历史报告与首发提示词保持原貌。
历史材料中已删除的证据路径不再可用，不重写原文或制造重复副本。
当前回归测试仍可运行，最新清理结果见[本次开发记录][accepted]。

## 2026-10-14 的姿态生产接入

用户接受 ShoulderSeam；Relaxed 动画和头颈 Runtime 节点已接入正式 ABP_TP_Player。
五轮肩部头部实验资产、地图、一次性取证源码与 Saved 原始采集资料已清理。
必要 Blender 输入与调整脚本进入 RifleTools；历史报告及开发记录保持原貌。
生产行为见[当前说明][current]，本轮范围和结果见[接入记录][posture]。

## 2026-10-13 的历史审计与实现

- [LowReady 复用 Reload 分层可行性审计](RifleLowReadyLayeringAudit_20261013/可行性审计.md)。
- [单动画分层原型](RifleLowReadyLayeringPrototype_20261013/原型实施与验收.md)。
- [胸腔相对与动态俯仰原型](RifleChestRelativePrototype_20261013/原型实施与验收.md)。
- [真实 K 收枪组件取证](RifleFPOcclusion_20261011/组件级取证.md)。
- [网络预览实施与验证](RifleReadyNetworkPreview_20261013/实施与验证.md)。

## 2026-10-11 的迁移与分段回退

- [动画迁移与生产接入](RifleProduction_20261011/迁移与生产接入记录.md)。

- [视觉验收与资源清理](RifleCleanup_20261011/清理与验收记录.md)。

用户已接受 -50° 方案；仅保留 Segmented50 当前地图及必要依赖。
其余实验资产与旧 Saved 原始采集数据已清理，历史报告保持原貌。
历史报告中的旧 Saved / 已删除实验资产路径不再可访问，详见清理清单。
制作所需的最小输入已迁到 RifleTools/Inputs，当前命令以工具文档为准。

- [-50° 分段 LowToReady 验证](RifleSegmented50_20261011/方案验证.md)。
- [旧 -60° 分段方案验证](RifleSegmented_20261011/方案验证.md)。
- [Down30 / Down60 制作报告][down]。
- [Blender 往返与姿态修正报告][blender]。
- [左臂软约束报告][soft]。

## 2026-10-10 的审查与实验

- [上半身分层审查][layer]。
- [动画与 WeaponAction 预审查][weapon]。
- [俯仰与 BlendMask 审查][pitch]。
- [Natural Mask 最小实验][mask]。
- [三态方案 A 运行时报告][state]。
- [第一人称调试蓝图报告][fp]。
- [正式角色视角对齐报告][camera]。

## 历史链接说明

迁移不会改变报告中的绝对 Saved、源码或证据路径。
俯仰遮罩报告原有的相对图片路径以 Docs 根为基准，迁移后不再自动解析到原图片。
按照历史文档保护规则保留该原始链接，在本索引提供[原图][mask-image]的新入口。
旧根路径失效时，按上列文件名查找对应报告；不创建重复正文副本。
迁移清单保存于旧 RifleSegmented_20261011 验证目录，可按旧路径核对新路径和内容哈希。

[down]: RifleDownPitch_20261011/Rifle负俯仰专用收枪动画制作与隔离验证_20261011.md
[blender]: BlenderRifle_20261011/Blender工具链与Rifle动画修正实验_20261011.md
[soft]: RifleSoftGrip_20261011/Rifle左臂自然运动与握点软约束校准_20261011.md
[layer]: RifleLayering_20261010/Rifle上半身分层与AimTransition专项审查_20261010.md
[weapon]: RifleWeaponAction_20261010/Rifle新动画与WeaponAction预审查_20261010.md
[pitch]: Rifle俯仰与BlendMask_20261010/Rifle俯仰姿态与BlendMask可行性专项审查_20261010.md
[mask]: Rifle俯仰遮罩最小实验_20261010/Rifle俯仰遮罩最小实验实施与验证_20261010.md
[state]: Rifle方案A隔离原型_20261010/Rifle方案A隔离原型运行时验证_20261010.md
[fp]: Rifle第一人称调试蓝图_20261010/Rifle第一人称可附身调试蓝图_20261010.md
[camera]: Rifle实验视角继承对齐_20261010/Rifle实验视角继承正式角色对齐_20261010.md
[mask-image]: Rifle俯仰遮罩最小实验_20261010/LowReady_实际图.png

[current]: ../Rifle收枪动画表现.md
[accepted]: ../开发记录/2026-10-13-1803-Rifle验收清理与提交.md

[posture]: ../开发记录/2026-10-14-1224-Rifle姿态生产接入与清理.md

当前动画目录已统一：Shooter/Animation 仅存放 AnimBP，素材与 Rig 位于 Characters。
详见[迁移记录](../开发记录/2026-10-14-1622-动画资源目录统一与Rifle验收提交.md)。
