# R0: creates the ranged weapon data (idempotent). Run via Scripts\RunPy.ps1 Scripts\Python\mkranged.py (editor closed).
#   1. E_ItemTypes += Ranged (ordinal 12) and E_ItemName += the new items (Blueprint enums, appended only)
#   2. DA_Ranged_* data assets under /Game/Data/Ranged (UMDRangedWeaponData)
#   3. DT_Items rows (guns: type Ranged, ammo: type Material stack 120 / 60), DT_ItemStats rows (RangedData), DT_ItemDisplay texts
#   4. DT_ElementResist skeleton (one row, all multipliers 1)
# Weapon meshes are PLACEHOLDERS referenced from Robert's content (never saved or modified): dagger meshes for the pistols and the
# crossbow mesh for the repeater, with per-weapon grip offsets in the data assets. Swap the StaticMesh / SkeletalMesh and the
# offsets in DA_Ranged_* when the real gun meshes (Fab) are downloaded.
import os
import traceback
import unreal

ITEMS = "/Game/Blueprints/Items/"
DATA = "/Game/Data/Ranged"
lib = unreal.MD_ItemAssetLibrary
lines = []


def log(s):
    lines.append(str(s))
    unreal.log("[mkranged] " + str(s))


def vec(x=0.0, y=0.0, z=0.0):
    return unreal.Vector(x, y, z)


def xform(loc=(0, 0, 0), rot=(0, 0, 0)):
    return unreal.Transform(location=vec(*loc), rotation=unreal.Rotator(rot[0], rot[1], rot[2]), scale=vec(1, 1, 1))


MESH_DAGGER1 = "/Game/Fab/Weapon_Pack/Mesh/Weapons/Weapons_Kit/SM_Dagger_1"
MESH_DAGGER2 = "/Game/Fab/Weapon_Pack/Mesh/Weapons/Weapons_Kit/SM_Dagger_2"
MESH_CROSSBOW = "/Game/Animations/WeaponStances/Crossbow/Mesh/medieval_crossbow_01/StaticMeshes/medieval_crossbow_01"

# name -> (display, description, mesh, data builder)
GUNS = ["HollowPistol", "CatacombRepeater", "DuskrayPistol"]
AMMO = {"AmmoLight": (120, "Light Rounds", "Small-caliber rounds for pistols."),
        "AmmoHeavy": (60, "Heavy Rounds", "Heavy rounds for repeaters and bolters.")}
TEXTS = {
    "HollowPistol": ("Hollow Pistol", "A fast, accurate sidearm. Good for openers and finishing blows."),
    "CatacombRepeater": ("Catacomb Repeater", "A rapid-fire two-handed repeater that holds the line."),
    "DuskrayPistol": ("Duskray Pistol", "A laser pistol. Needs no ammo but overheats; chills its targets."),
}


def row_call(table, row, template, overrides):
    res = lib.set_row_from_template(table, row, template, overrides)
    if not isinstance(res, tuple):
        return bool(res), []
    return bool(res[0]), list(res[-1]) if isinstance(res[-1], (list, tuple)) else [str(res[-1])]


def mk_asset(name, cls):
    path = "%s/%s" % (DATA, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path), False
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    return tools.create_asset(name, DATA, cls, unreal.DataAssetFactory()), True


def set_common(da, stance, grip, mesh_path, right_offset, left_offset):
    da.set_editor_property("stance", stance)
    da.set_editor_property("grip_type", grip)
    da.set_editor_property("static_mesh", unreal.load_asset(mesh_path))
    da.set_editor_property("hand_offset", right_offset)
    da.set_editor_property("has_right_grip_offset", True)
    if left_offset is not None:
        da.set_editor_property("support_hand_offset", left_offset)
        da.set_editor_property("has_left_grip_offset", True)


