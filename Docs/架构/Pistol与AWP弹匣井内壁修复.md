# Pistol 与 AWP 弹匣井内壁修复

- 日期：2026-09-29。
- 范围：SKM_Pistol 与 SKM_AWP_Sniper_Rifle 的固定枪体内衬。
- 状态：局部补面已保存，源模型重载和 Weapon 回归通过；用户确认视觉验收通过。

## 根因与补面范围

两枪不是本轮 Magazine 权重归属错误，原 mixed_boundary_triangles 均为 0。
AWP 分离弹匣后留下 18 边开口，枪体没有对应的内侧封闭结构。
Pistol 握把底部有 10 边开口，隐藏弹匣后可从特定角度透视到外部。
不处理瞄准镜、枪口、扳机等与此次拔匣无关的开放区域。

Pistol 沿握把内部插匣方向补 6.8 cm 深的局部内衬，新增侧壁和朝入口的顶面。
新增权重全部绑定既有 Grip 骨骼。
AWP 从分离边界向枪身内部补 1.2 cm 深的弹匣井，新增侧壁和内部顶面。
新增权重全部绑定既有 Root 骨骼。
两处入口仍开放，内衬其他边界封闭；不在入口直接放平面堵住拔匣空间。

独立内衬复制入口轮廓，保留原边缘顶点与外侧法线。
新增顶点绑定固定枪体骨骼，不连接任何 Magazine 顶点。
材质沿用相邻面材质槽，新增 UV0 使用相邻 UV，原有 UV0 和法线不重写。
按源资产的 UE 顺时针正面设置三角形绕序，内顶面与侧壁法线朝向井内。
没有将整枪材质改为 Two Sided。

## 几何数量

以下为 LOD0 源模型数量；两枪均只有一个 LOD。

| 武器 | 顶点前后 | 三角形前后 | 新增侧壁 / 顶面 |
| --- | --- | --- | --- |
| Pistol | 3166 → 3187 | 5590 → 5620 | 20 / 10 |
| AWP | 6695 → 6732 | 12397 → 12451 | 36 / 18 |

Pistol 的 Magazine 源顶点仍为 178，弹匣三角形仍为 128。
其中仅 66 个源顶点被弹匣三角形引用，其余未引用顶点亦未删除或重新归属。
AWP 的 Magazine 源顶点仍为 102，弹匣三角形仍为 180。
两枪 mixed_boundary_triangles 仍为 0。

## 验证证据与边界

- preview 不落盘预演、apply 保存和独立进程 verify 均通过。
- 对照所有原三角面的角点位置、UV0、法线、材质和权重，多重集合完整保留。
- 骨骼参考变换、所有 Socket、材质槽及 LOD 数量保持一致。
- 新增内衬没有退化面、绕序冲突或非流形边，唯一开放边界为入口。
- D3D12 RenderData 重载核验通过，两枪可见弹匣权重均为 1，枪体不含 Magazine 权重。
- Pistol 渲染弹匣顶点 150、三角形 128；AWP 为 238、180，混合边界均为 0。
- Proxy 装回最大误差分别为 0.0000002144 cm 和 0.0000001171 cm。
- RenderData 检查进程退出 0；八条告警为冷缓存纹理处理，不是网格或蒙皮错误。
- SM_Pistol_Magazine、SM_AWP_Magazine、Rifle、GL 与 DT_WeaponData 哈希未变。
- Weapon 回归 16 项成功：5 项无告警、11 项带告警，0 项失败。
- 报告目录：Saved/Automation/Reports/20260929_153917_ShootGame.Weapon。
- 未修改 Runtime C++、Notify、AnimBP、抓握配置、弹药逻辑或网络。

离线深度栅格图使用背面剔除，不是实际 PIE 截图，也不包含原材质贴图和照明。
三角度插匣对照中，Pistol 差异像素为 0。
AWP 有一个角度出现 10 个入口边缘差异像素，其他两角度为 0。
这是有限分辨率的几何抽查，不能据此宣告所有角度外观完全相同。
2026-09-29 用户确认本轮视觉验收通过，并授权提交。
人工验收结论来自用户反馈，不将离线图或自动化测试描述为 Agent 执行的 PIE 验收。

## 脚本与保存边界

脚本：Scripts/Development/RepairPistolAWPMagazineWells.py。
默认 preview；命令行 -MagazineWellMode=apply 保存，verify 只读重载。
preview / apply 仅接受修复前的两枪几何基线，修复后拒绝重复补面。
verify 依赖本机 Saved/MagazineWells 中保存的修复前快照。
这不是任意枪械的通用自动建模工具。

执行时临时启用 PythonScriptPlugin、GeometryScripting 和 SkeletalMeshModelingTools。
不修改项目插件配置，应用前关闭 Editor 并确认进程退出。
只保存两个目标 SkeletalMesh，不保存 Skeleton、材质、独立弹匣或其他资产。
应用前备份：Saved/MagazineWells/Before_20260929_153743。
现有 ABP_FP_Pistol 用户改动保留，未编辑或提交。

API 以 Epic 的 UE 5.6 文档及本机 MeshAssetFunctions.cpp 为依据。
use_original_vertex_order 保留源结构，避免无关重排及丢失未引用顶点。

- [MeshEdits 5.6 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/GeometryScript_MeshEdits?application_version=5.6)
- [CopyMeshToAssetOptions 5.6 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/GeometryScriptCopyMeshToAssetOptions?application_version=5.6)

## 排查与修正记录

首次只读审计未临时加载 GeometryScripting，Python 类型不存在；补齐加载参数后通过。
法线查询返回 tuple，按当前 UE Python 的实际返回值解包后通过。
首次离线图正面判定采用反向绕序，交叉检查源法线后修正，保存前重新验证。
Pistol 源模型本身有未引用顶点，保留这一历史结构，验证日志仍会提示告警。
额外 RenderData 检查必须启用 AllowCommandletRendering，不能用 NullRHI 作为蒙皮证据。
第一次检查未提供这一标志，RenderData 缺少骨骼属性；补齐后独立重载通过。
真实 RHI 的冷缓存初始化编译了默认着色器，等待完成后确认检查进程退出。
没有强制终止用户的 Editor，等待进程退出后才保存。
