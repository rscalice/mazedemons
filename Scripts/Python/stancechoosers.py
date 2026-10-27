
# Weapon-stance pipeline for the GASP motion-matching choosers. Needs the MazeDemonsEditor module built (unreal.MD_ChooserLibrary,
# unreal.MD_PoseSearchLibrary). Run via Scripts\ApplyStanceChoosers.ps1 (commandlet, editor closed) or in-editor (Execute Python Script).
# Idempotent: every run rebuilds the stance rows from scratch and re-applies every stance cell. Report: Saved\ClaudeDump\stancechoosers.txt
# Env: STANCE_DRYRUN=1 only describes the tables (no changes, no save). STANCE_FORCE=1 rebuilds ranged databases even when unchanged.
#
# Choosers edited (BOTH, the player uses Dense at MMDatabaseLOD 0 and Sparse at LOD 1; before R0 only Sparse had stance rows,
# which is why weapon stances never showed in PIE):
#   CHT_PoseSearchDatabases_Dense, CHT_PoseSearchDatabases_Sparse
# Per nested table (Stand Idles / Stand Walks / Stand Runs):
#   1. adds an "Enum (Or)" column bound to the ABP variable WeaponStances (cells = bitmask of stances, bit N = enum value N, 0 = any)
#   2. removes the stance rows it created earlier (rows whose stance cell is exactly one managed stance) and inserts them again at the TOP:
#        melee   Greatsword -> EssentialGS databases, DualSword -> DualSword databases (one row per role: idle / stops / starts / loops)
#        ranged  Pistol / Rifle / Staff -> ONE database per stance, PSD_Ranged_<Stance>, built from the folder /Game/Animations/Ranged/<Stance>/
#                (a row in each table). Until that folder has animations the rows are PLACEHOLDERS that point at the unarmed
#                (stock) databases of each role, so the stance already exists in the chooser and plays unarmed motion.
#      every other column's cell (Speed2D, IsStarting, ...) is copied from the stock row that plays the same role
#   3. stock rows that now have a stance replacement are limited to the non-managed stances (Unarmed, OneHanded, Crossbow); all other
#      stock rows (pivots, landings, spin, traversal, turn in place, sprint stops) stay "any stance"
# To bring a ranged stance to life: drop the animation sequences in /Game/Animations/Ranged/<Stance>/ and run the wrapper again.
import os
import traceback
import unreal

MMD = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/"
CHOOSERS = [
    ("Sparse", MMD + "CHT_PoseSearchDatabases_Sparse"),
    ("Dense", MMD + "CHT_PoseSearchDatabases_Dense"),
]
ENUM_PATH = "/Script/MazeDemons.EMDWeaponStance"
BOUND_PROPERTY = "WeaponStances"
DISPLAY_NAME = "Weapon Stance"
MM = "/Game/Animations/MotionMatchingData/"

ALL_STANCES = ["Unarmed", "Greatsword", "DualSword", "OneHanded", "Crossbow", "Pistol", "Rifle", "Staff"]

# melee stance -> (folder under MM, database name suffix)
MELEE = [
    ("Greatsword", "EssentialGreatSword", "EssentialGS"),
    ("DualSword", "DualSword", "DualSword"),
]
RANGED = ["Pistol", "Rifle", "Staff"]
RANGED_ANIM_ROOT = "/Game/Animations/Ranged/"  # + <Stance>/
RANGED_DB_ROOT = "/Game/Animations/MotionMatchingData/Ranged/"  # + PSD_Ranged_<Stance>
RANGED_DB_NAME = "PSD_Ranged_{stance}"

# nested table -> [(role, melee database name pattern)] in insert order
TABLES = {
    "Stand Idles": [("Idle", "PSD_Stand_Idles_{sfx}"), ("WalkStop", "PSD_WalkStops_{sfx}"), ("RunStop", "PSD_RunStops_{sfx}")],
    "Stand Walks": [("WalkStart", "PSD_WalkStarts_{sfx}"), ("WalkLoop", "PSD_WalkLoops_{sfx}")],
    "Stand Runs": [("RunStart", "PSD_RunStarts_{sfx}"), ("RunLoop", "PSD_RunLoops_{sfx}")],
}
# the role whose row carries a ranged stance's single combined database
RANGED_MAIN_ROLE = {"Stand Idles": "Idle", "Stand Walks": "WalkLoop", "Stand Runs": "RunLoop"}
ROLE_SUFFIX = {"WalkStop": "Walk_Stops", "RunStop": "Run_Stops", "WalkStart": "Walk_Starts", "WalkLoop": "Walk_Loops",
               "RunStart": "Run_Starts", "RunLoop": "Run_Loops"}

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT = os.path.join(PROJECT, "Saved", "ClaudeDump", "stancechoosers.txt")
DRY = os.environ.get("STANCE_DRYRUN", "") not in ("", "0")
FORCE = os.environ.get("STANCE_FORCE", "") not in ("", "0")
lines = []


