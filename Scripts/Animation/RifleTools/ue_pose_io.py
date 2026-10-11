"""仅导入本轮新目录及复制 Skeleton，按相同 UE 空间比较全部原始采样。"""
import json
import math
import traceback
from pathlib import Path

import unreal

OUT = None
DEST = None
MESH = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
SOURCE_SKEL = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SK_Mannequin")
LIB = unreal.AnimPoseExtensions


def tx(t):
    return {"p": list(t.translation.to_tuple()), "q": list(t.rotation.to_tuple()),
            "s": list(t.scale3d.to_tuple())}


def sample(asset, retarget):
    opts = unreal.AnimPoseEvaluationOptions(
        evaluation_type=unreal.AnimDataEvalType.RAW, should_retarget=retarget,
        extract_root_motion=False, incorporate_root_motion_into_pose=True, optional_skeletal_mesh=MESH)
    length = unreal.AnimationLibrary.get_sequence_length(asset)
    result = []
    for frame in range(26):
        time = frame / 30.0
        pose = LIB.get_anim_pose_at_time(asset, min(time, length), opts)
        assert LIB.is_valid(pose)
        names = [str(b) for b in LIB.get_bone_names(pose)]
        result.append({"frame": frame, "time": time,
                       "local": {b: tx(LIB.get_bone_pose(pose, b, unreal.AnimPoseSpaces.LOCAL)) for b in names},
                       "component": {b: tx(LIB.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)) for b in names},
                       "sockets": {str(s): tx(LIB.get_socket_pose(pose, s, unreal.AnimPoseSpaces.WORLD))
                                   for s in LIB.get_socket_names(pose)}})
    return result


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def angle(a, b):
    dot = abs(sum(x * y for x, y in zip(a, b)))
    dot /= math.sqrt(sum(x * x for x in a) * sum(y * y for y in b))
    return math.degrees(2 * math.acos(min(1.0, max(0.0, dot))))


def compare(a, b):
    maxima = {space: {"position_cm": 0.0, "angle_deg": 0.0, "scale": 0.0}
              for space in ("local", "component", "sockets")}
    detail = []
    for first, second in zip(a, b):
        for space in maxima:
            assert set(first[space]) == set(second[space]), "采样骨骼或 Socket 集合不一致"
            for bone in first[space]:
                ta, tb = first[space][bone], second[space][bone]
                delta = {"position_cm": distance(ta["p"], tb["p"]),
                         "angle_deg": angle(ta["q"], tb["q"]), "scale": distance(ta["s"], tb["s"])}
                for k, value in delta.items():
                    if value > maxima[space][k]:
                        maxima[space][k] = value
                        maxima[space][k + "_at"] = [first["frame"], bone]
                if bone in ("root", "pelvis", "hand_r", "hand_l", "HandGrip_R", "HandGrip_L"):
                    detail.append({"frame": first["frame"], "space": space, "bone": bone, **delta})
    return {"maxima": maxima, "key_bones": detail}


def import_fbx(filename, name, skeleton):
    assert not unreal.EditorAssetLibrary.does_asset_exist(DEST + "/" + name), "拒绝覆盖既有资产"
    opts = unreal.FbxImportUI()
    opts.set_editor_property("automated_import_should_detect_type", False)
    opts.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    opts.set_editor_property("original_import_type", unreal.FBXImportType.FBXIT_ANIMATION)
    opts.set_editor_property("import_mesh", False)
    opts.set_editor_property("import_animations", True)
    opts.set_editor_property("import_materials", False)
    opts.set_editor_property("import_textures", False)
    opts.set_editor_property("skeleton", skeleton)
    data = opts.get_editor_property("anim_sequence_import_data")
    settings = {"use_default_sample_rate": False, "custom_sample_rate": 30,
                "animation_length": unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
                "convert_scene": True, "convert_scene_unit": True, "force_front_x_axis": False,
                "preserve_local_transform": False, "import_bone_tracks": True,
                "import_custom_attribute": False, "add_curve_metadata_to_skeleton": False}
    for key, value in settings.items():
        data.set_editor_property(key, value)
    task = unreal.AssetImportTask()
    task.filename = str(OUT / filename)
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = False
    task.save = False
    task.factory = unreal.FbxFactory()
    task.options = opts
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    assets = task.get_objects()
    anims = [a for a in assets if isinstance(a, unreal.AnimSequence)]
    assert len(anims) == 1, [a.get_path_name() for a in assets]
    assert all(a.get_path_name().startswith(DEST + "/") for a in assets)
    asset = anims[0]
    assert asset.get_skeleton() == skeleton
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    return asset
