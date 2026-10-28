"""R1 fix: bullets use the C1 impact-set family instead of the pink laser hit.

Creates /Game/Data/Combat/DA_Impact_Bullet (UMDImpactSet): Default = a small spark puff for walls and floors (creatures get C1's blood
from DA_Impact_Blade through the feedback component, the projectile spawns nothing for them). Clears ImpactFX on the two projectile guns
(the FreeStylizedLaserBeamVFX hit stays for beams, R5). Fab assets are only referenced. Idempotent.

Run with the editor closed:  Scripts\\RunPy.ps1 Scripts/Python/impactbullet.py   (report: Saved/ClaudeDump/impactbullet.txt)
"""
import os
import traceback
import unreal

SPARK = "/Game/Fab/Legendary_Forge/Fx/Sparks/NS_sparks"
SPARK_SCALE = 0.35
DA_DIR = "/Game/Data/Combat"
DA_NAME = "DA_Impact_Bullet"
GUNS = ["/Game/Data/Ranged/DA_Ranged_HollowPistol", "/Game/Data/Ranged/DA_Ranged_CatacombRepeater"]

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Saved", "ClaudeDump", "impactbullet.txt")
L = []


def P(s=""):
    L.append(str(s))


try:
    path = DA_DIR + "/" + DA_NAME
    da = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if da is None:
        da = unreal.AssetToolsHelpers.get_asset_tools().create_asset(DA_NAME, DA_DIR, unreal.MDImpactSet, unreal.DataAssetFactory())
        P("created " + path)
    spark = unreal.load_asset(SPARK)
    if not spark:
        P("MISSING " + SPARK)
    entry = unreal.MDImpactEntry()
    entry.set_editor_property("vfx", spark)
    entry.set_editor_property("vfx_scale", SPARK_SCALE)
    da.set_editor_property("default", entry)
    da.set_editor_property("entries", [])
    da.set_editor_property("max_decals_per_zone", 20)
    da.modify()
    P("%s saved=%s" % (DA_NAME, unreal.EditorAssetLibrary.save_asset(path)))

    for gun in GUNS:
        g = unreal.EditorAssetLibrary.load_asset(gun)
        g.set_editor_property("impact_fx", None)
        g.modify()
        P("%s impact_fx cleared, saved=%s" % (gun, unreal.EditorAssetLibrary.save_asset(gun, False)))

    P("")
    P("---- re-read")
    da = unreal.EditorAssetLibrary.load_asset(path)
    d = da.get_editor_property("default")
    P("%s default vfx=%s scale=%s entries=%d" % (DA_NAME, d.get_editor_property("vfx"), d.get_editor_property("vfx_scale"), len(da.get_editor_property("entries"))))
    for gun in GUNS:
        g = unreal.EditorAssetLibrary.load_asset(gun)
        P("%s impact_fx=%s muzzle_flash_fx=%s" % (gun, g.get_editor_property("impact_fx"), g.get_editor_property("muzzle_flash_fx")))
except Exception:
    P(traceback.format_exc())

with open(OUT, "w") as f:
    f.write("\n".join(L))
