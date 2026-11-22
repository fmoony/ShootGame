"""仅重导入新四方向资源；完整父层运动转换，不改写步态。"""
import json,math
from pathlib import Path
import unreal

R=Path(unreal.Paths.project_dir()).resolve()
O=R/'Saved/FourDirectionRootLockSource_20261117_01'
S=R/'SourceAssets/FourDirectionRootLockSource_20261117/ImportReady'
D='/Game/Experiments/Stance_20261022/FourDirectionRootLockSource_20261117'
expected=json.loads((S/'Expected.json').read_text())
formal=unreal.load_asset('/Game/Characters/Mannequins/Meshes/SK_Mannequin')
mesh=unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
lib=unreal.AnimPoseExtensions
def tx(t):
 return {'p':list(t.translation.to_tuple()),'q':list(t.rotation.to_tuple()),'s':list(t.scale3d.to_tuple())}
def fp(sk):
 pose=sk.get_reference_pose()
 return {'bones':{str(n):tx(lib.get_bone_pose(pose,n,unreal.AnimPoseSpaces.LOCAL)) for n in lib.get_bone_names(pose)},
 'modes':[str(b.get_editor_property('translation_retargeting_mode')) for b in sk.get_editor_property('bone_tree')]}
def angle(a,b):
 d=abs(sum(x*y for x,y in zip(a,b)))/math.sqrt(sum(x*x for x in a)*sum(y*y for y in b))
 return math.degrees(2*math.acos(min(1,d)))
assert all(not unreal.EditorAssetLibrary.does_asset_exist(D+'/'+n) for n in ['AS_RootLockSource_Forward','AS_RootLockSource_Back','AS_RootLockSource_Left','AS_RootLockSource_Right'])
before=fp(formal)
tools=unreal.AssetToolsHelpers.get_asset_tools()
sk=unreal.load_asset('/Game/Experiments/Stance_20261022/FourDirection_20261117/SK_FourDirection')
assert fp(sk)==before


report={'status':'Running','normalization':expected['normalization'],'clips':[],'errors':[]}
assets=[]
for clip in expected['clips']:
 name=clip['name']
 opt=unreal.FbxImportUI()
 for k,v in {'automated_import_should_detect_type':False,'mesh_type_to_import':unreal.FBXImportType.FBXIT_ANIMATION,
 'original_import_type':unreal.FBXImportType.FBXIT_ANIMATION,'import_mesh':False,'import_animations':True,
 'import_materials':False,'import_textures':False,'skeleton':sk}.items():opt.set_editor_property(k,v)
 data=opt.get_editor_property('anim_sequence_import_data')
 for k,v in {'use_default_sample_rate':False,'custom_sample_rate':30,
 'animation_length':unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,'convert_scene':True,
 'convert_scene_unit':True,'force_front_x_axis':False,'preserve_local_transform':False,
 'import_bone_tracks':True,'import_custom_attribute':False,'add_curve_metadata_to_skeleton':False}.items():
  data.set_editor_property(k,v)
 task=unreal.AssetImportTask()
 task.filename=str(S/(name+'.fbx'))
 task.destination_path=D;task.destination_name='AS_RootLockSource_'+name
 task.automated=True;task.replace_existing=False;task.save=False;task.factory=unreal.FbxFactory();task.options=opt
 tools.import_asset_tasks([task])
 imported=[a for a in task.get_objects() if isinstance(a,unreal.AnimSequence)]
 assert len(imported)==1
 a=imported[0]
 assert a.get_skeleton()==sk
 assert unreal.AnimationLibrary.get_num_keys(a)==clip['keys']
 assert abs(unreal.AnimationLibrary.get_sequence_length(a)-clip['duration_s'])<1e-5
 unreal.AnimationLibrary.set_root_motion_enabled(a,False)
 unreal.AnimationLibrary.set_is_root_motion_lock_forced(a,False)
 a.set_preview_skeletal_mesh(mesh)
 entry={'direction':name,'asset':a.get_path_name(),'skeleton':sk.get_path_name(),
 'source':list(a.get_editor_property('asset_import_data').extract_filenames()),
 'keys':clip['keys'],'length':clip['duration_s'],'checks':[]}
 for mode,retarget in [(unreal.AnimDataEvalType.RAW,False),(unreal.AnimDataEvalType.RAW,True),(unreal.AnimDataEvalType.COMPRESSED,True)]:
  opt_pose=unreal.AnimPoseEvaluationOptions(evaluation_type=mode,should_retarget=retarget,
  extract_root_motion=False,incorporate_root_motion_into_pose=True,optional_skeletal_mesh=mesh)
  mp=mq=0
  for i,target in enumerate(clip['component']):
   pose=lib.get_anim_pose_at_time(a,i/30,opt_pose)
   assert lib.is_valid(pose)
   for bone,t in target.items():
    actual=tx(lib.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD))
    mp=max(mp,math.dist(actual['p'],t['p']))
    mq=max(mq,angle(actual['q'],t['q']))
  check={'mode':str(mode),'retarget':retarget,'bone_frames':clip['keys']*len(clip['component'][0]),
  'max_position_cm':mp,'max_angle_deg':mq}
  entry['checks'].append(check)
  if mp>(0.25 if mode==unreal.AnimDataEvalType.COMPRESSED else 0.05) or mq>0.1:
   report['errors'].append([name,check])
 assets.append(a);report['clips'].append(entry)
 unreal.log('FOUR_REIMPORT_CHECK '+name+' '+str(entry['checks']))
assert fp(formal)==before
assert fp(sk)==before
report['reference_unchanged']=True
report['status']='Failed' if report['errors'] else 'Passed'
(O/'ImportCheck.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
assert not report['errors'],report['errors']
for a in assets:
 assert unreal.EditorAssetLibrary.save_loaded_asset(a,False)
report['saved']=[a.get_path_name() for a in assets]
(O/'ImportCheck.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('ROOT_LOCK_SOURCE_IMPORT_PASSED')
