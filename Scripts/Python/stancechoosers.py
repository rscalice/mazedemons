
# Weapon-stance pipeline for the GASP motion-matching choosers. Needs the MazeDemonsEditor module built (unreal.MD_ChooserLibrary,
# unreal.MD_PoseSearchLibrary). Run via Scripts\ApplyStanceChoosers.ps1 (commandlet, editor closed) or in-editor (Execute Python Script).
# Idempotent: every run rebuilds the stance rows from scratch and re-applies every stance cell. Report: Saved\ClaudeDump\stancechoosers.txt
# Env: STANCE_DRYRUN=1 only classifies the clips and describes the tables (no changes, no save). STANCE_FORCE=1 rebuilds the ranged
#      databases even when unchanged. STANCE_SRC_<STANCE>=/Game/... overrides a stance's animation source folder (see RANGED_SOURCES).
#
# Choosers edited (BOTH, the player uses Dense at MMDatabaseLOD 0 and Sparse at LOD 1):
#   CHT_PoseSearchDatabases_Dense, CHT_PoseSearchDatabases_Sparse
# Per nested table (Stand Idles / Stand Walks / Stand Runs / Stand Sprint):
#   1. adds an "Enum (Or)" column bound to the ABP variable WeaponStances (cells = bitmask of stances, bit N = enum value N, 0 = any)
#   2. removes the stance rows it created earlier (rows whose stance cell is exactly one managed stance) and inserts them again at the TOP:
#        melee   Greatsword -> EssentialGS databases, DualSword -> DualSword databases (one row per role: idle / stops / starts / loops)
#        ranged  Pistol / Rifle -> one database PER ROLE (idles, turn in place, walk/run/sprint stops, starts, loops, pivots, spin
#                transition), the same split and the same schemas the stock Dense databases use, built from the stance's source set
#                (RANGED_SOURCES, GASP-named clips picked by name; nothing is moved or copied).
#                Staff -> PLACEHOLDER rows that point at the unarmed (stock) databases until it has a source set.
#      every other column's cell (Speed2D, IsStarting, ...) is copied from the stock row that plays the same role
#   3. a stock row that now has a stance replacement is limited to the stances that have none for it (everything else stays "any")
# The chooser returns every matching row, so the stance rows are searched together with the stock "any stance" rows (pivots, landings...).
#
# Clips that are NOT in a locomotion database (reported under "not used"): aim offsets (HeadAim_offset / HeadAimOffset folders, for R4),
# traversal clips (for the traversal databases), crouch, jump and landing clips, idle breaks, Sprint_to_Walk transitions, arc/circle/spin.
import os
import re
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
SCHEMA_ROOT = MMD + "Schemas/"
MANNEQUIN_MESH = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"

ALL_STANCES = ["Unarmed", "Greatsword", "DualSword", "OneHanded", "Crossbow", "Pistol", "Rifle", "Staff"]
ANY_BITS = (1 << 20) - 1  # "every stance, including ones appended later"

# melee stance -> (folder under MM, database name suffix)
MELEE = [
    ("Greatsword", "EssentialGreatSword", "EssentialGS"),
    ("DualSword", "DualSword", "DualSword"),
]
RANGED = ["Pistol", "Rifle", "Staff"]

# ---- Ranged animation sources -------------------------------------------------------------------------------------------------------
# stance -> {"root": content folder searched recursively, "suffix": the stance suffix of the clip names (M_Neutral_<clip><suffix>)}.
# A stance without an entry (Staff) has no source set yet and keeps placeholder rows. Override one with STANCE_SRC_PISTOL=/Game/... etc.
RANGED_SOURCES = {
    "Pistol": {"root": "/Game/Animations/WeaponStances/_FixedPistol", "suffix": "_Pistol"},
    "Rifle": {"root": "/Game/Animations/WeaponStances/_FixedRifle", "suffix": "_Rifle"},
}
CLIP_PREFIX = "M_Neutral_"
# Subfolders of a source set that never feed the locomotion databases.
EXCLUDE_FOLDERS = {"headaim_offset": "aim offset (R4)", "headaimoffset": "aim offset (R4)", "traversal": "traversal"}