def log(s=""):
    lines.append(str(s))
    unreal.log("[stancechoosers] " + str(s))


def flush():
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def stock_name(kind, role):
    """Object name of the stock (unarmed) database of a role in the Dense or Sparse chooser."""
    if role == "Idle":
        return "PSD_Dense_Stand_Idles"
    return "PSD_%s_Stand_%s" % (kind, ROLE_SUFFIX[role])


def anim_names(db):
    names = []
    for i in range(db.get_num_animation_assets()):
        a = db.get_animation_asset(i)
        if a:
            names.append(a.get_name())
    return sorted(names)


def build_ranged_databases(schema):
    """One database per ranged stance from its anim folder. Returns {stance: database or None (placeholder)}, or None on failure."""
    result = {}
    lib = unreal.MD_PoseSearchLibrary
    for stance in RANGED:
        folder = RANGED_ANIM_ROOT + stance
        db_path = RANGED_DB_ROOT + RANGED_DB_NAME.format(stance=stance)
        anims = lib.find_anims_in_folder(folder)
        if not anims:
            log("%s: no animations in %s -> placeholder rows (unarmed databases)" % (stance, folder))
            result[stance] = None
            continue
        existing = unreal.load_asset(db_path) if unreal.EditorAssetLibrary.does_asset_exist(db_path) else None
        wanted = sorted(a.get_name() for a in anims)
        if existing is not None and not FORCE and existing.get_editor_property("schema") == schema and anim_names(existing) == wanted:
            log("%s: database %s already up to date (%d animations)" % (stance, db_path, len(wanted)))
            result[stance] = existing
            continue
        count, blog = lib.build_stance_database(db_path, schema, folder)
        for b in blog:
            log("  " + b)
        if count <= 0:
            log("FAIL: could not build %s" % db_path)
            return None
        result[stance] = unreal.load_asset(db_path)
    return result


