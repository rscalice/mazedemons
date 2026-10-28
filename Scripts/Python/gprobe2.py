# Read-only probe (guns v2): vertex access for static + skeletal meshes, Manny sockets. Report: Saved\ClaudeDump\gprobe2.txt
import os
import traceback
import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump", "gprobe2.txt")
L = []


def P(s=""):
    L.append(str(s))


REVOLVER = "/Game/Fab/Free_Revolver___Handcanon/tesseract_revolver/StaticMeshes/tesseract_revolver"
sm = unreal.load_asset(REVOLVER)
try:
    d = sm.get_static_mesh_description(0)
    P("desc " + str(d) + " verts=" + str(d.get_vertex_count()))
    P("desc methods: " + str([m for m in dir(d) if "position" in m.lower()]))
    P("sm socket methods: " + str(sm.get_editor_property("sockets")))
except Exception:
    P("EXC1 " + traceback.format_exc())
try:
    dm = unreal.DynamicMesh()
    opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
    mesh, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(sm, dm, opts, unreal.GeometryScriptMeshReadLOD(unreal.GeometryScriptLODType.MAX_AVAILABLE, 0))
    P("gs outcome " + str(outcome))
    P("gs query methods: " + str([m for m in dir(unreal.GeometryScript_MeshQueries) if "vert" in m.lower()]))
except Exception:
    P("EXC2 " + traceback.format_exc())
try:
    P("skel asset utils: " + str([m for m in dir(unreal.GeometryScript_AssetUtils) if not m.startswith("_")]))
except Exception:
    P("EXC3 " + traceback.format_exc())
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(L) + "\n")
