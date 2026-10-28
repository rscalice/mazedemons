# Read-only (guns v2): dumps the vertex positions of the gun meshes to CSV (Saved\ClaudeDump\gverts_<name>.csv) for offline analysis.
import os
import traceback
import unreal

DUMP = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump")
os.makedirs(DUMP, exist_ok=True)
MESHES = {
    "revolver": ("/Game/Fab/Free_Revolver___Handcanon/tesseract_revolver/StaticMeshes/tesseract_revolver", False),
    "ion": ("/Game/Fab/ION_RIFLE_-__EC-11_/ion_rifle_ec_11/SkeletalMeshes/ion_rifle_ec_11", True),
    "beam": ("/Game/Fab/Beam_Rifle/model/StaticMeshes/model", False),
}
log = []
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
        mq = unreal.GeometryScript_MeshQueries
        nid = mq.get_num_vertex_i_ds(dm)
        nid = nid[0] if isinstance(nid, tuple) else nid
        pos = []
        for i in range(int(nid)):
            r = mq.get_vertex_position(dm, i)
            if isinstance(r, tuple):
                vec, ok = r[0], r[1]
                if not ok:
                    continue
            else:
                vec = r
            pos.append(vec)
        with open(os.path.join(DUMP, "gverts_%s.csv" % key), "w") as f:
            for v in pos:
                f.write("%.3f,%.3f,%.3f\n" % (v.x, v.y, v.z))
        log.append("%s: %d verts" % (key, len(pos)))
    except Exception:
        log.append("%s EXC %s" % (key, traceback.format_exc()[-400:]))
with open(os.path.join(DUMP, "gverts.txt"), "w") as f:
    f.write("\n".join(log))

