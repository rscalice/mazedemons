import unreal
# Zone 2 sixth ghoul (6-kill quest): adds BP_Enemy_Ghoul_Z2_06 to spawn group Zone2Pack. Idempotent.
# Commandlet, editor closed. Report: Saved/ClaudeDump/zone2sixth.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/zone2sixth.txt"
L = []
def P(s): L.append(str(s)); print(s)

MAP = "/Game/Maps/L_MazeDemonsPrototype"
GROUP = "Zone2Pack"
LABEL = "BP_Enemy_Ghoul_Z2_06"
world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
P("loaded: %s" % (world.get_name() if world else None))

actors = list(unreal.EditorLevelLibrary.get_all_level_actors())
ghouls = [a for a in actors if a.get_actor_label().startswith("BP_Enemy_Ghoul_Z2")]
P("existing zone 2 ghouls: %s" % [(g.get_actor_label(), str(g.get_actor_location())) for g in ghouls])
if not ghouls:
    raise RuntimeError("no Zone 2 ghouls found")
template = ghouls[0]
cls = template.get_class()
rot = template.get_actor_rotation()
P("template rotation: %s" % rot)

if not any(g.get_actor_label() == LABEL for g in ghouls):
    chosen = None
    for x in (5200.0, 5600.0):
        p = unreal.Vector(x, 0.0, 127.0)
        try:
            nav = unreal.NavigationSystemV1.project_point_to_navigation(world, p, None, None, unreal.Vector(50.0, 50.0, 300.0))
            P("nav projection at x=%s -> %s" % (x, nav))
        except Exception as e:
            P("nav projection failed at x=%s: %s" % (x, e))
            nav = None
        # floor check: line trace down
        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, 0.0, 600.0), unreal.Vector(x, 0.0, -200.0),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        P("floor trace at x=%s -> %s" % (x, hit))
        near = [g.get_actor_label() for g in ghouls if (g.get_actor_location() - p).length() < 150.0]
        P("ghouls within 150 of x=%s: %s" % (x, near))
        if not near:
            chosen = p
            break
    if chosen is None:
        raise RuntimeError("no free spot")
    a = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, chosen, rot)
    if a is None:
        raise RuntimeError("spawn failed")
    a.set_actor_label(LABEL)
    ghouls.append(a)
    P("spawned %s at %s" % (LABEL, chosen))

for g in ghouls:
    g.set_editor_property("spawn_group_id", GROUP)
saved = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
P("saved map -> %s" % saved)

actors = list(unreal.EditorLevelLibrary.get_all_level_actors())
z2 = [a for a in actors if a.get_actor_label().startswith("BP_Enemy_Ghoul") and a.get_editor_property("spawn_group_id") == GROUP]
for g in z2:
    P("  %-24s group=%s loc=%s rot=%s" % (g.get_actor_label(), g.get_editor_property("spawn_group_id"), g.get_actor_location(), g.get_actor_rotation()))
P("Zone2Pack ghouls: %d" % len(z2))
open(out, "w").write("\n".join(L))
