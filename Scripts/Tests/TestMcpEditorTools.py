"""在运行中的编辑器经真实 MCP 协议验证工具，并保留验收蓝图与截图。

默认仅查询工具列表；--exercise 显式允许修改测试蓝图。
"""
import argparse
import json
import os
from pathlib import Path
import time
import urllib.request


class McpClient:
    def __init__(self, url="http://127.0.0.1:3000/mcp"):
        self.url, self.session, self.counter = url, None, 0
        self.events = []
        self.request("initialize", {
            "protocolVersion": "2025-03-26", "capabilities": {},
            "clientInfo": {"name": "ShootGame-McpEditorTests", "version": "1.0"},
        })
        self.request("notifications/initialized", {}, notification=True)

    def request(self, method, params, notification=False):
        self.counter += 1
        body = {"jsonrpc": "2.0", "method": method, "params": params}
        if not notification:
            body["id"] = self.counter
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.session:
            headers["Mcp-Session-Id"] = self.session
            headers["MCP-Protocol-Version"] = "2025-03-26"
        token = os.environ.get("MCP_CAPABILITY_TOKEN")
        if token:
            headers["X-MCP-Capability-Token"] = token
        req = urllib.request.Request(self.url, json.dumps(body).encode(), headers)
        with urllib.request.urlopen(req, timeout=180) as response:
            self.session = response.headers.get("Mcp-Session-Id", self.session)
            # 逐帧读取 SSE，取得最终结果后立即返回，不等待持久连接关闭。
            if "text/event-stream" in response.headers.get("Content-Type", ""):
                for line in response:
                    if line.startswith(b"data: "):
                        message = json.loads(line[6:])
                        if message.get("id") == self.counter:
                            break
                else:
                    raise RuntimeError("MCP stream ended without a result")
            else:
                text = response.read().decode()
                if not text:
                    return None
                message = json.loads(text)
        if "error" in message:
            raise RuntimeError(message["error"])
        return message.get("result")

    def tool(self, name, arguments, expected_error=None):
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        self.events.append({"tool": name, "arguments": arguments, "result": result})
        text = "\n".join(c.get("text", "") for c in result.get("content", []))
        if expected_error:
            assert result.get("isError") and expected_error in text, text
            return result
        assert not result.get("isError"), text
        return json.loads(text.split("\n\n", 1)[1]) if "\n\n" in text else {}


