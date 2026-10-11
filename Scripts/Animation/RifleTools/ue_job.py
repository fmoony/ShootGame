"""在 UE Python Commandlet 中执行明确配置；只向新实验目录保存。"""
import json
import re
import sys
from pathlib import Path
import unreal

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ue_pose_io as io

command = unreal.SystemLibrary.get_command_line()
match = re.search(r'-ToolConfig=(?:"([^"]+)"|(\S+))', command)
assert match, '需要 -ToolConfig=<配置 JSON>'
job = json.loads(Path(match.group(1) or match.group(2)).read_text(encoding='utf-8-sig'))
project = Path(unreal.Paths.project_dir()).resolve()
output = Path(job['output_json']).resolve()
assert output.is_relative_to(project/'Saved'), '取样输出必须在本项目 Saved'
assert not output.exists(), '拒绝覆盖既有取样结果'
output.parent.mkdir(parents=True, exist_ok=True)
if job['operation'] == 'sample':
    asset = unreal.load_asset(job['asset'])
    assert isinstance(asset, unreal.AnimSequence)
    record = {'asset': asset.get_path_name(), 'raw': io.sample(asset, False),
              'retargeted': io.sample(asset, True),
              'length': unreal.AnimationLibrary.get_sequence_length(asset),
              'keys': unreal.AnimationLibrary.get_num_keys(asset)}
elif job['operation'] == 'import':
    io.DEST = job['destination'].rstrip('/')
    assert io.DEST.startswith('/Game/Experiments/RifleTransition/'), '只能导入 Rifle 隔离目录'
    assert not unreal.EditorAssetLibrary.does_directory_exist(io.DEST), '必须使用新目录'
    source_skeleton = unreal.load_asset(job['skeleton'])
    skeleton = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset('SK_Import', io.DEST, source_skeleton)
    assert skeleton and skeleton != source_skeleton
    io.OUT = Path(job['fbx']).resolve().parent
    asset = io.import_fbx(Path(job['fbx']).name, 'MM_Rifle_AimToLowReady', skeleton)
    assert unreal.EditorAssetLibrary.save_loaded_asset(skeleton, False)
    record = {'asset': asset.get_path_name(), 'raw': io.sample(asset, False),
              'retargeted': io.sample(asset, True), 'import_skeleton': skeleton.get_path_name(),
              'length': unreal.AnimationLibrary.get_sequence_length(asset),
              'keys': unreal.AnimationLibrary.get_num_keys(asset)}
else:
    raise ValueError('只支持 sample / import')
assert record['keys'] == 26 and abs(record['length']-25/30)<1e-6
output.write_text(json.dumps(record, indent=2), encoding='utf-8')
unreal.log('RIFLE_TOOL_JOB_PASSED ' + job['operation'])
