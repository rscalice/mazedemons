import unreal
# V1 WP1: make sure L_MazeDemonsPrototype has a NavMeshBoundsVolume over all four zones and a baked navmesh
# (the map had neither, so the enemy brain's Chase could not path). Commandlet, editor closed. Idempotent.
# Report: Saved/ClaudeDump/ghoulnav.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulnav.txt"
L = []
def P(s): L.append(str(s)); print(s)

MAP = "/Game/Maps/L_MazeDemonsPrototype"
world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
P("loaded: %s" % (world.get_name() if world else None))

# MapZone_Z1..Z4 span x 0..14400, y -800..800, floor near z 40; pad generously.
box = unreal.Box(unreal.Vector(-600.0, -1200.0, -300.0), unreal.Vector(15000.0, 1200.0, 1200.0))
log = [str(l) for l in (unreal.MD_AILibrary.ensure_nav_mesh_covers(box, "NavMeshBounds_AllZones") or [])]
for l in log: P("  " + l)
ok = len(log) > 0 and log[-1].startswith("OK")
P("nav ok = %s" % ok)

if ok:
    saved = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    P("saved map -> %s" % saved)
    # Re-read and list what matters.
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    for a in unreal.EditorLevelLibrary.get_all_level_actors():
        cls = a.get_class().get_name()
        if "Nav" in cls or "Enemy" in cls:
            o, ext = a.get_actor_bounds(False)
            P("  %-32s %-24s center=(%.0f,%.0f,%.0f) ext=(%.0f,%.0f,%.0f)" % (a.get_actor_label(), cls, o.x, o.y, o.z, ext.x, ext.y, ext.z))
open(out, "w").write("\n".join(L))
