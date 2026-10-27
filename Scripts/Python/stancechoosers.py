# Adds weapon-stance rows to the GASP motion-matching choosers. Needs the MazeDemonsEditor module rebuilt (unreal.MD_ChooserLibrary).
# Run in-editor (File > Execute Python Script) or via Scripts\ApplyStanceChoosers.ps1 (commandlet, editor closed).
# Idempotent. Saves only CHT_PoseSearchDatabases_Sparse. Report: Saved\ClaudeDump\stancechoosers.txt
# Set env STANCE_DRYRUN=1 to only describe the tables (no changes, no save).
#
# What it does, per nested table (Stand Idles / Stand Walks / Stand Runs):
#   1. adds an "Enum (Or)" column bound to the ABP variable WeaponStances (cells = bitmask of stances, bit N = enum value N, 0 = any)
#   2. inserts weapon rows at the TOP (Greatsword -> EssentialGS databases, DualSword -> DualSword databases); every other column's
#      cell (Speed2D, IsStarting, JustLanded_*, ShouldTurnInPlace...) is copied from the stock row that plays the same role
#   3. stock rows that now have a weapon replacement are limited to the non-weapon stances (Unarmed, OneHanded, Crossbow), so a
#      weapon stance never searches the stock + weapon database at the same time (the choosers can return several databases);
#      all other stock rows (pivots, landings, spin, traversal, turn in place, sprint stops) stay "any stance"
import os
import traceback
import unreal

CHOOSER = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/CHT_PoseSearchDatabases_Sparse"
ENUM_PATH = "/Script/MazeDemons.EMDWeaponStance"
BOUND_PROPERTY = "WeaponStances"
DISPLAY_NAME = "Weapon Stance"
MM = "/Game/Animations/MotionMatchingData/"

ALL_STANCES = ["Unarmed", "Greatsword", "DualSword", "OneHanded", "Crossbow"]

# stance -> (folder under MM, database name suffix). Add Crossbow here later if its databases get the same role split.
WEAPONS = [
    ("Greatsword", "EssentialGreatSword", "EssentialGS"),
    ("DualSword", "DualSword", "DualSword"),
]

# nested table -> [(role, stock database object name the weapon row copies its cells from, weapon database name pattern)]
TABLES = {
    "Stand Idles": [
        ("Idle", "PSD_Dense_Stand_Idles", "PSD_Stand_Idles_{sfx}"),
        ("WalkStop", "PSD_Sparse_Stand_Walk_Stops", "PSD_WalkStops_{sfx}"),
        ("RunStop", "PSD_Sparse_Stand_Run_Stops", "PSD_RunStops_{sfx}"),
    ],
    "Stand Walks": [
        ("WalkStart", "PSD_Sparse_Stand_Walk_Starts", "PSD_WalkStarts_{sfx}"),
        ("WalkLoop", "PSD_Sparse_Stand_Walk_Loops", "PSD_WalkLoops_{sfx}"),
    ],
    "Stand Runs": [
        ("RunStart", "PSD_Sparse_Stand_Run_Starts", "PSD_RunStarts_{sfx}"),
        ("RunLoop", "PSD_Sparse_Stand_Run_Loops", "PSD_RunLoops_{sfx}"),
    ],
}

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT = os.path.join(PROJECT, "Saved", "ClaudeDump", "stancechoosers.txt")
DRY = os.environ.get("STANCE_DRYRUN", "") not in ("", "0")
lines = []


def log(s=""):
    lines.append(str(s))
    unreal.log("[stancechoosers] " + str(s))


