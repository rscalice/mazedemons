
# Read-only dump for the GUNS work (R0 follow-up): every chooser table (all of them, not just Idles/Walks/Runs), the schema and size of
# every stock pose-search database, and the skeleton of the gun animation sets. Report: Saved\ClaudeDump\gundump.txt
import os
import traceback
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Saved", "ClaudeDump", "gundump.txt")
L = []


def P(s=""):
    L.append(str(s))
    unreal.log("[gundump] " + str(s))


MMD = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/"
try:
    lib = unreal.MD_ChooserLibrary
    for name in ["CHT_PoseSearchDatabases", "CHT_PoseSearchDatabases_Dense", "CHT_PoseSearchDatabases_Sparse"]:
        root = unreal.load_asset(MMD + name)
        P("######## " + name)
        if root is None:
            P("missing")
            continue
        P("tables: " + str(lib.list_tables(root)))
        if name != "CHT_PoseSearchDatabases_Sparse":
            for t in lib.list_tables(root):
                tab = lib.find_table(root, t)
                for s in lib.describe_table(tab):
                    P(s)

    P("\n######## stock + weapon databases")
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for folder in [MMD + "Databases", "/Game/Animations/MotionMatchingData"]:
        for ad in reg.get_assets_by_path(folder, recursive=True):
            if str(ad.asset_class_path.asset_name) != "PoseSearchDatabase":
                continue
            db = ad.get_asset()
            sch = db.get_editor_property("schema")
            P("%s | schema=%s | anims=%d" % (str(ad.package_name), sch.get_name() if sch else None, db.get_num_animation_assets()))

    P("\n######## gun animation skeletons")
    for stance in ["_FixedPistol", "_FixedRifle"]:
        root = "/Game/Animations/WeaponStances/%s" % stance
        paths = unreal.EditorAssetLibrary.list_assets(root, recursive=True, include_folder=False)
        skels = {}
        classes = {}
        for p in paths:
            a = unreal.load_asset(p)
            classes[type(a).__name__] = classes.get(type(a).__name__, 0) + 1
            if isinstance(a, unreal.AnimSequence):
                sk = a.get_editor_property("skeleton")
                key = sk.get_path_name() if sk else "None"
                skels[key] = skels.get(key, 0) + 1
        P("%s: %d assets, classes=%s, skeletons=%s" % (stance, len(paths), classes, skels))

    mesh = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin")
    if mesh is None:
        hits = [str(a.package_name) for a in reg.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "SkeletalMesh"))
                if "UEFN_Mannequin" in str(a.asset_name)]
        P("SKM_UEFN_Mannequin candidates: " + str(hits))
    else:
        P("SKM_UEFN_Mannequin skeleton: " + mesh.get_editor_property("skeleton").get_path_name())
    sch = unreal.load_asset(MMD + "Schemas/PSS_Idle")
    P("PSS_Idle skeleton: " + str(sch.get_editor_property("skeleton")))
    for n in ["PSS_Default", "PSS_Stop", "PSS_Jump", "PSS_Traversal"]:
        s = unreal.load_asset(MMD + "Schemas/" + n)
        P("%s skeleton: %s" % (n, s.get_editor_property("skeleton") if s else None))
except Exception:
    P("EXCEPTION:\n" + traceback.format_exc())
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(L) + "\n")
