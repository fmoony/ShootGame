"""负俯仰端点适配与枪口路径制作，复用已验证坐标桥和 FBX 导出设置。"""
import hashlib
import json
import math
from pathlib import Path
import bpy
from mathutils import Quaternion, Vector

import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from blender_bridge import fk as bridge_fk, uq, converted, angular
job_path = Path(sys.argv[sys.argv.index('--')+1]).resolve()
job = json.loads(job_path.read_text(encoding='utf-8-sig'))
project = Path(__file__).resolve().parents[3]
OUT = Path(job['output_dir']).resolve()
assert OUT.is_relative_to(project/'Saved'), '输出只能在本项目 Saved'
assert not OUT.exists(), '拒绝覆盖已有输出目录'
OUT.mkdir(parents=True)
source_file = Path(job['sources_json']).resolve()
source_data = json.loads(source_file.read_text(encoding='utf-8-sig'))
source = source_data['assets']['baseline']['raw']
hierarchy = source_data['hierarchy']
fk = lambda local: bridge_fk(local, hierarchy)
reference = json.loads(Path(job['reference_json']).read_text(encoding='utf-8-sig'))
geometry = reference['geometry']
source_blend = Path(job['source_blend']).resolve()
bpy.ops.wm.open_mainfile(filepath=str(source_blend))
rig = next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
assert rig.name=='root' and 'root' not in rig.pose.bones
bpy.context.scene.frame_set(1)
axis_offsets={'root': Quaternion()}
for name in rig.pose.bones.keys():
    actual=(rig.matrix_world @ rig.pose.bones[name].matrix).to_quaternion().normalized()
    axis_offsets[name]=converted(uq(source[0]['component'][name]['q'])).inverted() @ actual

muzzle_p, muzzle_q = Vector(geometry['Muzzle'][:3]), uq(geometry['Muzzle'][3:])
selected = ['spine_01','spine_02','spine_03','spine_04','spine_05',
            'clavicle_r','upperarm_r','lowerarm_r','hand_r',
            'clavicle_l','upperarm_l','lowerarm_l','hand_l']
settings = json.loads((Path(__file__).parent / 'fbx_settings.json').read_text())
settings['object_types'] = set(settings['object_types'])

def smooth(t):
    return 6*t**5-15*t**4+10*t**3

def qlist(q):
    return [q.x,q.y,q.z,q.w]

def fcurve_digest(rig):
    action = rig.animation_data.action
    curves = list(action.fcurves) if action.is_action_legacy else [
        f for layer in action.layers for strip in layer.strips
        for f in strip.channelbag(rig.animation_data.action_slot).fcurves]
    records = [(f.data_path,f.array_index,[(list(k.co),k.interpolation) for k in f.keyframe_points])
        for f in curves if not any('"'+n+'"' in f.data_path and f.data_path.endswith('rotation_quaternion') for n in selected)]
    return hashlib.sha256(json.dumps(records).encode()).hexdigest(), curves

def gun_angles(q):
    forward = q @ Vector((1,0,0))
    yaw = math.atan2(forward.y,forward.x)
    pitch = math.atan2(forward.z,math.hypot(forward.x,forward.y))
    swing = Quaternion(Vector((0,0,1)),yaw) @ Quaternion(Vector((0,1,0)),-pitch)
    twist = swing.inverted() @ q
    roll = 2*math.atan2(twist.x,twist.w)
    return yaw,pitch,roll

def unwrap(a,b):
    return a+(b-a+math.pi)%(2*math.pi)-math.pi

def gun_q(angles):
    yaw,pitch,roll=angles
    return Quaternion(Vector((0,0,1)),yaw) @ Quaternion(Vector((0,1,0)),-pitch) @ Quaternion(Vector((1,0,0)),roll)

def set_component_rotation(local,name,q):
    parent = hierarchy[name]['parent']
    parent_q = fk(local)[parent][1]
    local[name]['q'] = qlist(parent_q.inverted() @ q)