RANGED_DB_ROOT = MM + "Ranged/"
RANGED_DB_NAME = "PSD_Ranged_{stance}_{role}"

# ---- Ranged roles: one pose-search database per movement type, same split / schemas as the stock Dense databases ----------------------
# role -> (nested table, schema asset, clip-name regexes (matched against the clip name without prefix and stance suffix),
#          stock database names the row cells are copied from, in lookup order; {K} = Dense / Sparse)
_PIV = r"(Box|Diamond|Hourglass|Prism|Pivot|Turn)"
RANGED_ROLES = [
    ("Idles", "Stand Idles", "PSS_Idle", [r"^Stand_Idle_Loop$"], ["PSD_Dense_Stand_Idles"]),
    ("TurnInPlace", "Stand Idles", "PSS_Default", [r"^Stand_Turn_(045|090|135|180)_[LR]$"], ["PSD_Dense_Stand_TurnInPlace"]),
    ("WalkStops", "Stand Idles", "PSS_Stop", [r"^Walk_Stop_", r"^Stand_Turn_(090|135|180)_[LR]$"],
     ["PSD_{K}_Stand_Walk_Stops", "PSD_Dense_Stand_Walk_Stops"]),
    ("RunStops", "Stand Idles", "PSS_Stop", [r"^Run_Stop_"], ["PSD_{K}_Stand_Run_Stops", "PSD_Dense_Stand_Run_Stops"]),
    ("SprintStops", "Stand Idles", "PSS_Stop", [r"^Sprint_Stop_"], ["PSD_{K}_Stand_Sprint_Stops", "PSD_Dense_Stand_Sprint_Stops"]),
    ("WalkStarts", "Stand Walks", "PSS_Default", [r"^Walk_(Start|Reface_Start)_"], ["PSD_{K}_Stand_Walk_Starts", "PSD_Dense_Stand_Walk_Starts"]),
    ("WalkLoops", "Stand Walks", "PSS_Default", [r"^Walk_(Loop|Shuffle)_", r"^Transition_Run_to_Walk_"],
     ["PSD_{K}_Stand_Walk_Loops", "PSD_Dense_Stand_Walk_Loops"]),
    ("WalkPivots", "Stand Walks", "PSS_Default", [r"^Walk_" + _PIV + "_"], ["PSD_{K}_Stand_Walk_Pivots", "PSD_Dense_Stand_Walk_Pivots"]),
    ("WalkSpin", "Stand Walks", "PSS_Default", [r"^Walk_Reface_Start_"], ["PSD_Dense_Stand_Walk_SpinTransition"]),
    ("RunStarts", "Stand Runs", "PSS_Default", [r"^Run_(Start|Reface_Start)_"], ["PSD_{K}_Stand_Run_Starts", "PSD_Dense_Stand_Run_Starts"]),
    ("RunLoops", "Stand Runs", "PSS_Default", [r"^Run_(Loop|Shuffle)_", r"^Transition_(Sprint_to_Run|Walk_to_Run)_"],
     ["PSD_{K}_Stand_Run_Loops", "PSD_Dense_Stand_Run_Loops"]),
    ("RunPivots", "Stand Runs", "PSS_Default", [r"^Run_" + _PIV + "_"], ["PSD_{K}_Stand_Run_Pivots", "PSD_Dense_Stand_Run_Pivots"]),
    ("RunSpin", "Stand Runs", "PSS_Default", [r"^Run_Reface_Start_"], ["PSD_Dense_Stand_Run_SpinTransition"]),
    ("SprintLoops", "Stand Sprint", "PSS_Default", [r"^Sprint_Loop_", r"^Transition_(Run_to_Sprint|Walk_to_Sprint)_"],
     ["PSD_{K}_Stand_Sprint_Loops", "PSD_Dense_Stand_Sprint_Loops"]),
    ("SprintStarts", "Stand Sprint", "PSS_Default", [r"^Sprint_(Start|Reface_Start)_"],
     ["PSD_{K}_Stand_Sprint_Starts", "PSD_Dense_Stand_Sprint_Starts"]),
    ("SprintPivots", "Stand Sprint", "PSS_Default", [r"^Sprint_(Diamond|Turn)_"],
     ["PSD_{K}_Stand_Sprint_Pivots", "PSD_Dense_Stand_Sprint_Pivots"]),
]
RANGED_ROLE_BY_NAME = {r[0]: r for r in RANGED_ROLES}
RANGED_TABLE_ORDER = ["Stand Idles", "Stand Walks", "Stand Runs", "Stand Sprint"]