def run():
    lib = unreal.MD_ChooserLibrary
    enum = unreal.load_object(None, ENUM_PATH)
    if enum is None:
        log("FAIL: cannot load enum " + ENUM_PATH)
        return False
    val = {n: lib.get_enum_value_by_name(enum, n) for n in ALL_STANCES}
    log("Stance enum values: " + str(val))
    if any(v < 0 for v in val.values()):
        log("FAIL: enum entry missing (rebuild the MazeDemons module)")
        return False

    # A weapon database whose schema or animations failed to load is silently empty at runtime and the character falls back to
    # the unarmed pose, so refuse to apply/save on top of one.
    bad = []
    for stance, folder, sfx in MELEE:
        for tbl in TABLES.values():
            for role, pattern in tbl:
                path = "%s%s/%s" % (MM, folder, pattern.format(sfx=sfx))
                db = unreal.load_asset(path)
                if db is None:
                    bad.append(path + " (missing)")
                elif db.get_editor_property("schema") is None:
                    bad.append(path + " (no schema)")
                elif db.get_num_animation_assets() == 0:
                    bad.append(path + " (no animations)")
    if bad:
        log("FAIL: melee weapon databases are empty or broken (fix their references first, check the editor log):")
        for b in bad:
            log("  " + b)
        return False

    roots = {}
    for kind, path in CHOOSERS:
        root = unreal.load_asset(path)
        if root is None:
            log("FAIL: cannot load " + path)
            return False
        roots[kind] = root

    # The project schema for ranged databases: the one the stock idle database uses.
    idles = lib.find_table(roots["Dense"], "Stand Idles")
    stock_idle_row = lib.find_row_by_asset_name(idles, stock_name("Dense", "Idle")) if idles else -1
    stock_idle = lib.get_row_object(idles, stock_idle_row) if stock_idle_row >= 0 else None
    schema = stock_idle.get_editor_property("schema") if stock_idle else None
    if schema is None:
        log("FAIL: cannot find the project pose-search schema (stock database PSD_Dense_Stand_Idles)")
        return False
    log("Ranged database schema: " + schema.get_path_name())

    ranged_db = {s: None for s in RANGED}
    if not DRY:
        built = build_ranged_databases(schema)
        if built is None:
            return False
        ranged_db = built

    handled = [m[0] for m in MELEE] + RANGED
    handled_bits = set(1 << val[n] for n in handled)
    stock_mask = sum(1 << val[n] for n in ALL_STANCES if n not in handled)

    for kind, _ in CHOOSERS:
        root = roots[kind]
        log("\n######## %s chooser" % kind)
        tables = {}
        for name in TABLES:
            t = lib.find_table(root, name)
            if t is None:
                log("FAIL: nested table not found: " + name)
                return False
            tables[name] = t
        log("\n=== %s BEFORE ===" % kind)
        for t in tables.values():
            for s in lib.describe_table(t):
                log(s)
        if DRY:
            continue

        for name, spec in TABLES.items():
            t = tables[name]
            col = lib.find_column_by_binding(t, BOUND_PROPERTY)
            if col < 0:
                col = lib.add_enum_column(t, BOUND_PROPERTY, enum, True, DISPLAY_NAME)
                if col < 0:
                    log("FAIL: could not add stance column to " + name)
                    return False
                log("%s/%s: added stance column (index %d)" % (kind, name, col))
            else:
                log("%s/%s: stance column already present (index %d)" % (kind, name, col))

            # 1. drop the stance rows an earlier run created (a cell with exactly one managed stance bit)
            removed = 0
            for r in reversed(range(lib.get_row_count(t))):
                if lib.get_enum_cell_mask(t, col, r) in handled_bits:
                    lib.remove_row(t, r)
                    removed += 1
            if removed:
                log("%s/%s: removed %d earlier stance rows (rebuilding)" % (kind, name, removed))

            # 2. insert them again at the top: melee first, then ranged
            main_role = RANGED_MAIN_ROLE[name]
            plan = []  # (stance, role, asset or None = placeholder, stock database name, label)
            for stance, folder, sfx in MELEE:
                for role, pattern in spec:
                    db_name = pattern.format(sfx=sfx)
                    plan.append((stance, role, unreal.load_asset("%s%s/%s" % (MM, folder, db_name)), stock_name(kind, role), db_name))
            for stance in RANGED:
                db = ranged_db[stance]
                for role, _pattern in spec:
                    if db is not None and role != main_role:
                        continue  # a real database covers the whole table through its main role row
                    plan.append((stance, role, db, stock_name(kind, role), db.get_name() if db else "placeholder"))

            top_masks = []
            replaced = set()  # stock database names that got a stance replacement
            pos = 0
            for stance, role, asset, stock, label in plan:
                template = lib.find_row_by_asset_name(t, stock)
                if template < 0:
                    log("WARN %s/%s: stock row %s not found, skipping %s %s" % (kind, name, stock, stance, role))
                    continue
                if asset is None:  # placeholder: the unarmed database of this role
                    asset = lib.get_row_object(t, template)
                row = lib.insert_asset_row(t, pos, asset, template)
                if row < 0:
                    log("FAIL %s/%s: insert of %s failed" % (kind, name, label))
                    return False
                top_masks.append(1 << val[stance])
                replaced.add(stock)
                pos += 1
                log("%s/%s: row %d = %s for %s (%s, cells from %s)" % (kind, name, row, asset.get_name(), stance, role, stock))

            # 3. re-apply every stance cell: stance rows -> their stance, replaced stock rows -> non-managed stances, rest -> any
            for r in range(lib.get_row_count(t)):
                if r < len(top_masks):
                    mask = top_masks[r]
                else:
                    mask = stock_mask if lib.get_row_asset_name(t, r) in replaced else 0
                if not lib.set_enum_cell_mask(t, col, r, mask):
                    log("FAIL %s/%s: could not set stance cell row %d" % (kind, name, r))
                    return False

        lib.compile_table(root)
        errs = [e for e in lib.get_compile_errors(root) if BOUND_PROPERTY in e]
        if errs:
            log("\nBINDING ERRORS (not saving): is ABP_SandboxCharacter saved with the WeaponStances variable?")
            for e in errs:
                log("  " + e)
            return False

        log("\n=== %s AFTER ===" % kind)
        for t in tables.values():
            for s in lib.describe_table(t):
                log(s)

    if DRY:
        log("\nDRY RUN: nothing changed.")
        return True

    for kind, path in CHOOSERS:
        ok = unreal.EditorAssetLibrary.save_loaded_asset(roots[kind], False)
        log("Saved %s: %s" % (path, ok))
        if not ok:
            return False
    return True


try:
    ok = run()
    log("RESULT: " + ("OK" if ok else "FAILED"))
except AttributeError as e:
    log("FAILED: %s -- rebuild the MazeDemonsEditor module first (unreal.MD_ChooserLibrary / MD_PoseSearchLibrary missing)." % e)
except Exception:
    log("EXCEPTION:\n" + traceback.format_exc())
flush()
