"""仅补 Pistol/AWP 弹匣井内壁；默认 preview，apply 保存，verify 独立重载核验。"""

import collections
import hashlib
import json
import math
import shutil
from datetime import datetime
from pathlib import Path
import unreal as u

ROOT = Path(u.Paths.project_dir())
OUT = ROOT / 'Saved/MagazineWells'
Q = u.GeometryScript_MeshQueries
EDIT = u.GeometryScript_MeshEdits
ASSET = u.GeometryScript_AssetUtils
SKIN = u.GeometryScript_BoneWeights
EDITOR = u.get_editor_subsystem(u.SkeletalMeshEditorSubsystem)
TARGETS = {
    'Pistol': ('/Game/Weapons/Pistol/Meshes/SKM_Pistol', 5590, 10,
               (-1.0, 1.0, -3.4, -.1, -5.7, -4.6), (0.0, .35837, .93358), 6.8),
    'AWP': ('/Game/Weapons/AWP/Meshes/SKM_AWP_Sniper_Rifle', 12397, 18,
            (.2, 2.6, 10.5, 19.0, .8, 1.7), (0.0, 0.0, 1.0), 1.2),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def vector(v): return [float(v.x), float(v.y), float(v.z)]
def sub(a, b): return [x - y for x, y in zip(a, b)]
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
def key(p): return tuple(round(v, 4) for v in p)
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def read(mesh):
    dynamic, result = ASSET.copy_mesh_from_skeletal_mesh(
        mesh, u.DynamicMesh(), u.GeometryScriptCopyMeshFromAssetOptions(),
        u.GeometryScriptMeshReadLOD(lod_type=u.GeometryScriptLODType.SOURCE_MODEL, lod_index=0))
    require(result == u.GeometryScriptOutcomePins.SUCCESS, 'Source LOD 读取失败')
    return dynamic


def snapshot(mesh, dynamic):
    positions, triangles = [], []
    for i in range(Q.get_num_vertex_i_ds(dynamic)):
        p, valid = Q.get_vertex_position(dynamic, i)
        require(valid, '顶点 ID 不连续')
        positions.append(vector(p))
    for i in range(Q.get_num_triangle_i_ds(dynamic)):
        t, valid = Q.get_triangle_indices(dynamic, i)
        require(valid, '三角形 ID 不连续')
        triangles.append([t.x, t.y, t.z])
    skin = u.SkinWeightModifier()
    require(skin.set_skeletal_mesh(mesh), '读取源权重失败')
    require(skin.get_num_vertices() == len(positions), 'Source 顶点/权重 ID 不一致')
    weights = [{str(b): float(w) for b, w in skin.get_vertex_weights(i).items()} for i in range(len(positions))]
    _, ids, valid = u.GeometryScript_Materials.get_all_triangle_material_i_ds(dynamic)
    require(valid, '材质 ID 缺失')
    mids = list(u.GeometryScript_List.convert_index_list_to_array(ids))
    surfaces = []
    for i, tri in enumerate(triangles):
        a, b, c, valid = Q.get_triangle_u_vs(dynamic, 0, i)
        require(valid, 'UV0 缺失')
        normals = Q.get_triangle_normals(dynamic, i)
        if isinstance(normals, tuple):
            ns = list(normals)
            if ns and isinstance(ns[0], u.DynamicMesh):
                ns = ns[1:]
            if ns and isinstance(ns[-1], bool):
                require(ns[-1], '原表面法线缺失')
                ns = ns[:-1]
            if len(ns) == 1:
                ns = [ns[0].vector0, ns[0].vector1, ns[0].vector2]
        else:
            ns = [normals.vector0, normals.vector1, normals.vector2]
        require(len(ns) == 3, '法线返回值异常')
        corners = []
        for v, uv, normal in zip(tri, (a, b, c), ns):
            corners.append([key(positions[v]), [round(uv.x, 5), round(uv.y, 5)],
                            [round(x, 4) for x in vector(normal)], sorted(weights[v].items())])
        surfaces.append(json.dumps([sorted(corners), mids[i]]))
    sockets = {}
    for i in range(mesh.num_sockets()):
        s = mesh.get_socket_by_index(i)
        tr = s.get_socket_local_transform()
        sockets[str(s.socket_name)] = [str(s.bone_name), vector(tr.translation),
                                      [tr.rotation.x, tr.rotation.y, tr.rotation.z, tr.rotation.w], vector(tr.scale3d)]
    skeleton = u.SkeletonModifier()
    require(skeleton.set_skeletal_mesh(mesh), '读取骨架失败')
    bones = {}
    for b in skeleton.get_all_bone_names():
        tr = skeleton.get_bone_transform(b, True)
        bones[str(b)] = [vector(tr.translation),
                        [tr.rotation.x, tr.rotation.y, tr.rotation.z, tr.rotation.w], vector(tr.scale3d)]
    magazine = {i for i, w in enumerate(weights) if w.get('Magazine', 0.0) > .999}
    return {'positions': positions, 'triangles': triangles, 'weights': weights, 'material_ids': mids,
            'surfaces': surfaces, 'sockets': sockets, 'bones': bones,
            'materials': [[str(m.material_interface.get_path_name()), str(m.material_slot_name)] for m in mesh.materials],
            'lod_count': EDITOR.get_lod_count(mesh), 'vertices': len(positions), 'triangle_count': len(triangles),
            'magazine_vertices': len(magazine),
            'magazine_triangles': sum(all(v in magazine for v in tri) for tri in triangles),
            'mixed_boundary_triangles': sum(any(v in magazine for v in tri) and
                                            not all(v in magazine for v in tri) for tri in triangles)}


def mouth_edges(data, region, expected):
    points = data['positions']
    edges = collections.defaultdict(list)
    for i, tri in enumerate(data['triangles']):
        if any(data['weights'][v].get('Magazine', 0) > .0001 for v in tri):
            continue
        for j in range(3):
            a, b = tri[j], tri[(j+1) % 3]
            edges[tuple(sorted((key(points[a]), key(points[b]))))].append((i, a, b))
    def inside(p): return all(region[k*2] < p[k] < region[k*2+1] for k in range(3))
    result = [items[0] for items in edges.values() if len(items) == 1 and
              all(inside(points[v]) for v in items[0][1:])]
    require(len(result) == expected, '弹匣井开口基线变化，禁止重复补面')
    degree = collections.Counter(key(points[v]) for _, a, b in result for v in (a, b))
    require(all(n == 2 for n in degree.values()), '开口不是简单闭合轮廓')
    return result


def add_face(dynamic, points, indices, uvs, mid):
    n = cross(sub(points[indices[1]], points[indices[0]]), sub(points[indices[2]], points[indices[0]]))
    length = math.sqrt(dot(n, n))
    require(length > 1e-7, '新增三角形退化')
    # 源网格采用 UE 顺时针正面；角点法线与右手叉积方向相反。
    normal = u.Vector(*(-x / length for x in n))
    _, tid = EDIT.add_triangle_to_mesh(dynamic, u.IntVector(*indices), 0, True)
    require(tid >= 0, '新增三角形失败')
    _, valid = u.GeometryScript_UVs.set_mesh_triangle_u_vs(
        dynamic, 0, tid, u.GeometryScriptUVTriangle(uv0=uvs[0], uv1=uvs[1], uv2=uvs[2]), True)
    require(valid, '新增内壁 UV 失败')
    _, valid = u.GeometryScript_Normals.set_mesh_triangle_normals(
        dynamic, tid, u.GeometryScriptTriangle(vector0=normal, vector1=normal, vector2=normal), True)
    require(valid, '新增内壁法线失败')
    u.GeometryScript_Materials.set_triangle_material_id(dynamic, tid, mid, True)
    return n


def build(dynamic, before, edges, axis, depth):
    points = dict(enumerate(before['positions']))
    upper = {}
    mouth = {}
    bone_name = next(iter(before['weights'][edges[0][1]]))
    require(bone_name != 'Magazine', '内壁不能绑定弹匣骨骼')
    _, valid, bone_id = SKIN.get_bone_index(dynamic, bone_name)
    require(valid, '固定枪体骨骼不存在')
    weights = [u.GeometryScriptBoneWeight(bone_index=bone_id, weight=1.0)]
    def add_vertex(p):
        _, i = EDIT.add_vertex_to_mesh(dynamic, u.Vector(*p), True)
        require(i >= 0, '新增内壁顶点失败')
        points[i] = p
        _, valid = SKIN.set_vertex_bone_weights(dynamic, i, weights)
        require(valid, '新增固定枪体权重失败')
        return i
    for _, a, b in edges:
        for v in (a, b):
            if key(points[v]) not in upper:
                mouth[key(points[v])] = add_vertex(list(points[v]))
                upper[key(points[v])] = add_vertex([x + y * depth for x, y in zip(points[v], axis)])
    center = add_vertex([sum(points[v][k] for v in upper.values()) / len(upper) for k in range(3)])
    # 保留原开口边缘与外侧法线；只向枪体内部延伸侧壁并封闭顶部。
    for tid, a, b in edges:
        aa, bb = upper[key(points[a])], upper[key(points[b])]
        tri = before['triangles'][tid]
        uv0, uv1, uv2, valid = Q.get_triangle_u_vs(dynamic, 0, tid)
        require(valid, '原表面 UV 读取失败')
        uv_a, uv_b = [uv0, uv1, uv2][tri.index(a)], [uv0, uv1, uv2][tri.index(b)]
        ua, ub = u.Vector2D(uv_a.x, uv_a.y + .04), u.Vector2D(uv_b.x, uv_b.y + .04)
        mid = before['material_ids'][tid]
        a, b = mouth[key(points[a])], mouth[key(points[b])]
        cap_normal = cross(sub(points[aa], points[bb]), sub(points[center], points[bb]))
        if dot(cap_normal, axis) < 0:
            a, b, aa, bb, uv_a, uv_b, ua, ub = b, a, bb, aa, uv_b, uv_a, ub, ua
        add_face(dynamic, points, [b, a, aa], [uv_b, uv_a, ua], mid)
        add_face(dynamic, points, [b, aa, bb], [uv_b, ua, ub], mid)
        n = add_face(dynamic, points, [bb, aa, center], [ub, ua, u.Vector2D((ua.x+ub.x)/2, (ua.y+ub.y)/2)], mid)
        require(dot(n, axis) > 0, '内顶面正面未朝向弹匣井入口')
    return len(upper) + len(mouth) + 1


def validate(before, after, added_vertices, added_triangles):
    original = collections.Counter(before['surfaces'])
    require(not (original - collections.Counter(after['surfaces'])),
            '原表面位置、UV、法线、材质或蒙皮被改变')
    for field in ('bones', 'sockets', 'materials', 'lod_count', 'magazine_vertices', 'magazine_triangles'):
        require(before[field] == after[field], '保护字段发生变化: ' + field)
    require(after['mixed_boundary_triangles'] == 0, '产生 Magazine/Body 混合三角形')
    require(after['vertices'] == before['vertices'] + added_vertices, '顶点增量异常')
    require(after['triangle_count'] == before['triangle_count'] + added_triangles, '三角形增量异常')
    # 内衬保持入口开放，但侧壁和顶面之间不得存在裂缝、非流形边或反向法线。
    patch_edges = collections.defaultdict(list)
    for i, surface in enumerate(after['surfaces']):
        if original[surface] > 0:
            original[surface] -= 1
            continue
        tri = after['triangles'][i]
        for a, b in zip(tri, tri[1:] + tri[:1]):
            patch_edges[tuple(sorted((a, b)))].append((a, b))
        p = [after['positions'][v] for v in tri]
        n = cross(sub(p[1], p[0]), sub(p[2], p[0]))
        require(all(dot(n, corner[2]) < 0 for corner in json.loads(surface)[0]), '内衬法线不匹配 UE 正面')
    require(all(len(items) == 1 or (len(items) == 2 and items[0] == items[1][::-1])
                for items in patch_edges.values()), '内衬包含非流形边或绕序冲突')
    require(sum(len(items) == 1 for items in patch_edges.values()) == added_triangles // 3,
            '内衬除入口外仍有未封闭边界')


def main():
    mode = u.SystemLibrary.get_command_line().split('-MagazineWellMode=')
    mode = mode[-1].split()[0] if len(mode) > 1 else 'preview'
    require(mode in ('preview', 'apply', 'verify'), '无效执行模式')
    OUT.mkdir(parents=True, exist_ok=True)
    protected = [ROOT / ('Content/Weapons/' + n + '/Meshes/SM_' + n + '_Magazine.uasset') for n in ('Pistol', 'AWP')]
    protected += [ROOT / 'Content/Weapons/Rifle/Meshes/SKM_Rifle.uasset',
                  ROOT / 'Content/Weapons/GrenadeLauncher/Meshes/SKM_GrenadeLauncher.uasset',
                  ROOT / 'Content/Shooter/Data/DT_WeaponData.uasset']
    hashes = {str(p): digest(p) for p in protected}
    report = {'mode': mode, 'status': 'Running', 'weapons': {}, 'saved': []}
    pending = []
    for name, (path, triangle_count, count, region, axis, depth) in TARGETS.items():
        mesh = u.load_asset(path)
        require(mesh is not None and EDITOR.get_lod_count(mesh) == 1, '网格缺失或 LOD 基线变化')
        dynamic = read(mesh)
        if mode == 'verify':
            baseline = json.loads((OUT / (name + '_baseline.json')).read_text())
            after = snapshot(mesh, dynamic)
            validate(baseline, after, count * 2 + 1, count * 3)
        else:
            baseline = snapshot(mesh, dynamic)
            require(baseline['triangle_count'] == triangle_count, '基线已变化，禁止重复应用')
            edges = mouth_edges(baseline, region, count)
            added = build(dynamic, baseline, edges, axis, depth)
            settings = EDITOR.get_lod_build_settings(mesh, 0)
            u.GeometryScript_Normals.compute_tangents(dynamic, u.GeometryScriptTangentsOptions())
            _, outcome = ASSET.copy_mesh_to_skeletal_mesh(
                dynamic, mesh, u.GeometryScriptCopyMeshToAssetOptions(
                    use_original_vertex_order=True, enable_recompute_normals=False, enable_recompute_tangents=False,
                    bone_hierarchy_mismatch_handling=
                    u.GeometryScriptBoneHierarchyMismatchHandling.REMAP_GEOMETRY_TO_REFERENCE_SKELETON),
                u.GeometryScriptMeshWriteLOD(lod_index=0))
            require(outcome == u.GeometryScriptOutcomePins.SUCCESS, '写回 Source LOD 失败')
            EDITOR.set_lod_build_settings(mesh, 0, settings)
            after = snapshot(mesh, read(mesh))
            validate(baseline, after, added, count * 3)
            if mode == 'apply':
                (OUT / (name + '_baseline.json')).write_text(json.dumps(baseline), encoding='utf-8')
                pending.append((path, mesh))
        (OUT / (name + '_' + mode + '.json')).write_text(json.dumps(after), encoding='utf-8')
        report['weapons'][name] = {'before_vertices': baseline['vertices'], 'after_vertices': after['vertices'],
                                 'before_triangles': baseline['triangle_count'], 'after_triangles': after['triangle_count'],
                                 'magazine_vertices': after['magazine_vertices'],
                                 'magazine_triangles': after['magazine_triangles'],
                                 'mixed_boundary_triangles': after['mixed_boundary_triangles'],
                                 'walls': count * 2, 'roof': count, 'depth_cm': depth}
    require(all(digest(Path(p)) == h for p, h in hashes.items()), '受保护资产被修改')
    if mode == 'apply':
        backup = OUT / ('Before_' + datetime.now().strftime('%Y%m%d_%H%M%S'))
        for path, mesh in pending:
            relative = Path('Content') / (path.removeprefix('/Game/') + '.uasset')
            destination = backup / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, destination)
        report['backup'] = str(backup)
        for path, mesh in pending:
            require(u.EditorAssetLibrary.save_loaded_asset(mesh, False), '保存失败: ' + path)
            report['saved'].append(path)
            (OUT / (mode + '_report.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
    report['protected_asset_hashes_unchanged'] = True
    report['status'] = 'Passed'
    (OUT / (mode + '_report.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
    u.log('MAGAZINE_WELL_SUCCESS ' + mode)


main()