# ---- Melee / placeholder roles (unchanged): nested table -> [(role, melee database name pattern)] in insert order ---------------------
TABLES = {
    "Stand Idles": [("Idle", "PSD_Stand_Idles_{sfx}"), ("WalkStop", "PSD_WalkStops_{sfx}"), ("RunStop", "PSD_RunStops_{sfx}")],
    "Stand Walks": [("WalkStart", "PSD_WalkStarts_{sfx}"), ("WalkLoop", "PSD_WalkLoops_{sfx}")],
    "Stand Runs": [("RunStart", "PSD_RunStarts_{sfx}"), ("RunLoop", "PSD_RunLoops_{sfx}")],
    "Stand Sprint": [],
}
# the placeholder row of a stance without a source set, per legacy role, copies this stock database (and the cells of its row)
PLACEHOLDER_STOCK = {
    "Idle": ["PSD_Dense_Stand_Idles"],
    "WalkStop": ["PSD_{K}_Stand_Walk_Stops"], "RunStop": ["PSD_{K}_Stand_Run_Stops"],
    "WalkStart": ["PSD_{K}_Stand_Walk_Starts"], "WalkLoop": ["PSD_{K}_Stand_Walk_Loops"],
    "RunStart": ["PSD_{K}_Stand_Run_Starts"], "RunLoop": ["PSD_{K}_Stand_Run_Loops"],
}
# stock database of each melee role (cells are copied from its row)
MELEE_STOCK = PLACEHOLDER_STOCK

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


def stock_candidates(names, kind):
    return [n.replace("{K}", kind) for n in names]


def find_template_row(lib, table, names, kind):
    """(row index, database name) of the first stock row found among the candidate names, else (-1, None)."""
    for n in stock_candidates(names, kind):
        r = lib.find_row_by_asset_name(table, n)
        if r >= 0:
            return r, n
    return -1, None


def anim_names(db):
    names = []
    for i in range(db.get_num_animation_assets()):
        a = db.get_animation_asset(i)
        if a:
            names.append(a.get_name())
    return sorted(names)


def source_for(stance):
    """The (root, suffix) of a stance's animation source set, with the STANCE_SRC_<STANCE> override applied, or None."""
    env = os.environ.get("STANCE_SRC_" + stance.upper(), "")
    if env:
        return env.rstrip("/"), "_" + stance
    src = RANGED_SOURCES.get(stance)
    return (src["root"], src["suffix"]) if src else None


