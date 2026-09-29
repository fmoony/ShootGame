"""沿用 Rifle 基线，为其余三枪配置弹匣换手表现；默认 preview 不保存资产。"""

import copy
import json
import shutil
from datetime import datetime
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.project_dir())
OUTPUT = ROOT / "Saved/MagazineExpansion"
TABLE_PATH = "/Game/Shooter/Data/DT_WeaponData"
RIFLE_SEQUENCE_PATH = "/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload"
PISTOL_SEQUENCE_PATH = "/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Reload"
TARGETS = ("Pistol", "AWP", "GrenadeLauncher")
GRIP_FIELDS = ("FirstPersonMagazineGripTransform", "ThirdPersonMagazineGripTransform")
LIBRARY = unreal.AnimationLibrary
POSE = unreal.AnimPoseExtensions
SPACE = unreal.AnimPoseSpaces.WORLD


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def magazine_events(sequence):
    result = {}
    for event in LIBRARY.get_animation_notify_events(sequence):
        notify = event.get_editor_property("notify")
        if isinstance(notify, unreal.ShooterAnimNotify_WeaponMagazine):
            stage = str(notify.get_editor_property("stage"))
            require(stage not in result, "弹匣 Notify 阶段重复: " + sequence.get_path_name())
            result[stage] = LIBRARY.get_anim_notify_event_trigger_time(event)
    return result


def pistol_times(sequence):
    result = {}
    for event in LIBRARY.get_animation_notify_events(sequence):
        notify = event.get_editor_property("notify")
        if isinstance(notify, unreal.ShooterAnimNotify_WeaponSound):
            result[str(notify.get_editor_property("stage"))] = LIBRARY.get_anim_notify_event_trigger_time(event)
    # 原素材的拔匣音效晚于左手离枪；Detach 使用首个非零动画帧。
    # Insert 初值对应左手回到弹匣井附近的第 31 帧，音效本身不移动。
    require(str(unreal.ShooterReloadSoundStage.MAGAZINE_OUT) in result, "Pistol 拔匣音效阶段缺失")
    require(str(unreal.ShooterReloadSoundStage.MAGAZINE_IN) in result, "Pistol 插匣音效阶段缺失")
    return (1.0 / 30.0, 31.0 / 30.0)


def socket_reference(mesh, socket_name):
    socket = mesh.find_socket(socket_name)
    require(socket is not None, "Socket 缺失: " + mesh.get_path_name() + ":" + socket_name)
    modifier = unreal.SkeletonModifier()
    require(modifier.set_skeletal_mesh(mesh), "无法读取参考骨架")
    bone = socket.get_editor_property("bone_name")
    require(bone in modifier.get_all_bone_names(), "Socket 父骨骼无效")
    return unreal.MathLibrary.compose_transforms(
        socket.get_socket_local_transform(), modifier.get_bone_transform(bone, True))


def grip_from_contact(sequence, time, character_mesh, weapon_mesh, weapon_socket_name):
    options = unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=character_mesh)
    pose = POSE.get_anim_pose_at_time(sequence, time, options)
    require(POSE.is_valid(pose), "换弹采样姿态无效")
    require("hand_l" in [str(b) for b in POSE.get_bone_names(pose)], "角色缺少 hand_l")
    weapon_socket = character_mesh.find_socket(weapon_socket_name)
    require(weapon_socket is not None, "角色武器挂点缺失")
    parent_pose = POSE.get_bone_pose(pose, weapon_socket.get_editor_property("bone_name"), SPACE)
    weapon_pose = unreal.MathLibrary.compose_transforms(weapon_socket.get_socket_local_transform(), parent_pose)
    magazine_pose = unreal.MathLibrary.compose_transforms(socket_reference(weapon_mesh, "MagazineSocket"), weapon_pose)
    hand_pose = POSE.get_bone_pose(pose, "hand_l", SPACE)
    grip = magazine_pose.make_relative(hand_pose)
    grip.set_editor_property("scale3d", unreal.Vector(1.0, 1.0, 1.0))
    return grip


def transform_text(transform):
    r, t = transform.rotation, transform.translation
    return ("(Rotation=(X={:.9f},Y={:.9f},Z={:.9f},W={:.9f}),"
            "Translation=(X={:.9f},Y={:.9f},Z={:.9f}),Scale3D=(X=1,Y=1,Z=1))").format(
                r.x, r.y, r.z, r.w, t.x, t.y, t.z)


