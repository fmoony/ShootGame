# 独立右后方向派生

原始 Source、ParentBaked、LayerPlayback 保留。
本目录三段从 LayerPlayback/CrouchBack 在 Blender 4.5.8 中烘焙：
112.5°、135°、157.5°，整体偏航分别为 -67.5°、-45°、-22.5°。
它们是制作过的派生版本，不是原始 Source。
全部骨骼高度、非 root 局部曲线、31 点、30 FPS 和 1 秒周期保持来源。

blend 为可继续编辑的 Action；FBX 使用 RifleTools 的已验收导出选项。
Expected.json 保存 UE 坐标的组件空间目标；BakeSummary.json 保存烘焙误差。
bake_back_right.py 拒绝覆盖目标，可用 --output-dir 指定新目录重放。
标准 FBX 导入直接使用生产 SK_Mannequin，不修改 Skeleton 参考姿态或重定向模式。

实验用原生 BlendSpace、AO 和上身分层求值，不增加 C++ 骨骼处理。
当前九样本解决目标八向的右后穿地，约 101.25° 的过渡区仍有严重接触问题。
鞋底平贴、脚滑和自然度应继续人工验收，不能仅以骨骼点高度代替。
