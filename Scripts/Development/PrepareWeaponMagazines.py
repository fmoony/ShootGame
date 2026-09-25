"""在 UE 5.6 Python commandlet 中准备或验证四把枪的可拆卸弹匣资产。

默认只在进程内预演；-MagazineMode=apply 才保存，verify 只读验证已保存资产。
不修改数据表、角色动画或运行时逻辑。完整用法见枪械可拆卸弹匣资产约定。
"""

import collections
import hashlib
import json
from pathlib import Path
import re
import traceback

import unreal


ROOT = Path(unreal.Paths.project_dir())
OUTPUT = ROOT / "Saved/WeaponMagazines"
QUERY = unreal.GeometryScript_MeshQueries
ASSETS = unreal.GeometryScript_AssetUtils
WEIGHTS = unreal.GeometryScript_BoneWeights
EDITOR = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
WEAPONS = {
    "Rifle": ("/Game/Weapons/Rifle/Meshes/SKM_Rifle", "Magazine"),
    "Pistol": ("/Game/Weapons/Pistol/Meshes/SKM_Pistol", "Magazine"),
    "AWP": ("/Game/Weapons/AWP/Meshes/SKM_AWP_Sniper_Rifle", "Magazine"),
    "GrenadeLauncher": ("/Game/Weapons/GrenadeLauncher/Meshes/SKM_GrenadeLauncher", "Clip_Bone"),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def vector(value):
    return [value.x, value.y, value.z]


def transform(value):
    return {
        "translation": vector(value.translation),
        "rotation": [value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w],
        "scale": vector(value.scale3d),
    }


def read_mesh(mesh, lod=0, render=False):
    lod_type = unreal.GeometryScriptLODType.RENDER_DATA if render else unreal.GeometryScriptLODType.SOURCE_MODEL
    dynamic, outcome = ASSETS.copy_mesh_from_skeletal_mesh(
        mesh, unreal.DynamicMesh(), unreal.GeometryScriptCopyMeshFromAssetOptions(),
        unreal.GeometryScriptMeshReadLOD(lod_type=lod_type, lod_index=lod))
    require(outcome == unreal.GeometryScriptOutcomePins.SUCCESS, "无法读取模型 " + mesh.get_path_name())
    return dynamic


def geometry(dynamic):
    positions = {}
    triangles = {}
    for index in range(QUERY.get_num_vertex_i_ds(dynamic)):
        position, valid = QUERY.get_vertex_position(dynamic, index)
        if valid:
            positions[index] = vector(position)
    for index in range(QUERY.get_num_triangle_i_ds(dynamic)):
        triangle, valid = QUERY.get_triangle_indices(dynamic, index)
        if valid:
            triangles[index] = [triangle.x, triangle.y, triangle.z]
    return positions, triangles


def surface_keys(dynamic):
    # 按三角面角点位置、UV 和材质比较；不依赖拆点前后的顶点序号。
    positions, triangles = geometry(dynamic)
    _, material_ids, valid_materials = unreal.GeometryScript_Materials.get_all_triangle_material_i_ds(dynamic)
    require(valid_materials, "模型缺少材质 ID")
    materials = unreal.GeometryScript_List.convert_index_list_to_array(material_ids)
    keys = collections.Counter()
    for index, indices in triangles.items():
        uv1, uv2, uv3, valid = QUERY.get_triangle_u_vs(dynamic, 0, index)
        require(valid, "三角面缺少 UV0")
        points = sorted(tuple(round(c, 4) for c in positions[v]) +
                        (round(uv.x, 5), round(uv.y, 5))
                        for v, uv in zip(indices, (uv1, uv2, uv3)))
        keys[(tuple(points), materials[index])] += 1
    return keys


def skeleton_snapshot(mesh):
    modifier = unreal.SkeletonModifier()
    require(modifier.set_skeletal_mesh(mesh), "无法读取参考骨架")
    bones = {str(b): transform(modifier.get_bone_transform(b, True)) for b in modifier.get_all_bone_names()}
    sockets = {}
    for index in range(mesh.num_sockets()):
        socket = mesh.get_socket_by_index(index)
        sockets[str(socket.socket_name)] = {
            "bone": str(socket.get_editor_property("bone_name")),
            "local": transform(socket.get_socket_local_transform()),
        }
    return {"bones": bones, "sockets": sockets,
            "materials": [str(m.material_interface.get_path_name()) for m in mesh.materials],
            "lod_count": EDITOR.get_lod_count(mesh)}


def weighted_selection(dynamic, bone):
    _, valid, bone_index = WEIGHTS.get_bone_index(dynamic, bone)
    require(valid, "模型缺少骨骼 " + bone)
    _, triangles = geometry(dynamic)
    selected = set()
    mixed = []
    # 原始模型可能含未被任何三角面使用的孤立点；它们不参与实际蒙皮显示。
    for index in {v for t in triangles.values() for v in t}:
        _, weights, valid = WEIGHTS.get_vertex_bone_weights(dynamic, index)
        require(valid, "可见顶点缺少权重: " + str(index))
        amount = sum(w.weight for w in weights if w.bone_index == bone_index)
        if amount > 0.0001:
            require(abs(amount - 1.0) < 0.0001, "弹匣包含非刚性混合权重")
            selected.add(index)
    selected_triangles = []
    for index, triangle in triangles.items():
        count = sum(v in selected for v in triangle)
        if count == 3:
            selected_triangles.append(index)
        elif count:
            mixed.append(index)
    require(not mixed, "弹匣和枪身仍通过三角面相连，移动会拉伸")
    require(selected_triangles, "没有真实弹匣三角面")
    return selected_triangles, selected


def original_selection(name, dynamic):
    positions, triangles = geometry(dynamic)
    if name in ("Pistol", "GrenadeLauncher"):
        return weighted_selection(dynamic, WEAPONS[name][1])[0]
    if name == "Rifle":
        # 以拓扑连通块整体选择已审计的弹匣主体、顶盖、锁扣和底部装饰。
        parents = {i: i for i in positions}

        def find(index):
            while parents[index] != index:
                parents[index] = parents[parents[index]]
                index = parents[index]
            return index

        for a, b, c in triangles.values():
            parents[find(b)] = find(a)
            parents[find(c)] = find(a)
        groups = collections.defaultdict(list)
        for index in {v for t in triangles.values() for v in t}:
            groups[find(index)].append(index)
        selected = set()
        for group in groups.values():
            if all(9 < positions[v][1] < 19 and positions[v][2] < 1.5 for v in group):
                selected.update(group)
        require(len(selected) == 685, "Rifle 几何基线变化，请重新审计")
    else:
        # AWP 沿原模型已存在的弹匣上沿分离；不在三角面内部切割或改变外形。
        selected = {i for i, (_, y, z) in positions.items() if 10.3 <= y <= 19 and z <= 1.517}
    result = [i for i, t in triangles.items() if set(t) <= selected]
    expected = 1310 if name == "Rifle" else 180
    require(len(result) == expected, name + " 三角面基线变化，请重新审计")
    return result


def selection(dynamic, triangles):
    _, value = unreal.GeometryScript_MeshSelection.convert_index_array_to_mesh_selection(
        dynamic, triangles, unreal.GeometryScriptMeshSelectionType.TRIANGLES)
    return value


def extract(dynamic, triangles):
    result = unreal.DynamicMesh()
    unreal.GeometryScript_MeshDecomposition.copy_mesh_selection_to_mesh(
        dynamic, result, selection(dynamic, triangles))
    return result


def magazine_path(name):
    return "/Game/Weapons/" + name + "/Meshes/SM_" + name + "_Magazine"


def add_bone(mesh, name):
    modifier = unreal.SkeletonModifier()
    require(modifier.set_skeletal_mesh(mesh), "无法读取骨架")
    names = modifier.get_all_bone_names()
    require("Magazine" not in [str(b) for b in names], "已存在 Magazine，拒绝重复处理")
    position = (0.0, 14.3, 1.4) if name == "Rifle" else (1.38, 14.75, 1.245)
    target = unreal.Transform(location=unreal.Vector(*position))
    local = target.make_relative(modifier.get_bone_transform(names[0], True))
    require(modifier.add_bone("Magazine", names[0], local), "添加弹匣骨骼失败")
    require(modifier.commit_skeleton_to_skeletal_mesh(), "提交弹匣骨骼失败")


def prepare_mesh(name, mesh):
    before = skeleton_snapshot(mesh)
    dynamic = read_mesh(mesh)
    before_surface = surface_keys(dynamic)
    selected = original_selection(name, dynamic)
    bone = WEAPONS[name][1]
    if name in ("Rifle", "AWP"):
        add_bone(mesh, name)
        dynamic = read_mesh(mesh)
        if name == "Rifle":
            positions, triangles = geometry(dynamic)
            skin = unreal.SkinWeightModifier()
            require(skin.set_skeletal_mesh(mesh), "读取 Rifle 权重失败")
            require(len(positions) == skin.get_num_vertices(), "源顶点与权重 ID 不一致")
            for index in {v for t in selected for v in triangles[t]}:
                require(skin.set_vertex_weights(index, {bone: 1.0}, True), "设置弹匣权重失败")
            require(skin.commit_weights_to_skeletal_mesh(), "保存弹匣权重失败")
        else:
            # 保留三角面与 UV，只复制边界顶点，消除弹匣与机匣的共同蒙皮顶点。
            unreal.GeometryScript_MeshModeling.apply_mesh_disconnect_faces(dynamic, selection(dynamic, selected))
            _, triangles = geometry(dynamic)
            _, valid, bone_index = WEIGHTS.get_bone_index(dynamic, bone)
            require(valid, "动态模型未包含新骨骼")
            weights = [unreal.GeometryScriptBoneWeight(bone_index=bone_index, weight=1.0)]
            for index in {v for t in selected for v in triangles[t]}:
                _, valid = WEIGHTS.set_vertex_bone_weights(dynamic, index, weights)
                require(valid, "写入 AWP 弹匣权重失败")
            # AWP 只有 LOD0；保留原法线和切线，不把断开的边界按旧 ID 重新焊回。
            settings = EDITOR.get_lod_build_settings(mesh, 0)
            _, outcome = ASSETS.copy_mesh_to_skeletal_mesh(
                dynamic, mesh, unreal.GeometryScriptCopyMeshToAssetOptions(
                    use_original_vertex_order=False,
                    bone_hierarchy_mismatch_handling=(
                        unreal.GeometryScriptBoneHierarchyMismatchHandling.REMAP_GEOMETRY_TO_REFERENCE_SKELETON)),
                unreal.GeometryScriptMeshWriteLOD(lod_index=0))
            require(outcome == unreal.GeometryScriptOutcomePins.SUCCESS, "提交 AWP 分离网格失败")
            EDITOR.set_lod_build_settings(mesh, 0, settings)
        dynamic = read_mesh(mesh)
    require(surface_keys(dynamic) == before_surface, name + " 原三角面外形或材质发生变化")
    selected, _ = weighted_selection(dynamic, bone)
    modifier = unreal.SkeletonModifier()
    require(modifier.set_skeletal_mesh(mesh), "读取最终骨架失败")
    bone_transform = modifier.get_bone_transform(bone, True)

    require(mesh.find_socket("MagazineSocket") is None, "已有 MagazineSocket，拒绝覆盖")
    socket = unreal.SkeletalMeshSocket(outer=mesh)
    socket.set_socket_parent(mesh, bone)
    mesh.add_socket(socket, False)
    require(mesh.rename_socket(socket.socket_name, "MagazineSocket"), "重命名弹匣挂点失败")
    socket.set_socket_local_transform(unreal.Transform())

    part = extract(dynamic, selected)
    # 输出道具保留全部原三角面；仅 AWP 补齐切开后暴露的弹匣顶面。
    cap_count = 0
    if name == "AWP":
        unreal.GeometryScript_MeshRepair.weld_mesh_edges(
            part, unreal.GeometryScriptWeldEdgesOptions(tolerance=0.0001))
        _, cap_count, failures = unreal.GeometryScript_MeshRepair.fill_all_mesh_holes(
            part, unreal.GeometryScriptFillHolesOptions(delete_isolated_triangles=False))
        require(failures == 0 and QUERY.get_is_closed_mesh(part), "AWP 弹匣封口失败")
    unreal.GeometryScript_MeshTransforms.inverse_transform_mesh(part, bone_transform)
    path = magazine_path(name)
    require(not unreal.EditorAssetLibrary.does_asset_exist(path), "独立弹匣资产已存在，拒绝覆盖")
    static, outcome = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(
        part, path, unreal.GeometryScriptCreateNewStaticMeshAssetOptions(
            enable_collision=False, enable_nanite=False,
            enable_recompute_normals=False, enable_recompute_tangents=False))
    require(outcome == unreal.GeometryScriptOutcomePins.SUCCESS, "创建独立弹匣失败")
    materials = [unreal.StaticMaterial(
        material_interface=m.material_interface, material_slot_name=m.material_slot_name)
        for m in mesh.materials]
    static.set_editor_property("static_materials", materials)
    after = skeleton_snapshot(mesh)
    for key in ("materials", "lod_count"):
        require(after[key] == before[key], name + " 改变了 " + key)
    for key in ("bones", "sockets"):
        require(all(after[key].get(k) == v for k, v in before[key].items()), name + " 改变了原 " + key)
    return {"name": name, "before": before, "after": after,
            "source_surface_hash": hashlib.sha256(str(sorted(before_surface.items())).encode()).hexdigest(),
            "magazine": path, "capped_holes": cap_count}, static


def verify_weapon(name, mesh):
    bone = WEAPONS[name][1]
    socket = mesh.find_socket("MagazineSocket")
    require(socket is not None and str(socket.get_editor_property("bone_name")) == bone, "弹匣挂点无效")
    require(transform(socket.get_socket_local_transform()) == transform(unreal.Transform()), "挂点未对齐骨骼")
    lods = []
    for lod in range(EDITOR.get_lod_count(mesh)):
        dynamic = read_mesh(mesh, lod, True)
        triangles, vertices = weighted_selection(dynamic, bone)
        positions, all_triangles = geometry(dynamic)
        used = {v for t in all_triangles.values() for v in t}
        body = used - vertices
        require(body, "缺少枪身几何")
        # 实际 RenderData 的每个顶点均为 Magazine 的 0 或 1 权重；独立平移不会改变枪身。
        lods.append({"lod": lod, "magazine_triangles": len(triangles),
                     "magazine_vertices": len(vertices & used), "body_vertices": len(body),
                     "mixed_boundary_triangles": 0})
    static = unreal.load_asset(magazine_path(name))
    require(static is not None, "独立弹匣未加载")
    require([m.material_interface for m in static.static_materials] ==
            [m.material_interface for m in mesh.materials], "独立弹匣材质未继承")
    source = read_mesh(mesh)
    source_triangles, _ = weighted_selection(source, bone)
    source_part = extract(source, source_triangles)
    source_positions, _ = geometry(source_part)
    modifier = unreal.SkeletonModifier()
    require(modifier.set_skeletal_mesh(mesh), "无法读取弹匣参考变换")
    pose = modifier.get_bone_transform(bone, True)
    static_dynamic, outcome = ASSETS.copy_mesh_from_static_mesh(
        static, unreal.DynamicMesh(), unreal.GeometryScriptCopyMeshFromAssetOptions(),
        unreal.GeometryScriptMeshReadLOD(lod_type=unreal.GeometryScriptLODType.SOURCE_MODEL))
    require(outcome == unreal.GeometryScriptOutcomePins.SUCCESS, "无法读回独立弹匣几何")
    unreal.GeometryScript_MeshTransforms.transform_mesh(static_dynamic, pose)
    assembled, _ = geometry(static_dynamic)
    # 用保存后的独立网格装回骨骼参考帧，核验原弹匣每个点的位置，允许 AWP 新增封口点。
    maximum_error = 0.0
    for point in source_positions.values():
        distance_squared = min(sum((a - b) ** 2 for a, b in zip(point, other))
                               for other in assembled.values())
        maximum_error = max(maximum_error, distance_squared ** 0.5)
    require(maximum_error < 0.001, "独立弹匣装回后不能与原位置重合")
    # 预览数据来自 UE 实际 RenderData，可用于检查移出、隐藏和装回时的几何范围。
    render_mesh = read_mesh(mesh, 0, True)
    render_positions, render_triangles = geometry(render_mesh)
    render_magazine_triangles, render_vertices = weighted_selection(render_mesh, bone)
    evidence = {"name": name, "positions": render_positions, "triangles": render_triangles,
                "magazine_vertices": sorted(render_vertices), "magazine_triangles": render_magazine_triangles,
                "assembled_prop_positions": assembled, "socket_pose": transform(pose)}
    (OUTPUT / (name + "_geometry.json")).write_text(json.dumps(evidence), encoding="utf-8")
    return {"name": name, "mesh": mesh.get_path_name(), "bone": bone,
            "socket": "MagazineSocket", "magazine": static.get_path_name(), "lods": lods,
            "assembly_maximum_error_cm": maximum_error}


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    command_line = unreal.SystemLibrary.get_command_line()
    # UE 5.6 RenderData 转换器在资源初始化并发完成时可能越界；离线校验禁用渲染线程。
    require(re.search(r"-(?:onethread|norenderthread)\b", command_line, re.IGNORECASE),
            "请用独立 commandlet 并传入 -norenderthread -norhithread")
    require(not re.search(r"-nullrhi\b", command_line, re.IGNORECASE),
            "NullRHI 无法提供有效 RenderData 蒙皮权重，请使用真实 RHI")
    match = re.search(r"-MagazineMode=(preview|apply|verify)\b", command_line)
    mode = match.group(1) if match else "preview"
    report = {"mode": mode, "status": "Running", "weapons": [], "saved": []}
    report_path = OUTPUT / (mode + ".json")
    pending = []
    baseline_path = OUTPUT / "apply.json"
    baseline = {}
    if mode == "verify" and baseline_path.exists():
        applied = json.loads(baseline_path.read_text(encoding="utf-8"))
        require(applied["status"] == "Passed", "上一次保存未全部完成，请先检查保存列表")
        baseline = {w["name"]: w for w in applied["weapons"]}
    try:
        for name, (path, _) in WEAPONS.items():
            mesh = unreal.load_asset(path)
            require(mesh is not None, "模型未加载 " + path)
            if mode == "verify":
                record = verify_weapon(name, mesh)
                if name in baseline:
                    require(skeleton_snapshot(mesh) == baseline[name]["after"], "重载后骨骼或挂点不一致")
                    digest = hashlib.sha256(str(sorted(surface_keys(read_mesh(mesh)).items())).encode()).hexdigest()
                    require(digest == baseline[name]["source_surface_hash"], "重载后外形、UV 或材质不一致")
                record["compared_with_apply_baseline"] = name in baseline
            else:
                record, static = prepare_mesh(name, mesh)
                record["verification"] = verify_weapon(name, mesh)
                pending.extend([mesh, static])
                if name in ("Rifle", "AWP"):
                    pending.append(mesh.get_editor_property("skeleton"))
            report["weapons"].append(record)
            unreal.log("MAGAZINE_PREPARED " + name)
        # 四把枪全部通过验证后才开始逐包保存；失败报告保留已经保存的精确列表。
        if mode == "apply":
            for asset in pending:
                require(unreal.EditorAssetLibrary.save_loaded_asset(asset, False), "保存失败 " + asset.get_path_name())
                report["saved"].append(asset.get_path_name())
                report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        report["status"] = "Passed"
    except Exception:
        report["status"] = "Failed"
        report["error"] = traceback.format_exc()
        raise
    finally:
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("MAGAZINE_" + mode.upper() + "_PASSED")


main()