def exercise(client, blueprint):
    def bp(action, expected_error=None, **args):
        return client.tool("manage_blueprint", {"action": action, "blueprintPath": blueprint, **args}, expected_error)

    def settings(action, **args):
        return client.tool("manage_editor_settings", {"action": action, **args})

    tools = client.request("tools/list", {})["tools"]
    assert {"manage_asset", "manage_blueprint", "manage_editor_settings"} <= {t["name"] for t in tools}
    class_defaults = bp("get_class_defaults", propertyName="bReplicates")
    assert class_defaults["found"] and len(class_defaults["properties"]) == 1, class_defaults
    config = settings("read_config", file="Engine", section="/Script/EngineSettings.GameMapsSettings", key="GameDefaultMap")
    assert config["found"] and "/Game/Shooter/Maps/Lvl_Shooter" in config["value"], config
    assert config["sourceFile"].endswith(".ini"), config
    missing = settings("read_config", file="Engine", section="McpMissingSection_6B95D529", key="Missing")
    assert not missing["found"] and "value" not in missing, missing
    sections = settings("list_sections", file="EditorPerProjectUserSettings", query="LevelEditorViewportSettings")
    defaults = settings("get_settings", classPath="/Script/UnrealEd.LevelEditorViewportSettings", propertyName="CameraSpeed")
    assert defaults["found"] and len(defaults["properties"]) == 1, defaults
    client.tool("manage_editor_settings", {"action": "read_config", "file": "../../secret.ini", "section": "x"}, "INVALID_CONFIG_FILE")
    client.tool("manage_blueprint", {"action": "create_blueprint", "blueprintPath": "/Engine/McpShouldNotExist"}, "INVALID_PATH")

    # 独立新资产验证创建→改父类→资产系统移动，避免移动用户的验收蓝图。
    probe = "/Game/Shooter/Blueprints/McpToolTests/BP_McpLifecycleProbe"
    moved = "/Game/Shooter/Blueprints/McpToolTests/BP_McpCreatedAndMoved"
    exists = client.tool("manage_asset", {"action": "exists", "path": moved})["exists"]
    if not exists:
        if not client.tool("manage_asset", {"action": "exists", "path": probe})["exists"]:
            client.tool("manage_blueprint", {"action": "create_blueprint", "blueprintPath": probe, "save": True})
        client.tool("manage_blueprint", {"action": "create_blueprint", "blueprintPath": probe}, "ASSET_EXISTS")
        parent = client.tool("manage_blueprint", {"action": "reparent_blueprint", "blueprintPath": probe, "parentClass": "/Script/Engine.Pawn", "save": True})
        assert parent["parentClass"] == "/Script/Engine.Pawn" and parent["errors"] == 0, parent
        relocation = client.tool("manage_blueprint", {"action": "move_blueprint", "blueprintPath": probe, "destinationPath": moved, "save": True})
        assert relocation["redirectorFixed"] and relocation["saved"], relocation
    assert client.tool("manage_asset", {"action": "exists", "path": moved})["exists"]

    graph = bp("get_graph_details", graphName="EventGraph", limit=500)
    # 重试只清理本脚本明确标记的节点；保留原有的三个模板事件。
    for node in graph["nodes"]:
        if node.get("comment", "").startswith("MCP acceptance:"):
            bp("delete_node", graphName="EventGraph", nodeId=node["id"])
    graph = bp("get_graph_details", graphName="EventGraph", limit=500)
    begin = next(n["id"] for n in graph["nodes"] if n["title"] == "Event BeginPlay")
    assert not next(p for n in graph["nodes"] if n["id"] == begin for p in n["pins"] if p["name"] == "then")["links"], "BeginPlay has user connections; refusing to replace them"

    def add(node_type, name, x, y, **args):
        return bp("add_node", nodeType=node_type, graphName="EventGraph", comment="MCP acceptance: " + name, x=x, y=y, **args)["nodeId"]

    branch = add("branch", "server or client", 380, 0)
    server = add("call_function", "query authority", 0, -200, functionClass="/Script/Engine.KismetSystemLibrary", functionName="IsServer")
    yes = add("call_function", "server message", 740, -90, functionClass="/Script/Engine.KismetSystemLibrary", functionName="PrintString")
    no = add("call_function", "client message", 740, 220, functionClass="/Script/Engine.KismetSystemLibrary", functionName="PrintString")
    comment = add("comment", "temporary delete probe", 1400, 600)
    bp("move_node", nodeId=comment, x=1450, y=650)
    bp("delete_node", nodeId=comment)
    bp("set_pin_default", nodeId=yes, pinName="InString", defaultValue="MCP: Server branch")
    bp("set_pin_default", nodeId=no, pinName="InString", defaultValue="MCP: Client branch")

    links = [(begin, "then", branch, "execute"), (server, "ReturnValue", branch, "Condition"),
             (branch, "then", yes, "execute"), (branch, "else", no, "execute")]

    def connection(action, link, expected_error=None):
        a, ap, b, bpin = link
        return bp(action, expected_error, sourceNodeId=a, sourcePinName=ap, targetNodeId=b, targetPinName=bpin)

    for link in links:
        connection("connect_pins", link)
    connection("connect_pins", links[0])  # 已有连接幂等
    connection("connect_pins", (begin, "then", yes, "execute"), "LINK_REPLACEMENT_REQUIRED")
    connection("connect_pins", (begin, "then", branch, "Condition"), "INCOMPATIBLE_PINS")
    # 显式替换必须确实移除旧执行输出，并且能够恢复到最终验收链。
    bp("connect_pins", sourceNodeId=begin, sourcePinName="then", targetNodeId=yes,
       targetPinName="execute", breakExisting=True)
    redirected = bp("get_pin_details", graphName="EventGraph", nodeId=begin, pinName="then")
    assert len(redirected["links"]) == 1 and redirected["links"][0]["nodeId"] == yes, redirected
    connection("disconnect_pins", (begin, "then", yes, "execute"))
    connection("connect_pins", links[0])
    connection("disconnect_pins", links[-1])
    connection("connect_pins", links[-1])
    actual = bp("get_graph_details", graphName="EventGraph", limit=500)
    for a, ap, b, bpin in links:
        pin = next(p for n in actual["nodes"] if n["id"] == a for p in n["pins"] if p["name"] == ap)
        assert any(l["nodeId"] == b and l["pinName"] == bpin for l in pin["links"]), (a, ap, b, bpin)
    compiled = bp("save_blueprint")
    assert compiled["errors"] == 0 and compiled["saved"], compiled
    bp("open_blueprint", graphName="EventGraph")
    time.sleep(2)
    image = bp("capture_graph", graphName="EventGraph")
    assert Path(image["imagePath"]).is_file() and image["width"] > 300, image
    return {"status": "Passed", "blueprint": blueprint, "lifecycleBlueprint": moved,
            "screenshot": image, "links": links, "config": config, "settings": defaults, "sections": sections,
            "classDefaults": class_defaults, "compile": compiled, "events": client.events}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:3000/mcp")
    parser.add_argument("--exercise", action="store_true")
    parser.add_argument("--blueprint", default="/Game/Shooter/Blueprints/NewBlueprint")
    parser.add_argument("--report", default="Saved/MCP/EditorToolsValidation.json")
    args = parser.parse_args()
    client = McpClient(args.url)
    if not args.exercise:
        print(json.dumps(client.request("tools/list", {}), ensure_ascii=False))
        return
    report = Path(args.report)
    report.parent.mkdir(parents=True, exist_ok=True)
    try:
        result = exercise(client, args.blueprint)
    except Exception as error:
        report.write_text(json.dumps({"status": "Failed", "error": str(error), "events": client.events}, ensure_ascii=False, indent=2), encoding="utf-8")
        raise
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k != "events"}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