def classify_clips(stance, root, suffix):
    """Walks a source set (recursively) and sorts its anim sequences into roles. Returns (roles {role: [AssetData]}, skipped {reason: [names]})."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous([root], True)
    roles = {r[0]: [] for r in RANGED_ROLES}
    skipped = {}
    patterns = {r[0]: [re.compile(p) for p in r[3]] for r in RANGED_ROLES}
    for ad in sorted(reg.get_assets_by_path(root, True), key=lambda a: str(a.asset_name)):
        if str(ad.asset_class_path.asset_name) != "AnimSequence":
            continue
        name = str(ad.asset_name)
        folder_parts = [p.lower() for p in str(ad.package_path)[len(root):].split("/") if p]
        why = next((EXCLUDE_FOLDERS[p] for p in folder_parts if p in EXCLUDE_FOLDERS), None)
        if why:
            skipped.setdefault(why, []).append(name)
            continue
        base = name[len(CLIP_PREFIX):] if name.startswith(CLIP_PREFIX) else name
        if suffix and base.endswith(suffix):
            base = base[: -len(suffix)]
        hit = False
        for role, pats in patterns.items():
            if any(p.search(base) for p in pats):
                roles[role].append(ad)
                hit = True
        if not hit:
            key = "_".join(base.split("_")[:2]) if base else name
            skipped.setdefault("not used (" + key + ")", []).append(name)
    return roles, skipped


def build_ranged_databases(schemas):
    """One database per (ranged stance, role). Returns {stance: {role: database}} (roles without clips are absent), or None on failure."""
    result = {}
    lib = unreal.MD_PoseSearchLibrary
    mannequin = unreal.load_asset(MANNEQUIN_MESH)
    skeleton_path = mannequin.get_editor_property("skeleton").get_path_name() if mannequin else None
    log("Motion-matching mesh skeleton (SKM_UEFN_Mannequin): %s" % skeleton_path)
    for stance in RANGED:
        result[stance] = {}
        src = source_for(stance)
        if src is None:
            log("%s: no animation source set -> placeholder rows (unarmed databases)" % stance)
            continue
        root, suffix = src
        roles, skipped = classify_clips(stance, root, suffix)
        total = sum(len(v) for v in roles.values())
        if total == 0:
            log("%s: no animations under %s -> placeholder rows (unarmed databases)" % (stance, root))
            continue
        log("\n%s: source %s" % (stance, root))
        for reason in sorted(skipped):
            log("  skipped %-34s %4d clips" % (reason, len(skipped[reason])))
        for role, table, schema_name, _p, _s in RANGED_ROLES:
            assets = roles[role]
            db_path = RANGED_DB_ROOT + RANGED_DB_NAME.format(stance=stance, role=role)
            if not assets:
                log("  %-12s no clips -> placeholder row for this role" % role)
                continue
            anims = [ad.get_asset() for ad in assets]
            anims = [a for a in anims if a is not None]
            wrong = [a.get_name() for a in anims if a.get_editor_property("skeleton").get_path_name() != skeleton_path]
            if wrong:
                log("FAIL: %d clips of %s are not on the SK_UEFN_Mannequin skeleton (e.g. %s)" % (len(wrong), role, wrong[0]))
                return None
            schema = schemas[schema_name]
            existing = unreal.load_asset(db_path) if unreal.EditorAssetLibrary.does_asset_exist(db_path) else None
            wanted = sorted(a.get_name() for a in anims)
            if existing is not None and not FORCE and existing.get_editor_property("schema") == schema and anim_names(existing) == wanted:
                log("  %-12s %-28s %4d clips  %s  up to date" % (role, db_path.split("/")[-1], len(wanted), schema_name))
                result[stance][role] = existing
                continue
            count, blog = lib.build_database_from_anims(db_path, schema, anims)
            for b in blog:
                log("    " + b)
            if count <= 0:
                log("FAIL: could not build %s" % db_path)
                return None
            if count != len(anims):
                log("FAIL: %s kept %d of %d clips (skeleton / schema mismatch)" % (db_path, count, len(anims)))
                return None
            log("  %-12s %-28s %4d clips  %s  built" % (role, db_path.split("/")[-1], count, schema_name))
            result[stance][role] = unreal.load_asset(db_path)
    return result


def dry_run_classification():
    for stance in RANGED:
        src = source_for(stance)
        if src is None:
            log("%s: no animation source set (placeholder)" % stance)
            continue
        root, suffix = src
        roles, skipped = classify_clips(stance, root, suffix)
        log("\n%s: source %s" % (stance, root))
        for role, table, schema_name, _p, _s in RANGED_ROLES:
            log("  %-12s %4d clips  (%s, %s)" % (role, len(roles[role]), table, schema_name))
        for reason in sorted(skipped):
            log("  skipped %-34s %4d clips" % (reason, len(skipped[reason])))


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

    # The project schemas the stock Dense databases use (idle / default / stop).
    schemas = {}
    for name in ("PSS_Idle", "PSS_Default", "PSS_Stop"):
        schemas[name] = unreal.load_asset(SCHEMA_ROOT + name)
        if schemas[name] is None:
            log("FAIL: cannot load schema " + SCHEMA_ROOT + name)
            return False
    log("Ranged database schemas: " + ", ".join("%s" % s.get_path_name() for s in schemas.values()))

    ranged_db = {s: {} for s in RANGED}
    if DRY:
        dry_run_classification()
    else:
        built = build_ranged_databases(schemas)
        if built is None:
            return False
        ranged_db = built

    handled = [m[0] for m in MELEE] + RANGED
    handled_bits = set(1 << val[n] for n in handled)

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

        for name in TABLES:
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
            plan = []  # (stance, asset or None = placeholder, template stock names, label)
            for stance, folder, sfx in MELEE:
                for role, pattern in TABLES[name]:
                    db_name = pattern.format(sfx=sfx)
                    plan.append((stance, unreal.load_asset("%s%s/%s" % (MM, folder, db_name)), MELEE_STOCK[role], db_name))
            for stance in RANGED:
                dbs = ranged_db[stance]
                if dbs:
                    for role, table, _schema, _pats, stock in RANGED_ROLES:
                        if table != name:
                            continue
                        if role in dbs:
                            plan.append((stance, dbs[role], stock, dbs[role].get_name()))
                        else:
                            log("WARN %s/%s: %s has no %s database, the stock row keeps playing it" % (kind, name, stance, role))
                else:
                    for role, _pattern in TABLES[name]:
                        plan.append((stance, None, PLACEHOLDER_STOCK[role], "placeholder"))

            top_masks = []
            replaced_by = {}  # stock database name -> bits of the stances that got their own row for it
            pos = 0
            for stance, asset, stock_names, label in plan:
                template, stock = find_template_row(lib, t, stock_names, kind)
                if template < 0:
                    log("WARN %s/%s: stock row %s not found, skipping %s %s" % (kind, name, stock_names, stance, label))
                    continue
                if asset is None:  # placeholder: the unarmed database of this role
                    asset = lib.get_row_object(t, template)
                row = lib.insert_asset_row(t, pos, asset, template)
                if row < 0:
                    log("FAIL %s/%s: insert of %s failed" % (kind, name, label))
                    return False
                top_masks.append(1 << val[stance])
                replaced_by[stock] = replaced_by.get(stock, 0) | (1 << val[stance])
                pos += 1
                log("%s/%s: row %d = %s for %s (cells from %s)" % (kind, name, row, asset.get_name(), stance, stock))

            # 3. re-apply every stance cell: stance rows -> their stance, replaced stock rows -> the stances without a replacement, rest -> any
            for r in range(lib.get_row_count(t)):
                if r < len(top_masks):
                    mask = top_masks[r]
                else:
                    mask = ANY_BITS & ~replaced_by.get(lib.get_row_asset_name(t, r), 0)
                    if mask == ANY_BITS:
                        mask = 0
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

    # Final check: every ranged stance row must reference a non-empty database.
    for kind, _ in CHOOSERS:
        for name in TABLES:
            t = lib.find_table(roots[kind], name)
            col = lib.find_column_by_binding(t, BOUND_PROPERTY)
            for r in range(lib.get_row_count(t)):
                mask = lib.get_enum_cell_mask(t, col, r)
                for stance in ("Pistol", "Rifle"):
                    if mask == (1 << val[stance]) and ranged_db[stance]:
                        obj = lib.get_row_object(t, r)
                        if obj is None or obj.get_num_animation_assets() == 0 or obj.get_editor_property("schema") is None:
                            log("FAIL %s/%s row %d (%s): empty or missing database" % (kind, name, r, stance))
                            return False

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
