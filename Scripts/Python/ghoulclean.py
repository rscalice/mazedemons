import unreal, traceback
# Removes the old Ghoul placeholders (the Swipe attack, the single-clip locomotion and its blend space) once nothing outside the
# placeholder set references them. A placeholder that is still referenced is KEPT and reported.
# Run with the editor closed (commandlet; Scripts\RunPy.ps1 Scripts/Python/ghoulclean.py). Idempotent.
# Report: Saved/ClaudeDump/ghoulclean.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulclean.txt"
L = []
def P(s):
    L.append(str(s)); print(s)

D = "/Game/Animations/Enemies/Ghoul/"
OLD = ["AM_Ghoul_Swipe", "A_Ghoul_Swipe_RM", "A_Ghoul_Claw", "A_Ghoul_Walk", "A_Ghoul_Run", "A_Ghoul_Walk_Travel", "A_Ghoul_Run_Travel", "BS_Ghoul_Locomotion"]
EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
AR.scan_paths_synchronous(["/Game"], True)
old_pkgs = {D + n for n in OLD}
try:
    deps = unreal.AssetRegistryDependencyOptions()
    deps.include_soft_package_references = True
    deps.include_hard_package_references = True
    deps.include_searchable_names = False
    deps.include_soft_management_references = False
    deps.include_hard_management_references = False
    keep = []
    for n in OLD:
        pkg = D + n
        if not EAL.does_asset_exist(pkg):
            P("  %s: already gone" % n); continue
        refs = [str(r) for r in AR.get_referencers(pkg, deps)]
        outside = [r for r in refs if r not in old_pkgs]
        P("  %s referenced by %s" % (n, outside if outside else "nothing outside the placeholder set"))
        if outside:
            keep.append(n)
    # delete in dependency order: montage / blend space before the clips they reference
    for n in OLD:
        if n in keep or not EAL.does_asset_exist(D + n):
            continue
        # keep it too when a kept placeholder still references it
        still = [k for k in keep if (D + n) in [str(x) for x in AR.get_dependencies(D + k, deps)]]
        if still:
            P("  %s kept: referenced by kept %s" % (n, still)); keep.append(n); continue
    for n in ["AM_Ghoul_Swipe", "BS_Ghoul_Locomotion", "A_Ghoul_Swipe_RM", "A_Ghoul_Claw", "A_Ghoul_Walk", "A_Ghoul_Run", "A_Ghoul_Walk_Travel", "A_Ghoul_Run_Travel"]:
        if n in keep or not EAL.does_asset_exist(D + n):
            continue
        P("  deleted %s -> %s" % (n, EAL.delete_asset(D + n)))
    P("  KEPT: %s" % keep)
except Exception:
    P("EXC: " + traceback.format_exc())
open(out, "w").write("\n".join(L))
