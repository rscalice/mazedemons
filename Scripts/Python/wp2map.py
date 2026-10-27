import unreal
# V1 WP2: Zone 2 of L_MazeDemonsPrototype gets a 5-ghoul pack: the 2 existing BP_Enemy_Ghoul_Z2_* plus 3 new ones, all in spawn
# group "Zone2Pack", and one rally point (AMDRallyPoint) at the east end of the zone for the T2 retreat. Idempotent.
# Commandlet, editor closed. Report: Saved/ClaudeDump/wp2map.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/wp2map.txt"
L = []
def P(s): L.append(str(s)); print(s)

MAP = "/Game/Maps/L_MazeDemonsPrototype"
GROUP = "Zone2Pack"
world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
P("loaded: %s" % (world.get_name() if world else None))

actors = list(unreal.EditorLevelLibrary.get_all_level_actors())
ghouls = [a for a in actors if a.get_actor_label().startswith("BP_Enemy_Ghoul_Z2")]
P("existing zone 2 ghouls: %s" % [g.get_actor_label() for g in ghouls])
if not ghouls:
    raise RuntimeError("no Zone 2 ghouls found")
template = ghouls[0]
cls = template.get_class()
rot = template.get_actor_rotation()

# Three more, spread across the zone's width in front of the existing two (zone 2: x 3200..6400, y -800..800).
wanted = [
    ("BP_Enemy_Ghoul_Z2_3", unreal.Vector(4800.0, -327.0, 127.0)),
    ("BP_Enemy_Ghoul_Z2_4", unreal.Vector(5200.0, 160.0, 127.0)),
    ("BP_Enemy_Ghoul_Z2_5", unreal.Vector(5200.0, -160.0, 127.0)),
]
have = {g.get_actor_label() for g in ghouls}
for label, loc in wanted:
    if label in have:
        continue
    a = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, loc, rot)
    if a is None:
        raise RuntimeError("spawn failed: " + label)
    a.set_actor_label(label)
    ghouls.append(a)
    P("spawned %s at %s" % (label, loc))

for g in ghouls:
    g.set_editor_property("spawn_group_id", GROUP)
    P("  %-24s group=%s loc=%s" % (g.get_actor_label(), g.get_editor_property("spawn_group_id"), g.get_actor_location()))
P("zone 2 ghouls: %d" % len(ghouls))

# Rally point: east end of the zone, away from the player's approach from the west.
if not any(a.get_actor_label() == "MDRallyPoint_Z2" for a in actors):
    rally = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.MDRallyPoint, unreal.Vector(6000.0, 0.0, 127.0), unreal.Rotator(0.0, 0.0, 0.0))
    rally.set_actor_label("MDRallyPoint_Z2")
    P("spawned rally point at %s tags=%s" % (rally.get_actor_location(), rally.tags))

saved = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
P("saved map -> %s" % saved)
open(out, "w").write("\n".join(L))
