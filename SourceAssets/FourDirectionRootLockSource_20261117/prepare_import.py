"""保留原始动作，把 FBX Armature 对象动画烘焙到真实 root 骨；输出独立候选 FBX。"""
import argparse
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Saved/Tools/BlenderPython458'))
import bpy
from mathutils import Matrix,Vector
sys.path.insert(0,str(ROOT/'Scripts/Animation/RifleTools'))
from blender_bridge import uq,converted,angular
sys.path.insert(0,str(ROOT/'Scripts/Animation/Stance'))
from fbx_parent_motion import FBX,mul,inverse,rotate
FILES={'Forward':'Walk Crouching Forward.fbx','Back':'Walk Crouching Backward.fbx','Left':'Walk Crouching Left.fbx','Right':'Walk Crouching Right.fbx'}
NAMES=list(FILES)

parser=argparse.ArgumentParser()
parser.add_argument('--output-dir',default='Saved/FourDirectionRootLockSource_Replay/Candidate')
args=parser.parse_args()
OUT=(ROOT/args.output_dir).resolve()
assert OUT.is_relative_to(ROOT/'Saved') and not OUT.exists(), '候选须为 Saved 内新目录'
OUT.mkdir(parents=True)
hierarchy=json.loads((ROOT/'Scripts/Animation/RifleTools/Inputs/sources.json').read_text(encoding='utf8'))['hierarchy']
report={'clips':[],'source_bone_length_max_cm':0,'blender_source_position_max_cm':0,
    'blender_source_angle_max_deg':0,'bake_position_max_cm':0,'bake_angle_max_deg':0}
expected={'fps':30,'hierarchy':hierarchy,'normalization':'constant default-root reference frame only; retain initial XY, yaw and net travel','clips':[]}

def import_source(path,use_anim):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(path),use_anim=use_anim,
        automatic_bone_orientation=False,ignore_leaf_bones=False,use_prepost_rot=True)
    rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
    assert set(rig.pose.bones.keys())==set(hierarchy)
    assert rig.pose.bones['root'].parent is None
    assert not any(b.constraints for b in rig.pose.bones)
    return rig

