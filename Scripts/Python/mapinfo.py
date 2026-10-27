import unreal
# Read-only look at L_MazeDemonsPrototype: nav bounds, enemies, player starts, zone markers. Report to Saved/ClaudeDump/mapinfo.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/mapinfo.txt"
L = []
def P(s): L.append(str(s)); print(s)

ok = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/L_MazeDemonsPrototype")
P("loaded: %s" % ok)
actors = unreal.EditorLevelLibrary.get_all_level_actors()
P("actors: %d" % len(actors))
for a in actors:
    cls = a.get_class().get_name()
    name = a.get_actor_label()
    interesting = ("Nav" in cls or "Enemy" in cls or "Ghoul" in name or "Zone" in name or "PlayerStart" in cls or "Spawn" in name or "Spawn" in cls)
    if not interesting: continue
    loc = a.get_actor_location()
    o, ext = a.get_actor_bounds(False)
    P("  %-36s %-40s loc=(%.0f,%.0f,%.0f) bounds center=(%.0f,%.0f,%.0f) ext=(%.0f,%.0f,%.0f)" % (name, cls, loc.x, loc.y, loc.z, o.x, o.y, o.z, ext.x, ext.y, ext.z))
# nav data
for a in actors:
    if a.get_class().get_name().startswith("RecastNavMesh"):
        P("  navdata: %s" % a.get_actor_label())
open(out, "w").write("\n".join(L))
