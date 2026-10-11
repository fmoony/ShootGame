"""已验证的 UE/Blender 反射、四元数和骨骼组件变换。"""
import math
from mathutils import Matrix, Quaternion, Vector
reflect_y = Matrix.Diagonal((1.0, -1.0, 1.0))

def uq(q):
    return Quaternion((q[3], q[0], q[1], q[2])).normalized()


def converted(q):
    return (reflect_y @ q.to_matrix() @ reflect_y).to_quaternion().normalized()


def angular(a, b):
    av, bv = list(a), list(b)
    d = abs(sum(x * y for x, y in zip(av, bv)))
    d /= math.sqrt(sum(x * x for x in av) * sum(y * y for y in bv))
    return math.degrees(2 * math.acos(min(1.0, max(0.0, d))))


def components(local, hierarchy):
    result = {}
    for bone in hierarchy:
        q = uq(local[bone]["q"])
        parent = hierarchy[bone]["parent"]
        result[bone] = result[parent] @ q if parent in result else q
    return result


def fk(local, hierarchy):
    result = {}
    for bone in hierarchy:
        parent = hierarchy[bone]["parent"]
        p = Vector(local[bone]["p"])
        q = uq(local[bone]["q"])
        if parent in result:
            pp, pq = result[parent]
            result[bone] = (pp + pq @ p, pq @ q)
        else:
            result[bone] = (p, q)
    return result
