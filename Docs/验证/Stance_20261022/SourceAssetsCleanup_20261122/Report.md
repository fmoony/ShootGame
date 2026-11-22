# 下蹲实验 SourceAssets 制作材料清理

日期：2026-11-22。工程为 ShootGame_UE58，分支为 codex/ue-5.8。
本轮承接最终版本验收，仅清理 SourceAssets 中的旧下蹲制作材料。
用户确认 UE 已保存、本轮不编辑；未关闭、重启或修改 UE 资产。
提交继续暂停，本轮未暂存或创建提交。

## 清理结果

- 清理前：347 项，1294.98 MiB。
- 删除：285 项，1271.90 MiB。
- 保留原有文件：62 项，23.08 MiB。
- 新增 SourceAssets/README.md 后共 63 项，体积仍约 23.08 MiB。
- 当前实验的 14 项 UE 资产全部保持，不改变动画、Skeleton 或平滑参数。

清理的是旧派生材料，不是当前地图仍使用的源文件。
完整逐项路径、理由、体积与 SHA256 见 Plan.json。

## 保留范围

- Audio 的 21 项声音源、授权及说明。
- rootLock 四方向制作包全部 18 项，含原始 FBX、导入副本、blend 和工具。
- 较早四方向的 4 份 Original FBX、Sources.json 与 README。
- 最早 StanceCrouch 的 5 份原始 FBX、Manifest.json 与原制作说明。
- 所有旧方案 Markdown 说明均保留原文。

共保留 13 份用户原始 FBX，8 份与两份 Sources 清单、5 份与 Manifest 分别核对。
13 份原件与全部保留文件的本轮前后哈希一致。

## 删除范围

- BlenderGrounded 中旧 92 段接地制作与中间数据。
- StanceAware_20261116 中已停止采用的步态、Idle、持枪、AO 与候选版本。
- ParentBaked、LayerPlayback、EightWay、BackRight、Mirrored 的旧派生输出。
- 较早四方向方案的旧 ImportReady 与辅助导入脚本。
- BlendMirrorEight 与三个上身镜像变体的派生 blend、FBX 和数据。
- 对应旧流程的采样种子、Expected、BakeSummary 等中间文件。

空目录仅在逐项删除后清除，没有递归删除整个 SourceAssets。
保留目录中的原始输入和所有 Markdown 均未删除。
Scripts 下的历史工具本轮未修改；依赖已退役输入的旧入口不再保证可直接运行。
其原流程如需调查，应先依据备份恢复对应材料，不自动使用当前最终资产替代。

## 导入来源与依赖审计

通过官方 MCP 读取最终目录 14 项资产，核对八段动画的实际导入记录。
另读取全项目 206 项动画、网格、音频和纹理的可用导入数据。
仅依靠 Asset Registry 的 SourceFile 标签未取得有效来源，因此补充原对象读取。
非音频 SourceAssets 引用仅有 rootLock/ImportReady 下的四份 FBX，全部保留。

AS_Back_TorsoReference 继承 Back.fbx 的来源记录，但另有 UE 局部躯干修改。
保留其 FBX 不等于可通过直接重导恢复当前最终结果。
四段斜向没有外部 FBX 来源记录，原生烘焙证据仍保存在既有 Docs 与 Saved 中。
本轮没有制作新 FBX、重导入或重新烘焙。

rootLock 的 prepare_import.py 和 import_ue.py 保持，语法检查通过。
共享 fbx_parent_motion、blender_bridge、骨架层级数据和导出设置均存在并保持。
import_ue.py 仍是含历史路径的首次导入记录，没有被包装为当前重导入口。
没有运行旧制作、自动修复或批量导入流程。

## 备份与安全删除

删除前逐文件校验输入并压缩备份，读取压缩包逐项复算 SHA256。
备份 285 项、279760519 字节，约 266.80 MiB，另外占用 Saved 空间。
源目录体积缩小不等于释放同等磁盘空间，因为本地备份仍保留。

Saved/StanceSourceCleanup_20261122_01/RetiredSourceAssets.zip

归档完整信息见 Backup.json。
所有目标先解析为绝对路径，并限定在 SourceAssets 的七个下蹲目录内。
排除当前 rootLock 制作包、Audio、全部 Markdown 与原件后，逐项再次校验哈希。
使用 PowerShell Remove-Item -LiteralPath 删除具体文件，空目录单独检查后删除。
没有跨 shell 拼接删除命令，没有触及其他项目或生产文件。

## 保护与验证

- 导入来源及 UE 保存状态清理前后完全一致。
- 所有仍被记录的当前 FBX 来源存在。
- 62 项原有保留文件哈希一致，285 项候选确已移除。
- 原始 FBX 来源清单、当前导入文件和现有制作说明均保持。
- Content、Source、Config、Scripts、音效和历史报告均保持。
- 未重新执行动画或 Gameplay 回归：本轮没有改变任何 UE 资产或代码。
- 原有上拱、脚部下沉与方向响应取舍沿用已验收最终版本。

保护结果见 Protection.json，实际来源读取见 ImportSourcesAfter.json。
当前 SourceAssets 的保留职责和退役边界写于新增根 README。
旧制作说明保留历史原文，不回填新状态；当前索引新增本轮入口。
