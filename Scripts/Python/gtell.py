import unreal, traceback, json
# V1 WP4 assets (run with the editor closed as a commandlet; idempotent):
#   1. DT_IntelligenceTiers (/Game/AI): FMDIntelligenceTier rows T0..T4 with the v1-design 3.2 numbers
#   (The MDAN_Tell notifies and the archetype tell / sting sounds are set by ghoulcombat.py.)
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/gtell.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
def sec(name, fn):
    try:
        P("== " + name); return fn()
    except Exception:
        P("EXC in %s: %s" % (name, traceback.format_exc())); return None

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.AnimationLibrary

TIER_DIR = "/Game/AI"
TIER_TABLE = TIER_DIR + "/DT_IntelligenceTiers"
# v1-design 3.2: reaction, tell multiplier (floor 0.35 s in code), tokens/player, sight radius (cm), FOV, aggression, noise, share delay
TIERS = [
    ("T0", 0.9, 1.0, 1, 1500.0, 110.0, 0.6, 0.05, -1.0),
    ("T1", 0.7, 0.9, 2, 2000.0, 120.0, 0.8, 0.08, 2.0),
    ("T2", 0.5, 0.8, 2, 2500.0, 140.0, 1.0, 0.12, 1.0),
    ("T3", 0.4, 0.7, 3, 3000.0, 150.0, 1.1, 0.15, 0.5),
    ("T4", 0.3, 0.6, 3, 3000.0, 160.0, 1.2, 0.15, 0.25),
]

def s_table():
    if not EAL.does_asset_exist(TIER_TABLE):
        EAL.make_directory(TIER_DIR)
        f = unreal.DataTableFactory()
        f.set_editor_property("struct", unreal.MDIntelligenceTier.static_struct())
        AT.create_asset("DT_IntelligenceTiers", TIER_DIR, unreal.DataTable, f)
        P("  created DT_IntelligenceTiers")
    table = unreal.load_asset(TIER_TABLE)
    rows = []
    for name, react, tell, tokens, sight, fov, aggr, noise, share in TIERS:
        rows.append({"Name": name, "ReactionTime": react, "TellTimeMultiplier": tell, "MaxTokensPerPlayer": tokens,
                     "SightRadius": sight, "SightFOV": fov, "Aggression": aggr, "NoiseAmplitude": noise, "ShareDelay": share})
    ok = unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows), unreal.MDIntelligenceTier.static_struct())
    P("  fill from json -> %s" % ok)
    table.modify(); EAL.save_asset(TIER_TABLE, only_if_is_dirty=False)
    table = unreal.load_asset(TIER_TABLE)
    names = [str(n) for n in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
    P("  rows: %s" % names)
    for col in ("ReactionTime", "TellTimeMultiplier", "MaxTokensPerPlayer", "ShareDelay"):
        P("  %s: %s" % (col, [str(x) for x in unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, col)]))
sec("DT_IntelligenceTiers", s_table)

open(out, "w").write("\n".join(L))
