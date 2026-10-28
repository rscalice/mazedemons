# Read-only (guns v2): dumps triangles (9 floats each) of the gun meshes to Saved\ClaudeDump\gtris_<name>.csv for offline silhouette plots.
import os
import traceback
import unreal

DUMP = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump")
MESHES = {
    "revolver": ("/Game/Fab/Free_Revolver___Handcanon/tesseract_revolver/StaticMeshes/tesseract_revolver", False),
    "ion": ("/Game/Fab/ION_RIFLE_-__EC-11_/ion_rifle_ec_11/SkeletalMeshes/ion_rifle_ec_11", True),
    "beam": ("/Game/Fab/Beam_Rifle/model/StaticMeshes/model", False),
}
log = []
mq = unreal.GeometryScript_MeshQueries


def unwrap(r):
    return r[0] if isinstance(r, tuple) else r


for key, (path, skeletal) in MESHES.items():
    try:
        a = unreal.load_asset(path)
        dm = unreal.DynamicMesh()
        opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
        lod = unreal.GeometryScriptMeshReadLOD(unreal.GeometryScriptLODType.MAX_AVAILABLE, 0)
        if skeletal:
            unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(a, dm, opts, lod)
        else:
            unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(a, dm, opts, lod)
        nv = int(unwrap(mq.get_num_vertex_i_ds(dm)))
        vp = {}
        for i in range(nv):
            r = mq.get_vertex_position(dm, i)
            if isinstance(r, tuple):
                if not r[1]:
                    continue
                vp[i] = r[0]
            else:
                vp[i] = r
        nt = int(unwrap(mq.get_num_triangle_i_ds(dm)))
        n = 0
        with open(os.path.join(DUMP, "gtris_%s.csv" % key), "w") as f:
            for t in range(nt):
                r = mq.get_triangle_indices(dm, t)
                if isinstance(r, tuple):
                    if not r[-1]:
                        continue
                    idx = [x for x in r if isinstance(x, unreal.IntVector)] or [r[0]]
                    tri = idx[0]
                else:
                    tri = r
                a_, b_, c_ = vp.get(tri.x), vp.get(tri.y), vp.get(tri.z)
                if a_ is None or b_ is None or c_ is None:
                    continue
                f.write(",".join("%.2f" % x for v in (a_, b_, c_) for x in (v.x, v.y, v.z)) + "\n")
                n += 1
        log.append("%s: %d tris" % (key, n))
    except Exception:
        log.append("%s EXC %s" % (key, traceback.format_exc()[-500:]))
with open(os.path.join(DUMP, "gtris.txt"), "w") as f:
    f.write("\n".join(log))