for name in NAMES:
    path=ROOT/'SourceAssets/FourDirectionRootLockSource_20261117/Original'/FILES[name]
    fbx=FBX(path)
    count=max(len(v) for v in fbx.curves.values())
    assert all(all(abs(t/46186158000-i/30)<1e-5 for i,t in enumerate(times)) for times in fbx.times.values())
    rp,rq=fbx.world(fbx.names['root'],None)
    iq=inverse(rq)
    offsets={}
    for bone in hierarchy:
        p,q=fbx.world(fbx.names[bone],None)
        p=rotate(iq,[v-w for v,w in zip(p,rp)])
        mapped=Vector((p[0],-p[1],p[2]))
        if hierarchy[bone]['parent'] in hierarchy:
            parent=hierarchy[bone]['parent']
            pp,pq=fbx.world(fbx.names[parent],None)
            pp=Vector(rotate(iq,[v-w for v,w in zip(pp,rp)]))
            pp.y=-pp.y
            report['source_bone_length_max_cm']=max(report['source_bone_length_max_cm'],
                abs((mapped-pp).length-Vector(hierarchy[bone]['rest_local']['p']).length))
        q=mul(iq,q)
        mapped_q=uq([-q[0],q[1],-q[2],q[3]])
        offsets[bone]=mapped_q
    poses=[]
    for frame in range(count):
        pose={}
        for bone in hierarchy:
            p,q=fbx.world(fbx.names[bone],frame)
            p=rotate(iq,[v-w for v,w in zip(p,rp)])
            q=mul(iq,q)
            mapped_q=uq([-q[0],q[1],-q[2],q[3]])
            mapped_q.normalize()
            pose[bone]={'p':[p[0],-p[1],p[2]],'q':[mapped_q.x,mapped_q.y,mapped_q.z,mapped_q.w],'s':[1,1,1]}
        poses.append(pose)

    rest_rig=import_source(path,False)
    root_rest=rest_rig.matrix_world@rest_rig.data.bones['root'].matrix_local
    normalize=Matrix.LocRotScale(root_rest.translation,root_rest.to_quaternion(),Vector((1,1,1))).inverted()
    rig_reference=normalize@rest_rig.matrix_world
    rest_axes={b.name:converted((normalize@rest_rig.matrix_world@b.matrix_local).to_quaternion())
               .inverted()@offsets[b.name] for b in rest_rig.data.bones}
    rig=import_source(path,True)
    original=[]
    for frame in range(count):
        bpy.context.scene.frame_set(frame+1)
        matrices={b.name:normalize@rig.matrix_world@b.matrix for b in rig.pose.bones}
        original.append({n:m.copy() for n,m in matrices.items()})
        for bone,m in matrices.items():
            p=m.translation*100
            mapped=Vector((p.x,-p.y,p.z))
            q=converted(m.to_quaternion())@rest_axes[bone]
            report['blender_source_position_max_cm']=max(report['blender_source_position_max_cm'],
                (mapped-Vector(poses[frame][bone]['p'])).length)
            report['blender_source_angle_max_deg']=max(report['blender_source_angle_max_deg'],
                angular(q,uq(poses[frame][bone]['q'])))

    rig.animation_data_clear()
    rig.name='Armature'
    rig.data.name='FourDirectionParent_'+name
    rig.matrix_world=rig_reference
    rig.animation_data_create()
    action=bpy.data.actions.new('AS_NewCrouch_'+name)
    action.use_fake_user=True
    rig.animation_data.action=action
    order=sorted(rig.pose.bones,key=lambda b:len(b.parent_recursive))
    previous={}
    inverse_rig=rig_reference.inverted()
    for frame,matrices in enumerate(original):
        bpy.context.scene.frame_set(frame+1)
        for bone in order:
            bone.rotation_mode='QUATERNION'
            bone.matrix=inverse_rig@matrices[bone.name]
            if bone.name in previous and bone.rotation_quaternion.dot(previous[bone.name])<0:
                bone.rotation_quaternion.negate()
            previous[bone.name]=bone.rotation_quaternion.copy()
            bpy.context.view_layer.update()
            for channel in ['location','rotation_quaternion','scale']:
                bone.keyframe_insert(data_path=channel,frame=frame+1)
        for bone in rig.pose.bones:
            actual=rig.matrix_world@bone.matrix
            target=matrices[bone.name]
            report['bake_position_max_cm']=max(report['bake_position_max_cm'],
                (actual.translation-target.translation).length*100)
            report['bake_angle_max_deg']=max(report['bake_angle_max_deg'],
                angular(actual.to_quaternion(),target.to_quaternion()))
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:key.interpolation='LINEAR'
    bpy.context.scene.frame_start=1
    bpy.context.scene.frame_end=count
    bpy.context.scene.render.fps=30
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active=rig
    settings=json.loads((ROOT/'Scripts/Animation/RifleTools/fbx_settings.json').read_text(encoding='utf8'))
    settings['object_types']=set(settings['object_types'])
    bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),**settings)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(name+'.blend')))
    expected['clips'].append({'name':name,'keys':count,'fps':30,'duration_s':(count-1)/30,'component':poses})
    report['clips'].append({'name':name,'keys':count,'fps':30,'duration_s':(count-1)/30})
    print('SOURCE_PARENT_BAKED',name,count,flush=True)

assert report['source_bone_length_max_cm']<0.002,report
assert report['blender_source_position_max_cm']<0.005,report
assert report['blender_source_angle_max_deg']<0.005,report
assert report['bake_position_max_cm']<0.005,report
assert report['bake_angle_max_deg']<0.005,report
report['status']='Passed'
for name,data in [('Expected.json',expected),('BakeSummary.json',report)]:
    (OUT/name).write_bytes((json.dumps(data,ensure_ascii=False,separators=(',',':'))+'\r\n').encode('utf8'))
print('SOURCE_PARENT_BAKE_COMPLETE',report,flush=True)
