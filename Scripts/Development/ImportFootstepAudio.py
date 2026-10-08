"""在完整 UE 编辑器中导入基础脚步音效；要求关闭同项目的其他编辑器实例。"""

import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
destination = '/Game/Shooter/Audio/Footsteps'
tasks = []
for index in range(5):
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(root / 'SourceAssets/Audio/Footsteps' / f'footstep_concrete_{index:03}.wav'))
    task.set_editor_property('destination_path', destination)
    task.set_editor_property('destination_name', f'SW_Footstep_Concrete_{index:03}')
    task.set_editor_property('automated', True)
    task.set_editor_property('save', True)
    task.set_editor_property('replace_existing', True)
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
result = []
for task in tasks:
    paths = task.get_editor_property('imported_object_paths')
    assert paths, 'Footstep import failed'
    sound = unreal.load_asset(paths[0])
    result.append({'path': paths[0], 'duration': sound.get_editor_property('duration'),
                   'channels': sound.get_editor_property('num_channels')})
attenuation = unreal.load_asset(destination + '/SA_Footsteps') or unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'SA_Footsteps', destination, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
assert attenuation
settings = attenuation.get_editor_property('attenuation')
settings.set_editor_property('attenuate', True)
settings.set_editor_property('spatialize', True)
settings.set_editor_property('attenuation_shape_extents', unreal.Vector(150.0, 0.0, 0.0))
settings.set_editor_property('falloff_distance', 1800.0)
attenuation.set_editor_property('attenuation', settings)
assert unreal.EditorAssetLibrary.save_loaded_asset(attenuation)
character = unreal.load_object(None, '/Game/Shooter/Blueprints/Characters/BP_ShooterCharacter.BP_ShooterCharacter_C')
cdo = unreal.get_default_object(character)
result.append({'max_walk_speed': cdo.get_editor_property('character_movement').get_editor_property('max_walk_speed')})
(root / 'Saved/Footsteps/ImportReport.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
unreal.log('FOOTSTEP_IMPORT_SUCCESS ' + json.dumps(result))
