"""审计 TP 移动资源；只在 apply 模式下写入专用脚步轨道。"""

import json
import shutil
from datetime import datetime
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.project_dir())
OUTPUT = ROOT / "Saved/FootstepsNotify"
BLEND_PATH = "/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run"
TRACK = "ShooterFootsteps"
LIBRARY = unreal.AnimationLibrary


def main():
    mode_arg = unreal.SystemLibrary.get_command_line().split("-FootstepNotifyMode=")
    mode = mode_arg[-1].split()[0] if len(mode_arg) > 1 else "preview"
    assert mode in ("preview", "apply", "verify")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    blend = unreal.load_asset(BLEND_PATH)
    assert isinstance(blend, unreal.BlendSpace)
    samples = blend.get_editor_property("sample_data")
    report = {"mode": mode, "blend_space": BLEND_PATH,
              "notify_trigger_mode": str(blend.get_editor_property("notify_trigger_mode")),
              "samples": [], "sequences": []}
    assert blend.get_editor_property("notify_trigger_mode") == unreal.NotifyTriggerMode.HIGHEST_WEIGHTED_ANIMATION
    backup = OUTPUT / ("Before_" + datetime.now().strftime("%Y%m%d_%H%M%S"))
    sequences = {}
    for sample in samples:
        sequence = sample.get_editor_property("animation")
        value = sample.get_editor_property("sample_value")
        report["samples"].append({"animation": sequence.get_path_name(),
                                  "value": [value.x, value.y, value.z]})
        sequences[sequence.get_path_name()] = sequence
    for path, sequence in sorted(sequences.items()):
        length = sequence.get_play_length()
        markers = [{"name": str(m.get_editor_property("marker_name")),
                    "time": m.get_editor_property("time")}
                   for m in LIBRARY.get_animation_sync_markers(sequence)]
        notifies = []
        for event in LIBRARY.get_animation_notify_events(sequence):
            notify = event.get_editor_property("notify")
            notifies.append({"name": str(event.get_editor_property("notify_name")),
                             "class": notify.get_class().get_name() if notify else None,
                             "time": LIBRARY.get_anim_notify_event_trigger_time(event)})
        poses = []
        if mode == "preview" and "Idle" not in sequence.get_name():
            for frame in range(round(length * 60) + 1):
                time = min(frame / 60.0, length)
                pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(
                    sequence, time, unreal.AnimPoseEvaluationOptions())
                assert unreal.AnimPoseExtensions.is_valid(pose)
                feet = []
                for foot in ("foot_l", "foot_r"):
                    t = unreal.AnimPoseExtensions.get_bone_pose(pose, foot, unreal.AnimPoseSpaces.WORLD)
                    feet.append([t.translation.x, t.translation.y, t.translation.z])
                poses.append({"time": time, "feet": feet})
        report["sequences"].append({"path": path, "length": length,
                                    "markers": markers, "notifies": notifies, "poses": poses})
        if not markers:
            assert "Idle" in sequence.get_name(), "移动序列缺少左右脚标记"
            continue
        assert path.startswith("/Game/Characters/Mannequins/Anims/Unarmed/")
        assert {m["name"] for m in markers} == {"L", "R"}
        if mode == "apply":
            tracks = [str(t) for t in LIBRARY.get_animation_notify_track_names(sequence)]
            existing = LIBRARY.get_animation_notify_events_for_track(sequence, TRACK) if TRACK in tracks else []
            assert all(isinstance(e.get_editor_property("notify"), unreal.ShooterAnimNotify_Footstep)
                       for e in existing), "专用轨道包含其他事件，停止写入"
            relative = Path("Content") / (path.split(".")[0].removeprefix("/Game/") + ".uasset")
            destination = backup / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, destination)
            if TRACK not in tracks:
                LIBRARY.add_animation_notify_track(sequence, TRACK)
            LIBRARY.remove_animation_notify_events_by_track(sequence, TRACK)
            for marker in markers:
                notify = LIBRARY.add_animation_notify_event(
                    sequence, TRACK, marker["time"], unreal.ShooterAnimNotify_Footstep)
                assert notify is not None
                assert notify.configure_editor_event("foot_l" if marker["name"] == "L" else "foot_r")
            assert all(not e.get_editor_property("trigger_on_dedicated_server")
                       for e in LIBRARY.get_animation_notify_events_for_track(sequence, TRACK))
            assert unreal.EditorAssetLibrary.save_loaded_asset(sequence, only_if_is_dirty=False)
        if mode in ("apply", "verify"):
            events = LIBRARY.get_animation_notify_events_for_track(sequence, TRACK)
            assert len(events) == len(markers), "脚步事件数量与左右脚标记不符"
            actual = []
            for event in events:
                notify = event.get_editor_property("notify")
                assert isinstance(notify, unreal.ShooterAnimNotify_Footstep)
                foot = str(notify.get_editor_property("foot_bone"))
                time = LIBRARY.get_anim_notify_event_trigger_time(event)
                assert any(foot == ("foot_l" if m["name"] == "L" else "foot_r") and
                           abs(time - m["time"]) < 0.001 for m in markers)
                assert not event.get_editor_property("trigger_on_dedicated_server")
                assert abs(event.get_editor_property("trigger_weight_threshold") - 0.1) < 0.001
                assert len(notify.get_editor_property("sounds")) == 5
                actual.append({"foot": foot, "time": time})
            report["sequences"][-1]["footsteps"] = actual
    (OUTPUT / (mode.title() + ".json")).write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log("TP_FOOTSTEP_{}_SUCCESS sequences={}".format(mode.upper(), len(sequences)))


main()