def solve_arm(local,side,target,target_q,frame,report):
    pose = fk(local)
    upper,lower,hand = ('upperarm_'+side,'lowerarm_'+side,'hand_'+side)
    shoulder,upper_q = pose[upper]
    elbow,lower_q = pose[lower]
    hand_p = pose[hand][0]
    a,b = (elbow-shoulder).length,(hand_p-elbow).length
    direction = (target-shoulder).normalized()
    distance = (target-shoulder).length
    assert abs(a-b)+1e-5 < distance < a+b-1e-5, (side,frame,'unreachable',distance,a+b)
    along = (a*a-b*b+distance*distance)/(2*distance)
    height = math.sqrt(max(0,a*a-along*along))
    pole = elbow-shoulder
    pole = (pole-direction*pole.dot(direction)).normalized()
    assert pole.length > .9, (side,frame,'degenerate elbow plane')
    new_elbow = shoulder+direction*along+pole*height
    upper_target = (elbow-shoulder).rotation_difference(new_elbow-shoulder) @ upper_q
    lower_target = (hand_p-elbow).rotation_difference(target-new_elbow) @ lower_q
    parent_q = pose[hierarchy[upper]['parent']][1]
    for name,q in ((upper,parent_q.inverted() @ upper_target),
                   (lower,upper_target.inverted() @ lower_target),
                   (hand,lower_target.inverted() @ target_q)):
        local[name]['q']=qlist(q)
    report['reach'].append({'frame':frame,'side':side,'extension':distance/(a+b)})

