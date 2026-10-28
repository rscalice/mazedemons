import unreal, traceback, json
# V1 WP4 assets (run with the editor closed as a commandlet; idempotent):
#   1. DT_IntelligenceTiers (/Game/AI): FMDIntelligenceTier rows T0..T4 with the v1-design 3.2 numbers
#   2. MDAN_Tell notify at the wind-up start of AM_Ghoul_Swipe and AM_Ghoul_Lunge (the notify fires the tell cue on every machine)
#   3. DA_Archetype_Ghoul: TellSound (a short growl from the Fab MonsterRoarsAndGrowls pack, referenced only, never edited)
#      at low volume, and the off-screen ThreatSting
# ghoulassets.py applies the same notify and archetype fields when it rebuilds the montages. Report: Saved/ClaudeDump/gtell.txt
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

ANIM_DIR = "/Game/Animations/Enemies/Ghoul"
TELL_TIME = 0.02  # a notify exactly at 0 can be skipped on the first montage tick

def add_tell(path):
    m = unreal.load_asset(path)
    if m is None:
        P("  missing %s" % path); return
    for nm in ("MD Tell", "MDAN_Tell"):
        try: lib.remove_animation_notify_events_by_name(m, nm)
        except Exception: pass
    names = [str(x) for x in lib.get_animation_notify_track_names(m)]
    if "Tell" not in names:
        lib.add_animation_notify_track(m, "Tell", unreal.LinearColor(1.0, 0.85, 0.2, 1.0))
    cls = unreal.load_class(None, "/Script/MazeDemons.MDAN_Tell")
    lib.add_animation_notify_event(m, "Tell", TELL_TIME, cls)
    m.modify(); EAL.save_asset(path, only_if_is_dirty=False)
    m = unreal.load_asset(path)
    P("  %s: notifies %s" % (path.split("/")[-1], [str(x) for x in lib.get_animation_notify_event_names(m)]))

def s_notify():
    for p in (ANIM_DIR + "/AM_Ghoul_Swipe", ANIM_DIR + "/AM_Ghoul_Lunge"):
        add_tell(p)
sec("MDAN_Tell", s_notify)

TELL_SOUND = "/Game/Fab/MonsterRoarsAndGrowls/cues/11_Menacing_Growl_Cue"
STING_SOUND = "/Game/Fab/MonsterRoarsAndGrowls/cues/10_Attack_Roar_Cue"

def s_archetype():
    da = unreal.load_asset("/Game/Data/Enemies/DA_Archetype_Ghoul")
    for prop, path in (("tell_sound", TELL_SOUND), ("threat_sting", STING_SOUND)):
        asset = unreal.load_asset(path)
        P("  %s -> %s (%s)" % (prop, path, asset.get_name() if asset else "NOT FOUND"))
        if asset:
            da.set_editor_property(prop, asset)
    da.set_editor_property("tell_volume", 0.3)
    da.set_editor_property("threat_sting_volume", 0.7)
    da.modify(); EAL.save_asset("/Game/Data/Enemies/DA_Archetype_Ghoul", only_if_is_dirty=False)
    da = unreal.load_asset("/Game/Data/Enemies/DA_Archetype_Ghoul")
    P("  now: tell %s vol %.2f, sting %s vol %.2f" % (da.get_editor_property("tell_sound"), da.get_editor_property("tell_volume"),
                                                    da.get_editor_property("threat_sting"), da.get_editor_property("threat_sting_volume")))
sec("Archetype", s_archetype)

open(out, "w").write("\n".join(L))
