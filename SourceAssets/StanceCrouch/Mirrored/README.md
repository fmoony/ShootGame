# 左前与右后直接镜像

CrouchForwardLeft 来自 EightWay/CrouchForwardRight；
CrouchBackRight 来自 EightWay/CrouchBackLeft。
来源是上一轮已有资源，本轮不运行旧动作制作或优化模块。

仅反射 Body 组件 X 轴、交换左右骨骼，并按生产参考姿态处理旋转轴约定。
脚部标记随骨骼镜像；时间轴逐帧对应，不移相、不调高度、不重排轨迹。
没有 IK、接地修正、平滑或优化。全段 60 FPS、61 点、1 秒循环与来源一致。

blend 保存真实骨架的可编辑 Action；FBX 使用既有标准导出设置。
Expected.json 保存 UE cm / xyzw 的镜像目标，MirrorSummary.json 保存误差与操作声明。
mirror_stance.py 默认拒绝覆盖，重放时指定新的 --output-dir。
import_mirrored.py 只导入两个新目标，绑定生产 SK_Mannequin。
verify_mirrored.py 检查原始/压缩姿态、循环、镜像高度、标记和两处 BS 引用。

本次主方向采样中双脚表面相交为零。
鞋底穿地约 4～5 cm 从来源保留；相邻混合还有明显穿地。
这些问题没有额外处理，不能以镜像保真通过代替完整接触质量通过。
