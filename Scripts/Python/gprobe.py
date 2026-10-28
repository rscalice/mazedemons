# Read-only probe (guns v2): which Python APIs can read mesh vertices. Report: Saved\ClaudeDump\gprobe.txt
import os
import traceback
import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump", "gprobe.txt")
L = []


def P(s=""):
    L.append(str(s))


REVOLVER = "/Game/Fab/Free_Revolver___Handcanon/tesseract_revolver/StaticMeshes/tesseract_revolver"
try:
    sm = unreal.load_asset(REVOLVER)
    P("revolver: " + str(sm))
    P("dir(sm) mesh-ish: " + str([d for d in dir(sm) if "desc" in d.lower() or "vert" in d.lower() or "bound" in d.lower() or "socket" in d.lower()]))
    bb = sm.get_bounding_box()
    P("bbox min=%s max=%s" % (bb.min, bb.max))
    P("sockets=" + str([str(s.socket_name) for s in sm.sockets]))
except Exception:
    P("EXC1 " + traceback.format_exc())
try:
    P("StaticMeshEditorSubsystem: " + str([d for d in dir(unreal.StaticMeshEditorSubsystem) if not d.startswith("_")]))
except Exception:
    P("EXC2 " + traceback.format_exc())
try:
    P("GeometryScript: " + str([d for d in dir(unreal) if "GeometryScript" in d][:40]))
except Exception:
    P("EXC3 " + traceback.format_exc())
try:
    P("StaticMeshDescription: " + str([d for d in dir(unreal.StaticMeshDescription) if not d.startswith("_")][:80]))
except Exception:
    P("EXC4 " + traceback.format_exc())
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(L) + "\n")