def build_data():
    out = {}
    # ---- Hollow Pistol: semi-auto, light rounds, one-hand feel ----
    da, _ = mk_asset("DA_Ranged_HollowPistol", unreal.MDRangedWeaponData)
    da.set_editor_property("fire_mode", unreal.MDFireMode.SEMI)
    da.set_editor_property("hit_model", unreal.MDRangedHitModel.PROJECTILE)
    da.set_editor_property("rounds_per_second", 3.0)
    da.set_editor_property("damage_per_shot", 45.0)
    da.set_editor_property("poise_damage", 8.0)
    da.set_editor_property("mag_size", 8)
    da.set_editor_property("ammo_row", "AmmoLight")
    da.set_editor_property("reload_seconds", 1.2)
    da.set_editor_property("spread_hip", 3.0)
    da.set_editor_property("spread_aimed", 1.0)
    da.set_editor_property("spread_moving", 1.4)
    da.set_editor_property("spread_bloom", 0.6)
    proj = da.get_editor_property("projectile")
    proj.set_editor_property("speed", 20000.0)
    da.set_editor_property("projectile", proj)
    set_common(da, unreal.MDWeaponStance.PISTOL, unreal.MDGripType.ONE_HANDED_SUPPORT_WHEN_AIMED, MESH_DAGGER1,
               xform((0, 0, 0), (0, 0, 90)), xform((12, 0, 0)))
    out["HollowPistol"] = da

    # ---- Catacomb Repeater: auto 8/s, heavy rounds, two-hand stance ----
    da, _ = mk_asset("DA_Ranged_CatacombRepeater", unreal.MDRangedWeaponData)
    da.set_editor_property("fire_mode", unreal.MDFireMode.AUTO)
    da.set_editor_property("hit_model", unreal.MDRangedHitModel.PROJECTILE)
    da.set_editor_property("rounds_per_second", 8.0)
    da.set_editor_property("damage_per_shot", 22.0)
    da.set_editor_property("poise_damage", 4.0)
    da.set_editor_property("mag_size", 24)
    da.set_editor_property("ammo_row", "AmmoHeavy")
    da.set_editor_property("reload_seconds", 1.9)
    da.set_editor_property("spread_hip", 4.5)
    da.set_editor_property("spread_aimed", 1.6)
    da.set_editor_property("spread_moving", 2.2)
    da.set_editor_property("spread_bloom", 0.35)
    da.set_editor_property("spread_bloom_decay", 3.0)
    proj = da.get_editor_property("projectile")
    proj.set_editor_property("speed", 22000.0)
    da.set_editor_property("projectile", proj)
    set_common(da, unreal.MDWeaponStance.RIFLE, unreal.MDGripType.TWO_HANDED, MESH_CROSSBOW,
               xform((0, 0, 0), (0, 0, 90)), xform((28, 0, 4)))
    out["CatacombRepeater"] = da

    # ---- Duskray Pistol: hold-to-fire beam, heat instead of ammo, Frost ----
    da, _ = mk_asset("DA_Ranged_DuskrayPistol", unreal.MDRangedWeaponData)
    da.set_editor_property("fire_mode", unreal.MDFireMode.BEAM)
    da.set_editor_property("hit_model", unreal.MDRangedHitModel.BEAM)
    da.set_editor_property("mag_size", 0)
    da.set_editor_property("ammo_row", "")
    beam = da.get_editor_property("beam")
    beam.set_editor_property("tick_hz", 10.0)
    beam.set_editor_property("damage_per_second", 180.0)
    beam.set_editor_property("range", 1800.0)
    beam.set_editor_property("heat_per_second", 30.0)
    beam.set_editor_property("vent_seconds", 8.0)
    beam.set_editor_property("overheat_lock", 2.5)
    da.set_editor_property("beam", beam)
    try:
        tag = unreal.GameplayTag()
        tag.set_editor_property("tag_name", "Damage.Type.Frost")
        da.set_editor_property("default_damage_type", tag)
    except Exception as e:  # tag API differs between versions; R5 sets it again
        log("WARN: could not set Damage.Type.Frost on the Duskray (%s)" % e)
    set_common(da, unreal.MDWeaponStance.PISTOL, unreal.MDGripType.ONE_HANDED_SUPPORT_WHEN_AIMED, MESH_DAGGER2,
               xform((0, 0, 0), (0, 0, 90)), xform((12, 0, 0)))
    out["DuskrayPistol"] = da

    for name, asset in out.items():
        errs = asset.get_validation_errors() if hasattr(asset, "get_validation_errors") else []
        log("%s: %s" % (name, "valid" if not errs else "ERRORS " + "; ".join(errs)))
        unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    return out