def main():
    mode = unreal.SystemLibrary.get_command_line().split("-MagazinePresentationMode=")
    mode = mode[-1].split()[0] if len(mode) > 1 else "preview"
    require(mode in ("preview", "apply", "verify"), "模式必须是 preview / apply / verify")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    table = unreal.load_asset(TABLE_PATH)
    original = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    rows = copy.deepcopy(original)
    by_name = {row["Name"]: row for row in rows}
    require(all(name in by_name for name in ("Rifle",) + TARGETS), "正式武器配置行缺失")
    rifle = unreal.load_asset(RIFLE_SEQUENCE_PATH)
    pistol = unreal.load_asset(PISTOL_SEQUENCE_PATH)
    detach_stage = str(unreal.ShooterMagazinePresentationStage.DETACH)
    insert_stage = str(unreal.ShooterMagazinePresentationStage.INSERT)
    rifle_events = magazine_events(rifle)
    require(set(rifle_events) == {detach_stage, insert_stage}, "Rifle 基线 Notify 不完整")
    out_time, in_time = pistol_times(pistol)
    require(0.0 < out_time < in_time < pistol.sequence_length, "Pistol 换弹时序无效")

    klass = unreal.load_class(None, "/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter_C")
    cdo = unreal.get_default_object(klass)
    components = {c.get_name(): c for c in cdo.get_components_by_class(unreal.SkeletalMeshComponent)}
    report = {"mode": mode, "pistol_times": [out_time, in_time], "weapons": {}, "saved": []}
    for name in TARGETS:
        row = by_name[name]
        sequence = pistol if name == "Pistol" else rifle
        detach_time = out_time if name == "Pistol" else rifle_events[detach_stage]
        report["weapons"][name] = {"sequence": sequence.get_path_name(), "detach_time": detach_time, "grips": {}}
        for mesh_field, grip_field, component_name, socket_field in (
                ("FirstPersonMesh", GRIP_FIELDS[0], "First Person Mesh", "first_person_weapon_socket"),
                ("ThirdPersonMesh", GRIP_FIELDS[1], "CharacterMesh0", "third_person_weapon_socket")):
            character_mesh = components[component_name].get_editor_property("skeletal_mesh_asset")
            weapon_mesh = unreal.load_asset(row[mesh_field])
            require(unreal.load_asset(row["MagazineMesh"]) is not None, "独立弹匣资源缺失")
            grip = grip_from_contact(sequence, detach_time, character_mesh, weapon_mesh,
                                     str(cdo.get_editor_property(socket_field)))
            require(grip.translation.length() < 25.0, "初始抓握距离异常: " + name)
            report["weapons"][name]["grips"][grip_field] = transform_text(grip)
            if mode != "verify":
                row[grip_field] = transform_text(grip)
            else:
                require(row[grip_field] != by_name["Rifle"][grip_field], "配置不能盲目复制 Rifle 抓握值")
                require("Translation=(X=0.000000,Y=0.000000,Z=0.000000)" not in row[grip_field],
                        "目标抓握仍为 Identity: " + name)

    current_events = magazine_events(pistol)
    if mode == "verify":
        require(set(current_events) == {detach_stage, insert_stage}, "Pistol Notify 配置不完整")
        require(current_events[detach_stage] < current_events[insert_stage], "Pistol Notify 顺序错误")
    elif mode == "apply":
        # 先备份本轮仅允许写入的两个包；Rifle Sequence、网格、Socket 和 AnimBP 不保存。
        backup = OUTPUT / ("Before_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
        for path in (TABLE_PATH, PISTOL_SEQUENCE_PATH):
            relative = Path("Content") / (path.removeprefix("/Game/") + ".uasset")
            target = backup / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, target)
        report["backup"] = str(backup)
        require(not current_events, "Pistol 已有弹匣 Notify；禁止重复 apply")
        track = "Magazine"
        if track not in [str(t) for t in LIBRARY.get_animation_notify_track_names(pistol)]:
            LIBRARY.add_animation_notify_track(pistol, track)
        for stage, time in ((unreal.ShooterMagazinePresentationStage.DETACH, out_time),
                            (unreal.ShooterMagazinePresentationStage.INSERT, in_time)):
            notify = LIBRARY.add_animation_notify_event(pistol, track, time, unreal.ShooterAnimNotify_WeaponMagazine)
            require(notify is not None, "添加 Notify 失败")
            notify.set_editor_property("stage", stage)
        require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)), "配置写回失败")
        captured = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        for before, after in zip(original, captured):
            if before["Name"] in TARGETS:
                before = {k: v for k, v in before.items() if k not in GRIP_FIELDS}
                after = {k: v for k, v in after.items() if k not in GRIP_FIELDS}
            require(before == after, "非目标武器配置发生变化")
        for path in (PISTOL_SEQUENCE_PATH, TABLE_PATH):
            require(unreal.EditorAssetLibrary.save_asset(path), "保存失败: " + path)
            report["saved"].append(path)

    (OUTPUT / (mode + ".json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("MAGAZINE_PRESENTATION_SUCCESS " + mode)


main()
