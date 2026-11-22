"""读取本实验 FBX 的父节点动画；严格限定已验证的 Y-up、XYZ、单位缩放格式。"""
import csv
import hashlib
import json
import math
import struct
import zlib
from pathlib import Path

NAMES = ['CrouchIdle', 'CrouchLeft', 'CrouchRight', 'CrouchBack', 'CrouchForwardLeft']


def mul(a, b):
    x, y, z, w = a
    i, j, k, s = b
    return [w*i+x*s+y*k-z*j, w*j-x*k+y*s+z*i,
            w*k+x*j-y*i+z*s, w*s-x*i-y*j-z*k]


def inverse(q):
    return [-q[0], -q[1], -q[2], q[3]]


def rotate(q, v):
    return mul(mul(q, [*v, 0]), inverse(q))[:3]


def euler(v):
    a, b, c = [x*math.pi/360 for x in v]
    return mul(mul([0, 0, math.sin(c), math.cos(c)], [0, math.sin(b), 0, math.cos(b)]),
               [math.sin(a), 0, 0, math.cos(a)])


class FBX:
    def __init__(self, path):
        self.raw = Path(path).read_bytes()
        assert self.raw.startswith(b'Kaydara FBX Binary')
        assert struct.unpack_from('<I', self.raw, 23)[0] == 7700, 'Unsupported FBX version'
        self.pos = 27
        nodes = []
        while self.pos+25 < len(self.raw):
            node = self.node()
            if node is None:
                break
            nodes.append(node)
        self.nodes = nodes
        self.objects = next(n[2] for n in nodes if n[0] == 'Objects')
        self.by_id = {n[1][0]: n for n in self.objects if n[1]}
        self.models = {i: n for i, n in self.by_id.items() if n[0] == 'Model'}
        self.connections = [n[1] for n in next(n[2] for n in nodes if n[0] == 'Connections')]
        self.properties = {i: self.props(n) for i, n in self.models.items()}
        self.parents = {p[1]: p[2] for p in self.connections if p[0] == 'OO'
                        and p[1] in self.models and p[2] in self.models}
        self.names = {n[1][1].split('\x00')[0]: i for i, n in self.models.items()}
        self.channels = {(p[2], p[3]): p[1] for p in self.connections if p[0] == 'OP'
                         and p[2] in self.models and self.by_id[p[1]][0] == 'AnimationCurveNode'}
        self.curves = {}
        self.times = {}
        for p in self.connections:
            if p[0] != 'OP' or self.by_id[p[1]][0] != 'AnimationCurve':
                continue
            curve = self.by_id[p[1]]
            self.curves[p[2], p[3]] = next(n[1][0] for n in curve[2] if n[0] == 'KeyValueFloat')
            self.times[p[2], p[3]] = next(n[1][0] for n in curve[2] if n[0] == 'KeyTime')
        settings = self.props(next(n for n in nodes if n[0] == 'GlobalSettings'))
        assert settings['UpAxis'] == [1] and settings['UpAxisSign'] == [1]
        assert settings['UnitScaleFactor'] == [1.0], 'Expected centimetres'

    @staticmethod
    def props(node):
        return {n[1][0]: n[1][4:] for n in next((x[2] for x in node[2] if x[0] == 'Properties70'), [])}

    def prop(self):
        kind = chr(self.raw[self.pos])
        self.pos += 1
        formats = {'Y': 'h', 'C': '?', 'I': 'i', 'F': 'f', 'D': 'd', 'L': 'q'}
        if kind in formats:
            fmt = '<'+formats[kind]
            value = struct.unpack_from(fmt, self.raw, self.pos)[0]
            self.pos += struct.calcsize(fmt)
            return value
        if kind in 'SR':
            size = struct.unpack_from('<I', self.raw, self.pos)[0]
            self.pos += 4
            value = self.raw[self.pos:self.pos+size]
            self.pos += size
            return value.decode('utf8') if kind == 'S' else None
        assert kind in 'fdlibc', kind
        count, encoding, size = struct.unpack_from('<III', self.raw, self.pos)
        self.pos += 12
        value = self.raw[self.pos:self.pos+size]
        self.pos += size
        assert encoding in [0, 1]
        if encoding:
            value = zlib.decompress(value)
        fmt = {'f': 'f', 'd': 'd', 'l': 'q', 'i': 'i', 'b': '?', 'c': 'b'}[kind]
        return list(struct.unpack('<'+str(count)+fmt, value))

    def node(self):
        end, count, length, size = struct.unpack_from('<QQQB', self.raw, self.pos)
        self.pos += 25
        if not end:
            return None
        assert self.pos <= end <= len(self.raw)
        name = self.raw[self.pos:self.pos+size].decode()
        self.pos += size
        props = [self.prop() for _ in range(count)]
        children = []
        while self.pos+25 <= end:
            child = self.node()
            if child is None:
                break
            children.append(child)
        self.pos = end
        return name, props, children

    def channel(self, model, name, frame):
        default = self.properties[model].get(name, [1, 1, 1] if name == 'Lcl Scaling' else [0, 0, 0])
        if frame is None:
            return default
        node = self.channels.get((model, name))
        result = []
        for i, axis in enumerate('XYZ'):
            values = self.curves.get((node, 'd|'+axis))
            result.append(values[min(frame, len(values)-1)] if values else default[i])
        return result

    def local(self, model, frame):
        properties = self.properties[model]
        assert properties.get('RotationOrder', [0]) == [0]
        assert all(not any(abs(v) > 1e-8 for v in properties.get(n, [0, 0, 0]))
                   for n in ['RotationPivot', 'RotationOffset', 'ScalingPivot', 'ScalingOffset'])
        assert all(abs(v-1) < 1e-8 for v in self.channel(model, 'Lcl Scaling', frame))
        rotation = mul(mul(euler(properties.get('PreRotation', [0, 0, 0])),
                           euler(self.channel(model, 'Lcl Rotation', frame))),
                       inverse(euler(properties.get('PostRotation', [0, 0, 0]))))
        return self.channel(model, 'Lcl Translation', frame), rotation

    def world(self, model, frame):
        p, q = self.local(model, frame)
        if model in self.parents:
            pp, pq = self.world(self.parents[model], frame)
            p = [x+y for x, y in zip(pp, rotate(pq, p))]
            q = mul(pq, q)
        return p, q