for name,input_pitch in (('Down30',-30),('Down60',-60)):
    bpy.ops.wm.open_mainfile(filepath=str(source_blend))
    rig = next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
    assert rig.name=='root'
    original_digest,_ = fcurve_digest(rig)
    ref = reference['references'][f'{input_pitch}_0']
    cache_fk = fk(ref['armed_cache_local'])
    start = {n:(Vector(t['p']),uq(t['q'])) for n,t in ref['armed_final'].items() if n in hierarchy}
    end = {n:(Vector(t['p']),uq(t['q'])) for n,t in reference['references']['0_0']['lowready_final'].items() if n in hierarchy}
    start_muzzle = Vector(ref['armed_final']['Muzzle']['p'])
    end_muzzle = Vector(reference['references']['0_0']['lowready_final']['Muzzle']['p'])
    start_mq = uq(ref['armed_final']['Muzzle']['q'])
    end_mq = uq(reference['references']['0_0']['lowready_final']['Muzzle']['q'])
    angles0 = gun_angles(start_mq)
    raw_angles1 = gun_angles(end_mq)
    angles1 = tuple(unwrap(a,b) for a,b in zip(angles0,raw_angles1))
    assert angular(gun_q(angles0),start_mq)<.001 and angular(gun_q(angles1),end_mq)<.001
    for bone in selected:
        assert angular(cache_fk[bone][1],start[bone][1])<.005, (bone,'cache-to-final orientation')
    rp,rq=start['hand_r']; lp,lq=start['hand_l']
    relative0p,relative0q=rq.inverted() @ (lp-rp),rq.inverted() @ lq
    rp,rq=end['hand_r']; lp,lq=end['hand_l']
    relative1p,relative1q=rq.inverted() @ (lp-rp),rq.inverted() @ lq
    report={'candidate':name,'pitch':input_pitch,'selected':selected,'frames':[0,25],
            'source':'previous CorrectedGrip blend; actual ArmedAim cache and final LowReady, mask0',
            'method':'quintic muzzle path / explicit yaw pitch roll trajectory; coordinated spine and bilateral offline IK; retained hand-to-hand endpoint offsets',
            'reach':[],'poses':[],'bridge':{'max_position_cm':0.,'max_rotation_deg':0.}}
    previous={}
    for frame,base in enumerate(source):
        bpy.context.scene.frame_set(frame+1)
        for bone in rig.pose.bones:
            actual=rig.matrix_world @ bone.matrix
            expected=base['component'][bone.name]
            desired=Vector((expected['p'][0],-expected['p'][1],expected['p'][2]))
            report['bridge']['max_position_cm']=max(report['bridge']['max_position_cm'],(actual.translation*100-desired).length)
            report['bridge']['max_rotation_deg']=max(report['bridge']['max_rotation_deg'],angular(actual.to_quaternion(),converted(uq(expected['q'])) @ axis_offsets[bone.name]))
        local = {n:dict(t) for n,t in base['local'].items()}
        weight=smooth(frame/25)
        for bone in selected:
            set_component_rotation(local,bone,start[bone][1].slerp(end[bone][1],weight))
        # Outer LBB preserves component rotation while replacing the lower body.
        # Its spine_01 translation offset is removed when posing the actual component.
        posed=fk(local)
        source_offset=posed['spine_01'][0]-start['spine_01'][0]
        actual_spine01=start['spine_01'][0].lerp(end['spine_01'][0],weight)
        source_offset=posed['spine_01'][0]-actual_spine01
        target_muzzle=start_muzzle.lerp(end_muzzle,weight)+source_offset
        target_mq=gun_q(tuple(a+(b-a)*weight for a,b in zip(angles0,angles1)))
        target_rq=target_mq @ muzzle_q.inverted()
        target_rp=target_muzzle-target_rq @ muzzle_p
        solve_arm(local,'r',target_rp,target_rq,frame,report)
        target_lp=target_rp+target_rq @ relative0p.lerp(relative1p,weight)
        target_lq=target_rq @ relative0q.slerp(relative1q,weight)
        solve_arm(local,'l',target_lp,target_lq,frame,report)
        report['poses'].append({'frame':frame,'weight':weight,'local':local,
            'muzzle_goal_source':list(target_muzzle),'muzzle_goal_body':list(target_muzzle-source_offset),
            'muzzle_pitch_goal_deg':math.degrees(angles0[1]+(angles1[1]-angles0[1])*weight)})
        for bone_name in selected:
            bone=rig.pose.bones[bone_name]
            parent=hierarchy[bone_name]['parent']
            b_local=axis_offsets[parent].inverted() @ converted(uq(local[bone_name]['q'])) @ axis_offsets[bone_name]
            rest=(bone.parent.bone.matrix_local.inverted() @ bone.bone.matrix_local if bone.parent else bone.bone.matrix_local)
            basis=(rest.to_quaternion().inverted() @ b_local).normalized()
            if bone_name in previous and basis.dot(previous[bone_name])<0:
                basis.negate()
            previous[bone_name]=basis.copy()
            bone.rotation_mode='QUATERNION'
            bone.rotation_quaternion=basis
            bone.keyframe_insert(data_path='rotation_quaternion',frame=frame+1)
    after_digest,curves=fcurve_digest(rig)
    assert after_digest==original_digest
    assert report['bridge']['max_position_cm']<.01 and report['bridge']['max_rotation_deg']<.01
    for curve in curves:
        if any('"'+n+'"' in curve.data_path for n in selected) and curve.data_path.endswith('rotation_quaternion'):
            for key in curve.keyframe_points:
                key.interpolation='LINEAR'
    assert not any(b.constraints for b in rig.pose.bones)
    bpy.context.scene.frame_start=1
    bpy.context.scene.frame_end=26
    bpy.context.scene.render.fps=30
    blend=OUT / ('RifleAimToLowReady_'+name+'.blend')
    fbx=OUT / ('RifleAimToLowReady_'+name+'.fbx')
    assert not blend.exists() and not fbx.exists()
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    bpy.ops.object.select_all(action='DESELECT')
    rig.select_set(True)
    bpy.context.view_layer.objects.active=rig
    bpy.ops.export_scene.fbx(filepath=str(fbx),**settings)
    report.update({'status':'Exported','unchanged_fcurve_sha256':original_digest,'constraints':0})
    (OUT / ('blender_'+name+'.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('DOWN_EXPORT_PASSED',name,report['bridge'])
