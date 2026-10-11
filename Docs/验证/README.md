# Rifle 动画验证索引

本页维护报告位置与迁移说明，不回写历史报告的正文、状态、结论和链接。
2026-10-11 将 Docs 根目录的 10 份 Rifle / Blender 实验报告移到对应验证目录。
各报告迁移前后 SHA256 一致；开发记录和已完成计划继续保留在原目录。
可复用工具的当前入口是[Blender 动画编辑工具链](../Blender动画编辑工具链.md)。

## 当前隔离方案

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
