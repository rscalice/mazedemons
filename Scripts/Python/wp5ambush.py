import unreal
# V1 WP5: add the Zone 3 ambush group to L_MazeDemonsPrototype (commandlet, editor closed; idempotent).
# 3 ghouls (SpawnGroupId Zone3Ambush) + an AMDAmbushMarker (tag Spawn.Ambush) with a wall-to-wall trigger, 21 m from the station.
# Report: Saved/ClaudeDump/wp5ambush.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/wp5ambush.txt"
L = []
def P(s): L.append(str(s)); print(s)

unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/L_MazeDemonsPrototype")
for a in unreal.EditorLevelLibrary.get_all_level_actors():
    n = a.get_actor_label()
    if n.startswith("BP_Enemy_Ghoul_Z3_Ambush") or n == "AmbushMarker_Z3":
        unreal.EditorLevelLibrary.destroy_actor(a)
        P("removed old " + n)

ghoul_cls = unreal.EditorAssetLibrary.load_blueprint_class("/Game/Blueprints/Enemies/BP_Enemy_Ghoul")
spots = [(10200.0, 0.0), (10200.0, 320.0), (10200.0, -320.0)]
ghouls = []
for i, (x, y) in enumerate(spots):
    g = unreal.EditorLevelLibrary.spawn_actor_from_class(ghoul_cls, unreal.Vector(x, y, 127.0), unreal.Rotator(0.0, 0.0, 180.0))
    g.set_actor_label("BP_Enemy_Ghoul_Z3_Ambush_%02d" % (i + 1))
    g.set_editor_property("spawn_group_id", "Zone3Ambush")
    ghouls.append(g)
    P("ghoul %s at %s" % (g.get_actor_label(), g.get_actor_location()))

marker = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.MDAmbushMarker, unreal.Vector(9300.0, 0.0, 127.0), unreal.Rotator(0.0, 0.0, 0.0))
marker.set_actor_label("AmbushMarker_Z3")
marker.set_editor_property("ambushers", ghouls)
marker.set_editor_property("ignore_tier", True)  # demo: ambush at every tier (below T2 the tier gate would otherwise skip it)
trig = marker.get_editor_property("trigger")
trig.set_box_extent(unreal.Vector(100.0, 800.0, 150.0))
P("marker at %s, ambushers %d, tags %s" % (marker.get_actor_location(), len(marker.get_editor_property("ambushers")), marker.tags))

unreal.EditorLoadingAndSavingUtils.save_current_level()
open(out, "w").write("\n".join(L))
