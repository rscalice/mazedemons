# R3: creates DT_AmmoDrops (idempotent) and flips the Ammo service on BP_MDStation's defaults.
# Run via Scripts\RunPy.ps1 Scripts\Python\dropsr3.py (editor closed). Log: Saved\ClaudeDump\dropsr3.log
#   DT_AmmoDrops rows are keyed by the archetype tag name; columns AmmoRow, Quantity, Chance (ranged-design section 4).
import json
import unreal

ITEMS = "/Game/Blueprints/Items"
TABLE = ITEMS + "/DT_AmmoDrops"

ROWS = {
    "Enemy.BoneArcher": {"AmmoRow": "AmmoHeavy", "Quantity": 6, "Chance": 0.70},
    "Enemy.Cultist": {"AmmoRow": "AmmoLight", "Quantity": 8, "Chance": 0.50},
    "Enemy.Ghoul": {"AmmoRow": "AmmoLight", "Quantity": 4, "Chance": 0.15},
    "Enemy.GraveBrute": {"AmmoRow": "AmmoHeavy", "Quantity": 8, "Chance": 0.40},
}


def log(s):
    unreal.log("[dropsr3] " + str(s))


def make_table():
    if unreal.EditorAssetLibrary.does_asset_exist(TABLE):
        return unreal.load_asset(TABLE)
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", unreal.MDAmmoDropRow.static_struct())
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    return tools.create_asset("DT_AmmoDrops", ITEMS, unreal.DataTable, factory)


def fill_table(table):
    rows = []
    for name, cols in ROWS.items():
        entry = {"Name": name}
        entry.update(cols)
        rows.append(entry)
    errors = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows))
    log("fill result: %s" % (errors,))
    unreal.EditorAssetLibrary.save_loaded_asset(table)


def set_station_defaults():
    path = "/Game/Blueprints/Stations/BP_MDStation"
    bp = unreal.load_asset(path)
    if not bp:
        log("BP_MDStation not found")
        return
    cdo = unreal.get_default_object(bp.generated_class())
    before = cdo.get_editor_property("services")
    cdo.set_editor_property("services", 15)
    unreal.EditorAssetLibrary.save_loaded_asset(bp)
    log("BP_MDStation services %s -> %s" % (before, cdo.get_editor_property("services")))


def main():
    table = make_table()
    if not table:
        log("could not create DT_AmmoDrops")
        return
    fill_table(table)
    table = unreal.load_asset(TABLE)
    for name in table.get_row_names():
        log("row %s" % name)
    set_station_defaults()


try:
    main()
except Exception as exc:  # noqa
    import traceback
    log("FAILED: %s" % exc)
    log(traceback.format_exc())