def read_motion(path):
    fbx = FBX(path)
    root = fbx.names['root']
    parent = fbx.parents[root]
    assert fbx.models[parent][1][1].split('\x00')[0] == 'SKM_Manny_Simple'
    assert parent not in fbx.parents, 'Unreviewed parent hierarchy'
    count = max(len(v) for v in fbx.curves.values())
    assert 2 <= count <= 1000
    for times in fbx.times.values():
        assert all(abs(t/46186158000-i/30) < 1e-5 for i, t in enumerate(times)), 'Expected 30 FPS'
    rp, rq = fbx.world(root, None)
    iq = inverse(rq)
    frames = []
    for frame in range(count):
        p, q = fbx.world(root, frame)
        p = rotate(iq, [x-y for x, y in zip(p, rp)])
        q = mul(iq, q)
        frames.append(([p[0], -p[1], p[2]], [-q[0], q[1], -q[2], q[3]]))
    # 默认参考变换已完成坐标对齐。父节点的动画偏航是动作的一部分，不能逐片段归零。
    origin = [frames[0][0][0], frames[0][0][1], 0]
    result = [([p[0]-origin[0], p[1]-origin[1], p[2]], q) for p, q in frames]
    # 移动素材的父节点还承载水平行进；实验位移由 Pawn 驱动，只移除净行进，保留周期摆动。
    if Path(path).stem != 'CrouchIdle':
        end = result[-1][0]
        result = [([p[0]-end[0]*i/(count-1), p[1]-end[1]*i/(count-1), p[2]], q)
                  for i, (p, q) in enumerate(result)]
    return result


def main():
    root = Path(__file__).resolve().parents[3]
    destination = root/'Saved/StancePreview/ParentMotion'
    destination.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name in NAMES:
        path = root/'SourceAssets/StanceCrouch'/f'{name}.fbx'
        frames = read_motion(path)
        with (destination/f'{name}.csv').open('w', newline='', encoding='utf8') as stream:
            writer = csv.writer(stream)
            writer.writerow(['frame', 'px', 'py', 'pz', 'qx', 'qy', 'qz', 'qw'])
            for i, (p, q) in enumerate(frames):
                writer.writerow([i, *p, *q])
        manifest.append({'name': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                         'frames': len(frames), 'placement': 'remove initial XY and planar travel; retain authored yaw'})
    (destination/'Manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
    print('Parent motion extracted:', [(x['name'], x['frames']) for x in manifest])


if __name__ == '__main__':
    main()
