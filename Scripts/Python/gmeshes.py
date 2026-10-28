# Read-only (guns v2): real geometry of the three gun meshes + the visible Manny sockets. Report: Saved\ClaudeDump\gmeshes.txt
import os
import traceback
import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump", "gmeshes.txt")
L = []


def P(s=""):
    L.append(str(s))


MESHES = {
    "revolver(static)": ("/Game/Fab/Free_Revolver___Handcanon/tesseract_revolver/StaticMeshes/tesseract_revolver", False),
    "ion_rifle(skeletal)": ("/Game/Fab/ION_RIFLE_-__EC-11_/ion_rifle_ec_11/SkeletalMeshes/ion_rifle_ec_11", True),
    "beam_rifle(static)": ("/Game/Fab/Beam_Rifle/model/StaticMeshes/model", False),
    "crossbow(static)": ("/Game/Animations/WeaponStances/Crossbow/Mesh/medieval_crossbow_01/StaticMeshes/medieval_crossbow_01", False),
}


def verts_static(sm):
    d = sm.get_static_mesh_description(0)
    n = d.get_vertex_count()
    out = []
    for i in range(n):
        v = d.get_vertex_position(unreal.VertexID(i))
        out.append((v.x, v.y, v.z))
    return out


def verts_gs(asset, skeletal):
    dm = unreal.DynamicMesh()
    if skeletal:
        opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
        r = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(asset, dm, opts, unreal.GeometryScriptMeshReadLOD(unreal.GeometryScriptLODType.MAX_AVAILABLE, 0))
    else:
        opts = unreal.GeometryScriptCopyMeshFromAssetOptions()
        r = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(asset, dm, opts, unreal.GeometryScriptMeshReadLOD(unreal.GeometryScriptLODType.MAX_AVAILABLE, 0))
    P("  gs result: " + str(r)[:120])
    res = unreal.GeometryScript_MeshQueries.get_all_vertex_positions(dm)
    pos = res[0] if isinstance(res, tuple) else res
    return [(v.x, v.y, v.z) for v in pos]


def profile(vs, axis_names=("X", "Y", "Z")):
    mn = [min(v[i] for v in vs) for i in range(3)]
    mx = [max(v[i] for v in vs) for i in range(3)]
    P("  verts=%d  min=(%.2f, %.2f, %.2f) max=(%.2f, %.2f, %.2f) size=(%.2f, %.2f, %.2f)" % (len(vs), mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]))
    la = max(range(3), key=lambda i: mx[i] - mn[i])
    P("  longest axis = %s" % axis_names[la])
    bins = 20
    span = (mx[la] - mn[la]) / bins
    others = [i for i in range(3) if i != la]
    P("  profile along %s (bin start | count | %s range | %s range):" % (axis_names[la], axis_names[others[0]], axis_names[others[1]]))
    for b in range(bins):
        lo = mn[la] + b * span
        hi = lo + span
        sel = [v for v in vs if (lo <= v[la] < hi) or (b == bins - 1 and v[la] == hi)]
        if not sel:
            P("   %8.2f | 0" % lo)
            continue
        a = [v[others[0]] for v in sel]
        c = [v[others[1]] for v in sel]
        P("   %8.2f | %5d | %7.2f..%7.2f | %7.2f..%7.2f" % (lo, len(sel), min(a), max(a), min(c), max(c)))


for key, (path, skeletal) in MESHES.items():
    P("######## " + key + "  " + path)
    try:
        a = unreal.load_asset(path)
        if a is None:
            P("  MISSING")
            continue
        try:
            bb = a.get_bounds() if hasattr(a, "get_bounds") else None
            if bb:
                P("  bounds origin=%s extent=%s" % (bb.origin, bb.box_extent))
        except Exception:
            pass
        for sname in ("Grip_R", "Grip_L", "Muzzle"):
            try:
                s = a.find_socket(sname) if hasattr(a, "find_socket") else None
                P("  socket %s: %s" % (sname, s))
            except Exception:
                pass
        try:
            vs = verts_gs(a, skeletal) if skeletal else verts_static(a)
        except Exception:
            P("  vertex read failed, trying gs: " + traceback.format_exc()[-300:])
            vs = verts_gs(a, skeletal)
        profile(vs)
    except Exception:
        P("EXC " + traceback.format_exc())

P("######## Manny meshes")
try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    cands = [a for a in reg.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "SkeletalMesh")) if "Manny_Simple" in str(a.asset_name) or "UEFN_Mannequin" in str(a.asset_name)]
    for c in cands:
        P("  " + str(c.package_name))
        m = c.get_asset()
        try:
            P("    sockets: " + str([str(s.get_editor_property("socket_name")) for s in m.get_editor_property("mesh_only_socket_list")]))
        except Exception:
            P("    socket list err: " + traceback.format_exc()[-200:])
        try:
            sk = m.get_editor_property("skeleton")
            P("    skeleton sockets: " + str([str(s.get_editor_property("socket_name")) + "@" + str(s.get_editor_property("bone_name")) + " loc=" + str(s.get_editor_property("relative_location")) + " rot=" + str(s.get_editor_property("relative_rotation")) for s in sk.get_editor_property("sockets")]))
        except Exception:
            P("    skel sockets err: " + traceback.format_exc()[-200:])
except Exception:
    P("EXC manny " + traceback.format_exc())
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(L) + "\n")
