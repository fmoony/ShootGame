"""验证 Rifle 动画副本的嵌套图、连线和节点配置；默认只读，--prune 显式清理副本遗留节点并保存。"""
import argparse
import json
from pathlib import Path

from TestMcpEditorTools import McpClient

SOURCE = "/Game/Shooter/Animation/ThirdPerson/ABP_TP_Rifle"
TARGET = "/Game/Shooter/Blueprints/NewAnimBlueprint"
PARENT = "/Script/ShootGame.ShooterThirdPersonAnimInstance"


def snapshot(client, asset):
    def call(action, **args):
        return client.tool("manage_blueprint", dict(action=action, blueprintPath=asset, **args))

    summary = call("get_blueprint")
    graphs = {}
    for graph in summary["graphs"]:
        path = graph["graphPath"]
        data = call("get_graph_details", graphName=path, limit=500)
        for node in data["nodes"]:
            details = call("get_node_details", graphName=path, nodeId=node["id"])
            node["editableProperties"] = details["editableProperties"]
        graphs[path] = data
    return dict(summary=summary, graphs=graphs, defaults=call("get_class_defaults"))


def comparable(node):
    # 引脚 GUID 会由 RefreshAllNodes 重建；逻辑连接由节点 GUID + 引脚名标识。
    result = {k: node[k] for k in ("id", "name", "class", "x", "y", "editableProperties")}
    result["pins"] = [{k: v for k, v in pin.items() if k != "id"} for pin in node["pins"]]
    text = json.dumps(result, sort_keys=True)
    for path in (SOURCE, TARGET):
        name = path.rsplit("/", 1)[1]
        text = text.replace(path + "." + name, "ANIM_BLUEPRINT")
    return json.loads(text)


def validate(source, target, pruned):
    assert target["summary"]["parentClass"] == PARENT
    assert target["summary"]["skeleton"] == source["summary"]["skeleton"]
    assert set(source["graphs"]) == set(target["graphs"]), "Missing nested graph"
    total = 0
    for path, graph in source["graphs"].items():
        expected = {n["id"]: n for n in graph["nodes"] if n["id"] not in pruned}
        actual = {n["id"]: n for n in target["graphs"][path]["nodes"]}
        assert expected.keys() == actual.keys(), (path, expected.keys() ^ actual.keys())
        for guid in expected:
            left, right = comparable(expected[guid]), comparable(actual[guid])
            assert left == right, (path, expected[guid]["title"], {k: (left[k], right[k]) for k in left if left[k] != right[k]})
            total += 1
    source_defaults = {p["name"]: p["valueText"] for p in source["defaults"]["properties"]}
    target_defaults = {p["name"]: p["valueText"] for p in target["defaults"]["properties"]}
    assert source_defaults == target_defaults, {k: (v, target_defaults.get(k)) for k, v in source_defaults.items() if target_defaults.get(k) != v}
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prune", action="store_true")
    parser.add_argument("--expect-pruned", action="store_true")
    args = parser.parse_args()
    client = McpClient()
    report = Path("Saved/MCP/AnimBlueprintCopyValidation.json")
    result = {}
    try:
        source = snapshot(client, SOURCE)
        target = snapshot(client, TARGET)
        result.update(source=source, before=target)
        # 只清理已审计的旧事件链；保留唯一有效的换弹表现通知。
        removed = [n["id"] for n in source["graphs"]["EventGraph"]["nodes"]
                   if n["title"] not in ("AnimNotify_ReloadRecovery", "BeginReloadPresentationRecovery")]
        removed += [n["id"] for n in source["graphs"]["AnimGraph"]["nodes"]
                    if n["title"] == "Get CalibratedWeaponGripInRightHandSpace"]
        armed_aim = next(path for path in source["graphs"] if path.endswith(".ArmedAim"))
        removed += [n["id"] for n in source["graphs"][armed_aim]["nodes"]
                    if n["title"] in ("Sequence Player 'MF_Rifle_Idle_ADS'", "AimOffset Player 'AO_Rifle'", "Get AimPitchN")]
        assert len(removed) == 19, "Source changed; review cleanup scope"
        present = {n["id"] for graph in target["graphs"].values() for n in graph["nodes"]}
        already_removed = set(removed) - present
        validate(source, target, already_removed if args.prune else set(removed) if args.expect_pruned else set())
        if args.prune:
            for guid in removed:
                if guid in already_removed:
                    continue
                graph = next(path for path, data in source["graphs"].items() if any(n["id"] == guid for n in data["nodes"]))
                client.tool("manage_blueprint", dict(action="delete_node", blueprintPath=TARGET, graphName=graph, nodeId=guid))
            result["compile"] = client.tool("manage_blueprint", dict(action="save_blueprint", blueprintPath=TARGET))
            assert result["compile"]["errors"] == 0 and result["compile"]["warnings"] == 0 and result["compile"]["saved"]
            target = snapshot(client, TARGET)
        result["nodesVerified"] = validate(source, target, set(removed) if args.prune or args.expect_pruned else set())
        result.update(status="Passed", removedNodeIds=removed if args.prune or args.expect_pruned else [], after=target)
    except Exception as error:
        result.update(status="Failed", error=str(error))
        raise
    finally:
        result["events"] = client.events
        report.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps({k: v for k, v in result.items() if k in ("status", "nodesVerified", "error", "compile")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
