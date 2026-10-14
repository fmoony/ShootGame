import bpy
import json
import sys
from pathlib import Path
from mathutils import Matrix, Quaternion, Vector

root = Path(__file__).resolve().parents[3]
tools = root/'Scripts/Animation/RifleTools'
sys.path.insert(0, str(tools))
from blender_bridge import converted, uq, angular, fk
args = sys.argv[sys.argv.index('--')+1:]
assert len(args)==2, 'Expected mode and a new output directory'
mode, output = args
assert mode in ('SmoothSeam','RoundTrip'), "Unsupported mode"
out = Path(output).resolve()
assert not out.exists(), 'Output directory already exists; refusing overwrite'
assert out.is_relative_to(root/'Saved'), 'Output must be isolated below Saved'
out.mkdir(parents=True)
sources = json.loads((tools/'Inputs/SeamSources.json').read_text(encoding='utf-8'))

bpy.ops.wm.open_mainfile(filepath=str(tools/'Inputs/CorrectedGrip.blend'))
rig = next(o for o in bpy.context.scene.objects if o.type=='ARMATURE')
bpy.context.scene.frame_set(1)
offsets, scales = {}, {}
reference = sources['transition']['poses'][0]
for bone in rig.pose.bones:
    world = rig.matrix_world @ bone.matrix
    offsets[bone.name] = converted(uq(reference['component'][bone.name]['q'])).inverted() @ world.to_quaternion()
    scales[bone.name] = world.to_scale()
record = sources['transition']
rig.animation_data_clear()
rig.animation_data_create()
rig.animation_data.action = bpy.data.actions.new('Rifle_'+mode)
ordered = sorted(rig.pose.bones, key=lambda b: len(b.parent_recursive))
hierarchy = json.loads((tools/'Inputs/sources.json').read_text())['hierarchy']
bridge_position, bridge_rotation = 0., 0.
intended=[]
for frame, source in enumerate(record['poses']):
    local = {n:dict(v) for n,v in source['local'].items()}
    original_pose = fk(local,hierarchy)
    if mode!='RoundTrip':
        # Distribute endpoint correction across the existing transition; preserve frame zero and LowReady.
        t=frame/30.0
        x=max(0.,min(1.,t/record['length']))
        w=x*x*x*(10-15*x+6*x*x)
        for name in ('clavicle_l','clavicle_r','upperarm_l','upperarm_r',
                     'lowerarm_l','lowerarm_r','hand_l','hand_r'):
            before=uq(record['poses'][-1]['local'][name]['q'])
            goal=uq(sources['low']['poses'][0]['local'][name]['q'])
            delta=goal @ before.inverted()
            q=Quaternion().slerp(delta,w) @ uq(local[name]['q'])
            local[name]['q']=[q.x,q.y,q.z,q.w]
    intended.append({"frame":frame,"local":local})
    component = {}
    for name in hierarchy:
        parent = hierarchy[name]['parent']
        p,q = Vector(local[name]['p']), uq(local[name]['q'])
        component[name] = ((component[parent][0] + component[parent][1] @ p, component[parent][1] @ q)
                           if parent in component else (p,q))
    bpy.context.scene.frame_set(frame+1)
    for bone in ordered:
        p,q = component[bone.name]
        target = Matrix.LocRotScale(Vector((p.x,-p.y,p.z))/100,
                                   converted(q) @ offsets[bone.name], scales[bone.name])
        bone.rotation_mode='QUATERNION'
        bone.matrix = rig.matrix_world.inverted() @ target
        bpy.context.view_layer.update()
        for channel in ('location','rotation_quaternion','scale'):
            bone.keyframe_insert(data_path=channel,frame=frame+1)
    bpy.context.view_layer.update()
    for bone in ordered:
        actual = rig.matrix_world @ bone.matrix
        p,q = component[bone.name]
        bridge_position=max(bridge_position,(actual.translation*100-Vector((p.x,-p.y,p.z))).length)
        bridge_rotation=max(bridge_rotation,angular(actual.to_quaternion(),converted(q) @ offsets[bone.name]))
bpy.context.scene.frame_start=1
bpy.context.scene.frame_end=record['keys']
bpy.context.scene.render.fps=30
for slot in rig.animation_data.action.slots:
    for layer in rig.animation_data.action.layers:
        for strip in layer.strips:
            bag = strip.channelbag(slot)
            if bag:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points: key.interpolation='LINEAR'
assert bridge_position<.01 and bridge_rotation<.02,(bridge_position,bridge_rotation)
assert not any(b.constraints for b in rig.pose.bones)
bpy.ops.wm.save_as_mainfile(filepath=str(out/('Rifle_'+mode+'.blend')))
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
bpy.context.view_layer.objects.active=rig
settings=json.loads((tools/'fbx_settings.json').read_text())
settings['object_types']=set(settings['object_types'])
bpy.ops.export_scene.fbx(filepath=str(out/('Rifle_'+mode+'.fbx')),**settings)
(out/('Blender_'+mode+'.json')).write_text(json.dumps({'position_cm':bridge_position,'rotation_deg':bridge_rotation,
                                                     'frames':record['keys'],'mode':mode}))
(out/('Intended_'+mode+'.json')).write_text(json.dumps(intended))
print('SHOULDER_BLENDER_EXPORTED',mode,bridge_position,bridge_rotation)