def run():
    # 1. enums
    ordinal = lib.append_enumerator(ITEMS + "E_ItemTypes", "Ranged")
    log("E_ItemTypes.Ranged ordinal = %s" % ordinal)
    if ordinal != 12:
        log("FAIL: Ranged must be ordinal 12 (append-only), got %s" % ordinal)
        return False
    for n in GUNS + list(AMMO.keys()):
        o = lib.append_enumerator(ITEMS + "E_ItemName", n)
        log("E_ItemName.%s ordinal = %s" % (n, o))
        if o < 0:
            return False

    # 2. data assets
    data = build_data()
    for n, a in data.items():
        if a.get_editor_property("static_mesh") is None:
            log("FAIL: %s mesh did not load" % n)
            return False

    # 3. table rows
    for n in GUNS:
        display, desc = TEXTS[n]
        ov = {"Name": n, "ItemType": "Ranged", "StackSize": "1", "MaxStack": "1",
              "StaticMesh": data[n].get_editor_property("static_mesh").get_path_name()}
        ok, l = row_call(ITEMS + "DT_Items", n, "DefaultItem", ov)
        for s in l:
            log("  DT_Items " + s)
        if not ok:
            return False
    for n, (stack, display, desc) in AMMO.items():
        ov = {"Name": n, "ItemType": "Material", "StackSize": str(stack), "MaxStack": str(stack)}
        ok, l = row_call(ITEMS + "DT_Items", n, "UpgradeShard", ov)
        for s in l:
            log("  DT_Items " + s)
        if not ok:
            return False

    stats = {"HollowPistol": 120, "CatacombRepeater": 200, "DuskrayPistol": 160}
    for n in GUNS:
        ov = {"ItemValue": str(stats[n]), "MaxDurability": "100", "bUpgradeable": "True",
              "RangedData": "%s/DA_Ranged_%s.DA_Ranged_%s" % (DATA, n, n)}
        ok, l = row_call(ITEMS + "DT_ItemStats", n, "", ov)
        for s in l:
            log("  DT_ItemStats " + s)
        if not ok:
            return False
    for n, (stack, display, desc) in AMMO.items():
        ok, l = row_call(ITEMS + "DT_ItemStats", n, "", {"ItemValue": "4" if n == "AmmoLight" else "8"})
        for s in l:
            log("  DT_ItemStats " + s)
        if not ok:
            return False

    texts = {n: TEXTS[n] for n in GUNS}
    texts.update({n: (v[1], v[2]) for n, v in AMMO.items()})
    for n, (display, desc) in texts.items():
        ns = 'NSLOCTEXT("DT_ItemDisplay", "%s_%s", "%s")'
        ov = {"DisplayName": ns % (n, "DisplayName", display), "Description": ns % (n, "Description", desc)}
        ok, l = row_call(ITEMS + "DT_ItemDisplay", n, "", ov)
        for s in l:
            log("  DT_ItemDisplay " + s)
        if not ok:
            return False

    # 4. DT_ElementResist skeleton
    path = ITEMS + "DT_ElementResist"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        factory = unreal.DataTableFactory()
        factory.set_editor_property("struct", unreal.MDElementResistRow.static_struct())
        unreal.AssetToolsHelpers.get_asset_tools().create_asset("DT_ElementResist", ITEMS.rstrip("/"), unreal.DataTable, factory)
        log("created DT_ElementResist")
    ok, l = row_call(path, "Enemy.Ghoul", "", {})
    for s in l:
        log("  DT_ElementResist " + s)
    return ok


try:
    ok = run()
    log("RESULT: " + ("OK" if ok else "FAILED"))
except Exception:
    log("EXCEPTION:\n" + traceback.format_exc())
path = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump", "mkranged.txt")
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
