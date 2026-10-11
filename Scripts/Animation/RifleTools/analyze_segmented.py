"""检查分段选片、活动期锁定和真实累计时间；保留边界姿态风险。"""
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
import sys

out = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parents[3]/'Saved/RifleTools/Measure'
threshold = float(sys.argv[2]) if len(sys.argv)>2 else -50.0
metrics = defaultdict(list)
poses = defaultdict(lambda: defaultdict(dict))
with (out/'runtime_metrics.csv').open(encoding='utf-8-sig') as stream:
    for r in csv.DictReader(stream):
        key=(float(r['case_pitch']),int(r['strength']),r['scenario'])
        metrics[key].append(r)
with (out/'runtime_poses.csv').open(encoding='utf-8-sig') as stream:
    for r in csv.DictReader(stream):
        key=(float(r['case_pitch']),int(r['strength']),r['scenario'])
        poses[key][int(r['frame'])][r['bone']]=[float(r[n]) for n in ('px','py','pz','qx','qy','qz','qw')]

def angle(a,b):
    d=abs(sum(x*y for x,y in zip(a,b)))/math.sqrt(sum(x*x for x in a)*sum(y*y for y in b))
    return math.degrees(2*math.acos(min(1,d)))

def pitch(t):
    x,y,z,w=t[3:]
    forward=(1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w))
    return math.degrees(math.atan2(forward[2],math.hypot(*forward[:2])))

def name(path):
    asset_name = path.rsplit('.',1)[-1]
    if '/Down60/' in path or asset_name.endswith('_Down60'): return 'Down60'
    if '/Down30/' in path or asset_name.endswith('_Down30'): return 'Down30'
    return 'Original'

def expected(pi):
    return 'Down60' if pi < threshold else 'Down30' if pi <= -30 else 'Original'

report={'down60_threshold':threshold,
        'scope':'actual graph; fixed 60Hz; IK off; direct input pitch changes, no cross-clip blending',
        'cases':[], 'violations':[], 'frames':sum(map(len,metrics.values()))}
for key,rows in metrics.items():
    pi,strength,scenario=key
    p=poses[key]
    assert len(rows)==260 and len(p)==260
    assert all(math.isfinite(v) for frame in p.values() for t in frame.values() for v in t)
    entries=[]
    for f,r in enumerate(rows):
        if f==0: continue
        prev=rows[f-1]
        if int(r['selections'])!=int(prev['selections']):
            entries.append({'frame':f,'pitch':float(r['pitch']),'clip':name(r['sequence']),'time':float(r['time'])})
            if name(r['sequence'])!=expected(float(r['pitch'])):report['violations'].append([key,f,'wrong selection'])
        if prev['state']=='AimTransition' and r['state']=='AimTransition':
            if r['sequence']!=prev['sequence'] or r['selections']!=prev['selections']:
                report['violations'].append([key,f,'clip changed while active'])
            dt=float(r['time'])-float(prev['time'])
            rate=float(r['rate'])
            want=min(25/30,max(0,float(prev['time'])+rate/60))-float(prev['time'])
            if abs(dt-want)>1e-6:report['violations'].append([key,f,'time discontinuity'])
    assert {'ArmedAim','AimTransition'}.issubset({r['state'] for r in rows})
    if scenario in ('Full','ChangeDuringLower','ChangeDuringRaise'):
        assert 'LowReady' in {r['state'] for r in rows}
    assert rows[-1]['state']=='ArmedAim'
    mp=[pitch(p[f]['Muzzle']) for f in range(59,140)]
    z=[p[f]['Muzzle'][2] for f in range(59,140)]
    switches=[]
    for f in range(1,260):
        if float(rows[f]['rate'])<0 and float(rows[f-1]['rate'])>0:
            switches.append({'frame':f,'before_time':float(rows[f-1]['time']),'after_time':float(rows[f]['time']),
                'clip_before':name(rows[f-1]['sequence']),'clip_after':name(rows[f]['sequence']),
                'hand_r_cm':math.dist(p[f]['hand_r'][:3],p[f-1]['hand_r'][:3]),
                'muzzle_deg':angle(p[f]['Muzzle'][3:],p[f-1]['Muzzle'][3:])})
    report['cases'].append({'pitch':pi,'strength':strength,'scenario':scenario,'entries':entries,'reverse':switches,
        'entry_muzzle_step_deg':angle(p[60]['Muzzle'][3:],p[59]['Muzzle'][3:]),
        'entry_hand_r_step_cm':math.dist(p[60]['hand_r'][:3],p[59]['hand_r'][:3]),
        'muzzle_pitch_extra_deg':max(0,max(mp)-max(mp[0],mp[-1])),
        'muzzle_height_extra_cm':max(0,max(z)-max(z[0],z[-1])),
        'max_hand_r_step_cm':max(math.dist(p[f]['hand_r'][:3],p[f-1]['hand_r'][:3]) for f in range(60,220)),
        'max_muzzle_step_deg':max(angle(p[f]['Muzzle'][3:],p[f-1]['Muzzle'][3:]) for f in range(60,220))})
assert not report['violations'], report['violations'][:10]
report['status']='Passed selection/time checks; human motion review pending'
(out/'analysis.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
for r in report['cases']:
    if r['scenario']=='Full' and r['strength']==0: print(json.dumps(r))
print('SEGMENTED_ANALYSIS_PASSED',len(metrics),'cases',report['frames'],'frames')
