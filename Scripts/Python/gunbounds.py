"""Diagnostic (read only): bounds of the three gun meshes and the current ranged data values. Report: Saved/ClaudeDump/gunbounds.txt"""
import os
import traceback
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Saved", "ClaudeDump", "gunbounds.txt")
L = []


def P(s=""):
    L.append(str(s))


MESHES = {
    "revolver": "/Game/Fab/Free_Revolver___Handcanon/tesseract_revolver/StaticMeshes/tesseract_revolver",
    "ion_rifle": "/Game/Fab/ION_RIFLE_-__EC-11_/ion_rifle_ec_11/SkeletalMeshes/ion_rifle_ec_11",
    "beam_rifle": "/Game/Fab/Beam_Rifle/model/StaticMeshes/model",
}
try:
    for key, path in MESHES.items():
        mesh = unreal.EditorAssetLibrary.load_asset(path)
        if not mesh:
            P("missing " + path)
            continue
        b = mesh.get_bounds()
        o, e = b.origin, b.box_extent
        P("%s origin=(%.2f,%.2f,%.2f) extent=(%.2f,%.2f,%.2f) size=(%.1f,%.1f,%.1f)" % (
            key, o.x, o.y, o.z, e.x, e.y, e.z, e.x * 2, e.y * 2, e.z * 2))

    for name in ("DA_Ranged_HollowPistol", "DA_Ranged_CatacombRepeater", "DA_Ranged_DuskrayPistol"):
        da = unreal.EditorAssetLibrary.load_asset("/Game/Data/Ranged/" + name)
        if not da:
            P("missing " + name)
            continue
        P("---- " + name)
        for p in ["fire_mode", "rounds_per_second", "damage_per_shot", "poise_damage", "headshot_mult", "spread_hip",
                  "spread_aimed", "spread_moving", "spread_bloom", "spread_bloom_decay", "recoil_pitch", "recoil_yaw",
                  "recoil_recovery", "mag_size", "ammo_row", "projectile", "beam", "default_damage_type", "hand_offset",
                  "support_hand_offset", "muzzle_local", "has_muzzle_local", "reaction_cap", "falloff_start", "tracer_material"]:
            try:
                P("  %s = %s" % (p, da.get_editor_property(p)))
            except Exception as ex:  # noqa
                P("  %s = <err %s>" % (p, ex))
except Exception:
    P(traceback.format_exc())

with open(OUT, "w") as f:
    f.write("\n".join(L))