def flush():
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def run():
    lib = unreal.MD_ChooserLibrary
    root = unreal.load_asset(CHOOSER)
    if root is None:
        log("FAIL: cannot load " + CHOOSER)
        return False
    enum = unreal.load_object(None, ENUM_PATH)
    if enum is None:
        log("FAIL: cannot load enum " + ENUM_PATH)
        return False
    val = {n: lib.get_enum_value_by_name(enum, n) for n in ALL_STANCES}
    log("Stance enum values: " + str(val))
    if any(v < 0 for v in val.values()):
        log("FAIL: enum entry missing")
        return False

    # A weapon database whose schema or animations failed to load (e.g. a dangling /Game/... reference after content was moved)
    # is silently empty at runtime and the character falls back to the unarmed pose, so refuse to apply/save on top of one.
    bad = []
    for stance, folder, sfx in WEAPONS:
        for tbl in TABLES.values():
            for role, stock_name, pattern in tbl:
                path = "%s%s/%s" % (MM, folder, pattern.format(sfx=sfx))
                db = unreal.load_asset(path)
                if db is None:
                    bad.append(path + " (missing)")
                elif db.get_editor_property("schema") is None:
                    bad.append(path + " (no schema)")
                elif db.get_num_animation_assets() == 0:
                    bad.append(path + " (no animations)")
    if bad:
        log("FAIL: weapon databases are empty or broken (fix their references first, check the editor log for missing packages):")
        for b in bad:
            log("  " + b)
        return False

    log("Tables in asset: " + ", ".join(lib.list_tables(root)))
    tables = {}
    for name in TABLES:
        t = lib.find_table(root, name)
        if t is None:
            log("FAIL: nested table not found: " + name)
            return False
        tables[name] = t

    log("\n=== BEFORE ===")
    for name, t in tables.items():
        for s in lib.describe_table(t):
            log(s)
    if DRY:
        log("\nDRY RUN: nothing changed.")
        return True

    handled = [w[0] for w in WEAPONS]
    stock_mask = sum(1 << val[n] for n in ALL_STANCES if n not in handled)
    changed = False

    for name, spec in TABLES.items():
        t = tables[name]
        col = lib.find_column_by_binding(t, BOUND_PROPERTY)
        if col < 0:
            col = lib.add_enum_column(t, BOUND_PROPERTY, enum, True, DISPLAY_NAME)
            if col < 0:
                log("FAIL: could not add stance column to " + name)
                return False
            log("%s: added stance column (index %d)" % (name, col))
            changed = True
        else:
            log("%s: stance column already present (index %d)" % (name, col))

        pos = 0
        masks = {}  # database object name -> stance mask
        for stance, folder, sfx in WEAPONS:
            for role, stock_name, pattern in spec:
                db_name = pattern.format(sfx=sfx)
                masks[db_name] = 1 << val[stance]
                masks.setdefault(stock_name, stock_mask)
                if lib.find_row_by_asset_name(t, db_name) >= 0:
                    log("%s: row %s already present" % (name, db_name))
                    pos += 1
                    continue
                template = lib.find_row_by_asset_name(t, stock_name)
                if template < 0:
                    log("WARN %s: stock row %s not found, skipping %s" % (name, stock_name, db_name))
                    continue
                path = "%s%s/%s" % (MM, folder, db_name)
                asset = unreal.load_asset(path)
                if asset is None:
                    log("WARN %s: cannot load %s, skipping" % (name, path))
                    continue
                row = lib.insert_asset_row(t, pos, asset, template)
                if row < 0:
                    log("FAIL %s: insert of %s failed" % (name, db_name))
                    return False
                log("%s: inserted row %d = %s (cells copied from %s)" % (name, row, db_name, stock_name))
                pos += 1
                changed = True

        # (re)apply every stance cell: weapon rows -> their stance, replaced stock rows -> non-weapon stances, rest -> any
        for r in range(lib.get_row_count(t)):
            rn = lib.get_row_asset_name(t, r)
            if not lib.set_enum_cell_mask(t, col, r, masks.get(rn, 0)):
                log("FAIL %s: could not set stance cell row %d" % (name, r))
                return False

    lib.compile_table(root)
    errs = [e for e in lib.get_compile_errors(root) if BOUND_PROPERTY in e]
    if errs:
        log("\nBINDING ERRORS (not saving): is ABP_SandboxCharacter saved with the WeaponStances variable?")
        for e in errs:
            log("  " + e)
        return False

    log("\n=== AFTER ===")
    for name, t in tables.items():
        for s in lib.describe_table(t):
            log(s)

    ok = unreal.EditorAssetLibrary.save_loaded_asset(root, False)  # always save: cells are re-applied every run
    log("\nSaved %s: %s (changes this run: %s)" % (CHOOSER, ok, changed))
    return bool(ok)


try:
    ok = run()
    log("RESULT: " + ("OK" if ok else "FAILED"))
except AttributeError as e:
    log("FAILED: %s -- rebuild the MazeDemonsEditor module first (unreal.MD_ChooserLibrary missing)." % e)
except Exception:
    log("EXCEPTION:\n" + traceback.format_exc())
flush()
