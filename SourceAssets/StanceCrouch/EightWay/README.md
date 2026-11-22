# 八向移动制作输入

本目录包含左前、右前、左后、右后四斜向，及原先缺失独立资源的正前。
它们是明确处理过的派生资源：来源融合、离线 IK、相邻原生混合约束和周期平滑。
没有新采集动作，也不是简单旋转同一个后退动作。
原始 Source、既有轴向和所有历史来源保持不变。

全部为 60 FPS、61 点、1 秒循环；每段保存可编辑 Blender Action 和标准 FBX。
Expected.json 保存 UE cm、xyzw 四元数的组件空间逐帧目标。
AuthorSummary.json、BakeSummary.json、Quality.json 保存制作参数和核对结果。
导入使用标准 FbxFactory，直接绑定生产 SK_Mannequin，保留 0.01 骨架对象尺度。

工具入口位于 Scripts/Animation/Stance：

- author_eight_way.py：制作正前和四斜向，拒绝覆盖；用 --output-dir 指定 Saved 新目录。
- stance_author_math.py：离线双腿求解和每侧 16 权重约束，固定脚部组件朝向。
- 周期求解四轮，处理前后邻帧和循环接缝；所有计算均烘焙进资源。
- bake_eight_way.py：指定 --prepared-dir 与新 --output-dir，写入真实 bpy 骨架并核对。
- import_eight_way.py：默认只导入五个新目标；EightWayReimportOwn 仅允许本目录已验证目标。
- verify_eight_way.py：读取原始/压缩姿态、循环、布点、骨架和保护文件。
- analyze_authored_motion.py：检查 60 FPS 腿骨逐帧旋转，不替代自然度验收。
- analyze_eight_way.py：检查实际运行的八向或加密方向接触、AO 与 FP 复制。

制作依赖 SciPy 1.16.3；本机装在 Saved/Tools/StanceOptimization，未进入运行时。
后台 bpy 4.5.8 位于 Saved/Tools/BlenderPython458；依赖不作为长期资源输入。
重放先在新 Saved 目录制作与烘焙，再审查并通过 UE 资产系统导入。
不覆盖原始 Source，不在 C++ 添加接地、混合或骨骼回写。

最终 BS_SourceLayer_Lower 为中心 idle 加八个标准点。
旧右后 112.5°、157.5° 点退出，135° 只引用一段新右后。
原 idle 仍是单膝跪姿；脚滑、鞋底逐顶点接触、坡面与网络尚未验收。
