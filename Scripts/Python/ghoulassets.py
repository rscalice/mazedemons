import unreal, traceback, math
# V1 WP1 Ghoul assets (run with the editor closed as a commandlet; idempotent):
#   1. ST_Enemy StateTree built from C++ (UMD_AILibrary::BuildEnemyStateTree)
#   2. SK_Ghoul: a copy of the Fab ghoul mesh outside Content/Fab, on the project's SK_Mannequin skeleton
#      (its 79 bones are a strict subset of the 161 of SK_Mannequin), physics asset PA_Ghoul_UE5
#   3. Locomotion anims retargeted from the Fab Manny demo (MM_Idle / MM_Walk_Fwd / MM_Run_Fwd) onto SK_Ghoul
#   4. BS_Ghoul_Locomotion (1D blend space on Speed)
#   5. Attack montages (Swipe, Lunge) from the Werewolf claw clip retargeted by ghoulclaw.py (run it before this script),
#      with MD Weapon Trace + Motion Warping windows (WINDOWS below)
#   6. DA_Archetype_Ghoul.Attacks filled; BP_Enemy_Ghoul mesh swapped
# Report: Saved/ClaudeDump/ghoulassets.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulassets.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
def sec(name, fn):
    try:
        P("== " + name); return fn()
    except Exception:
        P("EXC in %s: %s" % (name, traceback.format_exc())); return None

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.AnimationLibrary
SK_PATH = "/Game/Characters/UE5_Mannequins/Meshes/SK_Mannequin"
ANIM_DIR = "/Game/Animations/Enemies/Ghoul"
CHAR_DIR = "/Game/Characters/Enemies/Ghoul"
MESH_PATH = CHAR_DIR + "/SK_Ghoul"

def save(path):
    ok = EAL.save_asset(path, only_if_is_dirty=False)
    P("  saved %s -> %s" % (path, ok)); return ok

def unpack(res):
    """(ok, log) from a C++ helper returning bool + TArray<FString>: python gives a tuple or just the array."""
    if isinstance(res, tuple):
        return bool(res[0]), list(res[1])
    log = [res] if isinstance(res, str) else list(res)
    return not any(str(l).startswith("Error") for l in log), log

# ---------------------------------------------------------------- 1. StateTree
def s_statetree():
    ok, log = unpack(unreal.MD_AILibrary.build_enemy_state_tree("/Game/AI/ST_Enemy"))
    for l in log: P("  " + str(l))
    P("  build ok = %s" % ok)
    for l in unreal.MD_AILibrary.describe_state_tree("/Game/AI/ST_Enemy"): P("  " + str(l))
sec("StateTree", s_statetree)

# ---------------------------------------------------------------- 2. Mesh
def s_mesh():
    sk = unreal.load_asset(SK_PATH)
    if not EAL.does_asset_exist(MESH_PATH):
        EAL.make_directory(CHAR_DIR)
        dup = EAL.duplicate_asset("/Game/Fab/Ghoul/Mesh/SK_Ghoul_UE5", MESH_PATH)
        P("  duplicated -> %s" % (dup.get_path_name() if dup else None))
    mesh = unreal.load_asset(MESH_PATH)
    if mesh.skeleton != sk:
        r = unreal.MD_AILibrary.assign_skeleton(mesh, sk)
        ok, err = (r[0], r[1]) if isinstance(r, tuple) else ((r == "" or r is True), r)
        P("  assign_skeleton -> %s %s" % (ok, err))
        if not ok:
            raise RuntimeError("skeleton assignment failed: " + str(err))
    pa = unreal.load_asset("/Game/Data/Enemies/Physics/PA_Ghoul_UE5")
    if pa and mesh.physics_asset != pa:
        mesh.set_editor_property('physics_asset', pa)
    mesh.modify(); save(MESH_PATH)
    mesh = unreal.load_asset(MESH_PATH)
    P("  SK_Ghoul skeleton now: %s, physics: %s" % (mesh.skeleton.get_path_name(), mesh.physics_asset.get_path_name() if mesh.physics_asset else None))
    return mesh
ghoul_mesh = sec("Mesh", s_mesh)

# ---------------------------------------------------------------- 3. Locomotion retarget
LOCO = {"MM_Idle": "A_Ghoul_Idle", "MM_Walk_Fwd": "A_Ghoul_Walk", "MM_Run_Fwd": "A_Ghoul_Run"}
def s_retarget():
    missing = [n for n in LOCO.values() if not EAL.does_asset_exist(ANIM_DIR + "/" + n)]
    if not missing:
        P("  locomotion anims already exist"); return
    EAL.make_directory(ANIM_DIR)
    src_mesh = unreal.load_asset("/Game/Fab/Ghoul/Demo/Demoscene_UE5/Mesh/SKM_Manny")
    rig = unreal.load_asset("/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget")
    rtg_path = ANIM_DIR + "/Retarget/RTG_FabManny_to_Ghoul"
    if EAL.does_asset_exist(rtg_path):
        rtg = unreal.load_asset(rtg_path)
    else:
        EAL.make_directory(ANIM_DIR + "/Retarget")
        rtg = AT.create_asset("RTG_FabManny_to_Ghoul", ANIM_DIR + "/Retarget", unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rig)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, rig)
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, src_mesh)
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, ghoul_mesh)
    if c.get_num_retarget_ops() == 0:
        c.add_default_ops()
    P("  retarget ops: %d" % c.get_num_retarget_ops())
    c.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    save(rtg_path)
    AR = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = []
    for src in LOCO.keys():
        ad = AR.get_asset_by_object_path("/Game/Fab/Ghoul/Demo/Demoscene_UE5/Animations/%s.%s" % (src, src))
        P("  source %s valid=%s" % (src, ad.is_valid()))
        assets.append(ad)
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.assets_to_retarget = assets
    inputs.source_mesh = src_mesh
    inputs.target_mesh = ghoul_mesh
    inputs.ik_retarget_asset = rtg
    inputs.search = "MM_"
    inputs.replace = "A_Ghoul_"
    inputs.target_path = ANIM_DIR
    inputs.use_source_path = False
    inputs.include_referenced_assets = False
    inputs.overwrite_existing_files = True
    res = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    for r in res:
        P("  retargeted: %s" % str(r.package_name))
    # rename to the final names (Walk_Fwd -> Walk etc.)
    for src, dst in LOCO.items():
        made = ANIM_DIR + "/" + src.replace("MM_", "A_Ghoul_")
        if made != ANIM_DIR + "/" + dst and EAL.does_asset_exist(made):
            EAL.rename_asset(made, ANIM_DIR + "/" + dst)
        a = unreal.load_asset(ANIM_DIR + "/" + dst)
        P("  %s: skeleton %s len %.2f" % (dst, a.get_editor_property('skeleton').get_name() if a else None, a.get_play_length() if a else -1))
        if a: save(ANIM_DIR + "/" + dst)
sec("Retarget", s_retarget)

# ---------------------------------------------------------------- 4. Blend space
def s_blendspace():
    path = ANIM_DIR + "/BS_Ghoul_Locomotion"
    sk = unreal.load_asset(SK_PATH)
    anims = {k: unreal.load_asset(ANIM_DIR + "/" + v) for k, v in LOCO.items()}
    if any(a is None for a in anims.values()):
        P("  missing locomotion anims, skipping"); return
    if EAL.does_asset_exist(path):
        bs = unreal.load_asset(path)
    else:
        f = unreal.BlendSpaceFactory1D()
        f.set_editor_property('target_skeleton', sk)
        bs = AT.create_asset("BS_Ghoul_Locomotion", ANIM_DIR, unreal.BlendSpace1D, f)
    try:
        bp = bs.get_editor_property('blend_parameters')
        p = bp[0]
        p.set_editor_property('display_name', 'Speed'); p.set_editor_property('min', 0.0); p.set_editor_property('max', 600.0); p.set_editor_property('grid_num', 6)
        bp[0] = p
        bs.set_editor_property('blend_parameters', bp)
        P("  blend parameter Speed 0..600")
    except Exception as ex:
        P("  blend_parameters ERR %s" % ex)
    samples = []
    for anim, speed in ((anims["MM_Idle"], 0.0), (anims["MM_Walk_Fwd"], 200.0), (anims["MM_Run_Fwd"], 520.0)):
        s = unreal.BlendSample()
        s.set_editor_property('animation', anim)
        s.set_editor_property('sample_value', unreal.Vector(speed, 0.0, 0.0))
        s.set_editor_property('rate_scale', 1.0)
        samples.append(s)
    bs.set_editor_property('sample_data', samples)
    # Setting sample_data from a script does not re-triangulate the blend space (the editor does that from
    # PostEditChangeProperty); without segments GetSamplesFromBlendInput returns nothing and the player shows one pose.
    for l in unreal.MD_AILibrary.finalize_blend_space(bs): P("  " + str(l))
    bs.modify(); save(path)
    bs = unreal.load_asset(path)
    try:
        P("  samples: %s" % [(x.get_editor_property('animation').get_name() if x.get_editor_property('animation') else None, x.get_editor_property('sample_value').x) for x in bs.get_editor_property('sample_data')])
    except Exception as ex:
        P("  samples: (unreadable from python: %s)" % ex)
    return bs
blendspace = sec("BlendSpace", s_blendspace)

# ---------------------------------------------------------------- 4b. Anim Blueprint (graph authored from C++)
def s_abp():
    if blendspace is None:
        P("  no blend space, skipping"); return None
    sk = unreal.load_asset(SK_PATH)
    ok, log = unpack(unreal.MD_AILibrary.build_enemy_anim_blueprint(ANIM_DIR + "/ABP_Ghoul", sk, blendspace))
    for l in log: P("  " + str(l))
    P("  build ok = %s" % ok)
    return unreal.load_asset(ANIM_DIR + "/ABP_Ghoul") if ok else None
abp = sec("AnimBlueprint", s_abp)

# ---------------------------------------------------------------- 5. Attack analysis + montages
# Attack sources: the Werewolf claw attack retargeted by ghoulclaw.py (run that first), with a baked step-in / leap on the
# root bone. The earlier picks (twin-axe ComboA1 = a 360 degree spin, twin-sword Attack48 = a flipping jump) read as a
# human swordsman and went upside down; the project has no other creature melee clips.
CANDIDATES = [
    "/Game/Animations/Enemies/Ghoul/A_Ghoul_Swipe_RM",
    "/Game/Animations/Enemies/Ghoul/A_Ghoul_Lunge_RM",
]
# Trace window per source (clip time, before the wind-up rate scaling); both claws trace.
WINDOWS = {"A_Ghoul_Swipe_RM": (0.50, 0.98), "A_Ghoul_Lunge_RM": (0.60, 1.05)}

def analyze(anim):
    """Returns dict: len, travel (root motion cm), hand peak time/speed per hand."""
    length = anim.get_play_length()
    n = max(int(length * 30), 2)
    dt = length / n
    opts = unreal.AnimPoseEvaluationOptions()
    # Keep root motion in the root bone track so 'root' travel can be read directly (extracting it zeroes the root).
    opts.set_editor_property('extract_root_motion', False)
    hands = {}
    root_start = None; root_end = None
    root_path = []  # (t, 2D distance travelled from the start)
    for h in ('hand_r', 'hand_l'):
        prev = None; peak = (0.0, 0.0)
        for i in range(n + 1):
            t = i * dt
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
            xf = unreal.AnimPoseExtensions.get_bone_pose(pose, h, unreal.AnimPoseSpaces.WORLD)
            loc = xf.translation
            if h == 'hand_r':
                root = unreal.AnimPoseExtensions.get_bone_pose(pose, 'root', unreal.AnimPoseSpaces.WORLD).translation
                if i == 0: root_start = root
                root_end = root
                root_path.append((t, math.sqrt((root.x - root_start.x) ** 2 + (root.y - root_start.y) ** 2)))
            if prev is not None:
                sp = (loc - prev).length() / dt
                if sp > peak[1]: peak = (t, sp)
            prev = loc
        hands[h] = peak
    travel = (root_end - root_start) if root_start else unreal.Vector(0, 0, 0)
    # Time by which 90% of the root travel is done: the motion-warp window must reach it, or the authored displacement
    # after the window carries the enemy through its target.
    total = root_path[-1][1] if root_path else 0.0
    travel_end = 0.0
    for t, dist in root_path:
        travel_end = t
        if total > 1.0 and dist >= 0.9 * total: break
    return {"len": length, "travel": travel, "hands": hands, "travel_end": travel_end, "root_path": root_path}

def travel_after(d, t):
    """Authored 2D root travel (cm) left after time t."""
    path = d.get("root_path") or []
    if not path: return 0.0
    total = path[-1][1]
    at = next((dist for tt, dist in path if tt >= t), total)
    return max(0.0, total - at)

def s_analyze():
    rows = []
    for p in CANDIDATES:
        a = unreal.load_asset(p)
        if a is None: P("  missing %s" % p); continue
        d = analyze(a)
        rows.append((p, d))
        P("  %s len=%.2f travel=(%.0f,%.0f) 90%% by %.2fs hand_r peak %.2fs %.0fcm/s hand_l peak %.2fs %.0fcm/s" % (
            a.get_name(), d["len"], d["travel"].x, d["travel"].y, d["travel_end"], d["hands"]["hand_r"][0], d["hands"]["hand_r"][1], d["hands"]["hand_l"][0], d["hands"]["hand_l"][1]))
    return rows
analysis = sec("Analyze", s_analyze) or []
open(out, "w").write("\n".join(L))

# ---------------------------------------------------------------- 5b. Pick attacks and build montages
SWIPE_OVERRIDE = "/Game/Animations/Enemies/Ghoul/A_Ghoul_Swipe_RM"
LUNGE_OVERRIDE = "/Game/Animations/Enemies/Ghoul/A_Ghoul_Lunge_RM"
WARP_TARGET = "AttackTarget"
WARP_END = {"A_Ghoul_Swipe_RM": 0.80, "A_Ghoul_Lunge_RM": 0.85}  # clip seconds; travel ends at 0.85 / 0.90

def pick_attacks(rows):
    def dist(d): return math.sqrt(d["travel"].x ** 2 + d["travel"].y ** 2)
    swipe = SWIPE_OVERRIDE
    lunge = LUNGE_OVERRIDE
    if swipe is None:
        c = [(p, d) for p, d in rows if dist(d) <= 140.0 and d["len"] <= 1.8]
        c.sort(key=lambda x: (0 if "DTA" in x[0] else 1, x[1]["len"]))
        swipe = c[0][0] if c else None
    if lunge is None:
        c = [(p, d) for p, d in rows if 220.0 <= dist(d) <= 600.0]
        c.sort(key=lambda x: x[1]["len"])
        lunge = c[0][0] if c else None
    return swipe, lunge

def window_for(d, name=None):
    hr, hl = d["hands"]["hand_r"], d["hands"]["hand_l"]
    peak = hr if hr[1] >= hl[1] else hl
    start = max(0.15, peak[0] - 0.12)
    end = min(d["len"] - 0.05, peak[0] + 0.2)
    if end <= start + 0.1: end = min(d["len"], start + 0.25)
    sockets = []
    top = max(hr[1], hl[1])
    if hr[1] >= 0.6 * top: sockets.append("hand_r")
    if hl[1] >= 0.6 * top: sockets.append("hand_l")
    if name in WINDOWS:
        start, end = WINDOWS[name]
        sockets = ["hand_r", "hand_l"]
    return start, end, sockets

def make_modifier(state):
    for prop, cls_name in (("root_motion_modifier_config", "RootMotionModifierConfig_SkewWarp"), ("root_motion_modifier", "RootMotionModifier_SkewWarp")):
        try: state.get_editor_property(prop)
        except Exception: continue
        cls = unreal.load_class(None, "/Script/MotionWarping." + cls_name)
        if cls is None: continue
        obj = unreal.new_object(cls, outer=state)
        for names, val in ((["warp_target_name"], WARP_TARGET), (["warp_translation", "b_warp_translation"], True),
                           (["ignore_z_axis", "b_ignore_z_axis"], True), (["warp_rotation", "b_warp_rotation"], True)):
            for n in names:
                try: obj.set_editor_property(n, val); break
                except Exception: continue
        # DEFAULT: turn to the warp target's rotation, which the attack component sets to the facing yaw at swing start.
        # FACING recomputes a yaw from the attacker's own position every tick and degenerates (any flip) once the skew warp
        # has put the capsule on the warp point. (ghoulwarp.py re-applies this to the existing montages.)
        try: obj.set_editor_property("rotation_type", unreal.MotionWarpRotationType.DEFAULT)
        except Exception: pass
        state.set_editor_property(prop, obj)
        return prop
    return None

def build_montage(name, src_path, d):
    path = ANIM_DIR + "/" + name
    src = unreal.load_asset(src_path)
    sk = unreal.load_asset(SK_PATH)
    if EAL.does_asset_exist(path):
        m = unreal.load_asset(path)
        tracks = m.get_editor_property('slot_anim_tracks')
        segs = tracks[0].get_editor_property('anim_track').get_editor_property('anim_segments') if len(tracks) else []
        cur = segs[0].get_editor_property('anim_reference') if len(segs) else None
        if cur is None or cur.get_path_name() != src.get_path_name():
            # Source animation changed: recreate from the factory so length/sections are derived cleanly.
            # The archetype references the montage, so drop its attacks first (section 6 refills them).
            P("  %s source changed (%s -> %s), recreating" % (name, cur.get_name() if cur else None, src.get_name()))
            da = unreal.load_asset("/Game/Data/Enemies/DA_Archetype_Ghoul")
            da.set_editor_property('attacks', []); EAL.save_loaded_asset(da)
            deleted = EAL.delete_asset(path)
            P("  deleted old %s -> %s" % (name, deleted))
            if not deleted:
                P("  FAIL cannot replace montage %s" % name); return None, []
            m = None
    if not EAL.does_asset_exist(path):
        f = unreal.AnimMontageFactory()
        f.set_editor_property('target_skeleton', sk)
        try: f.set_editor_property('source_animation', src)
        except Exception as ex: P("  factory source_animation ERR %s" % ex)
        m = AT.create_asset(name, ANIM_DIR, unreal.AnimMontage, f)
    if m is None:
        P("  FAIL montage %s" % name); return None, []
    tracks = m.get_editor_property('slot_anim_tracks')
    segs = tracks[0].get_editor_property('anim_track').get_editor_property('anim_segments') if len(tracks) else []
    if len(segs) == 0 or segs[0].get_editor_property('anim_reference') is None:
        # New montage, or the source animation changed: rebuild the single DefaultSlot segment.
        seg = unreal.AnimSegment(); seg.set_editor_property('anim_reference', src)
        seg.set_editor_property('anim_end_time', src.get_play_length())
        at = unreal.AnimTrack(); at.set_editor_property('anim_segments', [seg])
        st = unreal.SlotAnimationTrack(); st.set_editor_property('slot_name', 'DefaultSlot'); st.set_editor_property('anim_track', at)
        m.set_editor_property('slot_anim_tracks', [st])
    try:
        mode = unreal.MontageBlendMode.INERTIALIZATION
        m.set_editor_property('blend_mode_in', mode); m.set_editor_property('blend_mode_out', mode)
    except Exception: pass
    start, end, sockets = window_for(d, src.get_name())
    for nm in ("MD Weapon Trace", "AnimNotifyState_MotionWarping", "MotionWarping"):
        try: lib.remove_animation_notify_events_by_name(m, nm)
        except Exception: pass
    names = [str(x) for x in lib.get_animation_notify_track_names(m)]
    for tn, col in (("Trace", unreal.LinearColor(0.9, 0.2, 0.2, 1.0)), ("Warp", unreal.LinearColor(1.0, 0.6, 0.1, 1.0))):
        if tn not in names: lib.add_animation_notify_track(m, tn, col)
    trace_cls = unreal.load_class(None, "/Script/MazeDemons.MDANS_WeaponTrace")
    ev = lib.add_animation_notify_state_event(m, "Trace", start, end - start, trace_cls)
    try: ev.set_editor_property('trace_hand', unreal.MDTraceHandOverride.BOTH)
    except Exception as ex: P("  trace_hand ERR %s" % ex)
    # The skew warp redistributes the root motion INSIDE its window so the window ends on the warp point, and any
    # authored travel after the window plays unwarped. A window that only covered the wind-up (0..trace start) let the
    # whole strike travel carry the enemy straight through its target. The window now runs to the END of the trace
    # window: the claws reach the warp point while they are live; the authored follow-through after that (printed
    # below) only presses against the target's capsule, as the player's own warped combos do.
    warp_end = min(d["len"] - 0.05, end)
    # The baked claw clips finish their travel (ghoulclaw.py VARIANTS) well before the trace window ends. A skew warp that
    # keeps running after the enemy has reached the warp point faces a zero-length direction and can flip the actor 180
    # degrees, so the window stops just before the authored travel does (the last few cm play unwarped).
    warp_end = min(warp_end, WARP_END.get(src.get_name(), warp_end))
    follow_through = travel_after(d, warp_end)
    warp_cls = unreal.load_class(None, "/Script/MotionWarping.AnimNotifyState_MotionWarping")
    wv = lib.add_animation_notify_state_event(m, "Warp", 0.0, warp_end, warp_cls)
    info = make_modifier(wv)
    m.modify(); save(path)
    P("  %s <- %s: trace %.2f..%.2f sockets %s warp 0..%.2f (%s), authored follow-through after the warp %.0f cm" % (
        name, src.get_name(), start, end, sockets, warp_end, info, follow_through))
    return unreal.load_asset(path), sockets

def s_montages():
    swipe, lunge = pick_attacks(analysis)
    P("  swipe = %s" % swipe); P("  lunge = %s" % lunge)
    res = {}
    for name, src in (("AM_Ghoul_Swipe", swipe), ("AM_Ghoul_Lunge", lunge)):
        if src is None: continue
        d = dict(analysis)[src]
        m, sockets = build_montage(name, src, d)
        if m: res[name] = (m, sockets, d)
    return res
montages = sec("Montages", s_montages) or {}

# ---------------------------------------------------------------- 6. Archetype + Blueprint
def s_archetype():
    da = unreal.load_asset("/Game/Data/Enemies/DA_Archetype_Ghoul")
    attacks = []
    # The Lunge is disabled in the shipped archetype (2026-10-28, see ghoullunge.py): re-running this script must not bring it back.
    specs = [("Swipe", "AM_Ghoul_Swipe", 170.0, 0.8, 0.8, 1.0, 15.0, 0.0, 180.0)]
    # ("Lunge", "AM_Ghoul_Lunge", 450.0, 0.9, 1.5, 1.3, 25.0, 350.0, 520.0)  # re-enable with Scripts/Python/ghoullunge.py (ENABLE = True)
    for nm, mn, rng, tell, cd, dmg, poise, kb, warp in specs:
        if mn not in montages: continue
        m, sockets, d = montages[mn]
        a = unreal.MDEnemyAttack()
        a.set_editor_property('name', nm); a.set_editor_property('montage', m)
        a.set_editor_property('range', rng); a.set_editor_property('tell_time', tell); a.set_editor_property('cooldown', cd)
        a.set_editor_property('trace_sockets', sockets or ["hand_r"])
        a.set_editor_property('claw_length', 35.0); a.set_editor_property('claw_back', 10.0)
        step = a.get_editor_property('step')
        step.set_editor_property('damage_mult', dmg); step.set_editor_property('poise_damage', poise)
        step.set_editor_property('reaction', unreal.MDHitReaction.FLINCH)
        step.set_editor_property('knockback_strength', kb); step.set_editor_property('hit_stop_frames', 4)
        step.set_editor_property('warp_range', warp); step.set_editor_property('warp_stop_distance', 90.0)
        step.set_editor_property('root_motion_scale', 1.0)
        a.set_editor_property('step', step)
        attacks.append(a)
    da.set_editor_property('attacks', attacks)
    ai = da.get_editor_property('ai_params'); ai.set_editor_property('threat_range', 450.0); da.set_editor_property('ai_params', ai)
    da.modify(); save("/Game/Data/Enemies/DA_Archetype_Ghoul")
    da = unreal.load_asset("/Game/Data/Enemies/DA_Archetype_Ghoul")
    P("  attacks now: %s" % [(str(x.name), x.montage.get_name() if x.montage else None, x.range, [str(s) for s in x.trace_sockets]) for x in da.get_editor_property('attacks')])
sec("Archetype", s_archetype)

# ECollisionResponse is exposed as unreal.CollisionResponseType
CR_IGNORE = unreal.CollisionResponseType.ECR_IGNORE
CR_BLOCK = unreal.CollisionResponseType.ECR_BLOCK

def s_blueprint():
    bp = unreal.load_asset("/Game/Blueprints/Enemies/BP_Enemy_Ghoul")
    cdo = unreal.get_default_object(bp.generated_class())
    mesh = cdo.get_editor_property('mesh')
    if ghoul_mesh is None:
        raise RuntimeError("no SK_Ghoul: leaving the Blueprint mesh alone")
    mesh.set_skeletal_mesh_asset(ghoul_mesh)
    if abp:
        mesh.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT)
        mesh.set_anim_instance_class(abp.generated_class())
        P("  anim class ABP_Ghoul")
    else:
        idle = unreal.load_asset(ANIM_DIR + "/A_Ghoul_Idle")
        if idle:
            ad = mesh.get_editor_property('animation_data')
            ad.set_editor_property('anim_to_play', idle)
            mesh.set_editor_property('animation_data', ad)
        P("  ABP_Ghoul not built yet: single-node A_Ghoul_Idle for now")
    # The BP's serialized collision (profile Custom, everything blocking) overrides the C++ constructor, so the camera
    # channel is cleared on the BP's components as well: the player's camera probe must pass through enemies. Visibility
    # stays blocking for the weapon sweeps. (AMDEnemyCharacter::PostInitializeComponents enforces the same at runtime.)
    for comp in (cdo.get_editor_property('capsule_component'), cdo.get_editor_property('mesh')):
        comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, CR_IGNORE)
        comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY, CR_BLOCK)
    bp.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    save("/Game/Blueprints/Enemies/BP_Enemy_Ghoul")
    cdo = unreal.get_default_object(unreal.load_asset("/Game/Blueprints/Enemies/BP_Enemy_Ghoul").generated_class())
    P("  BP mesh now: %s" % cdo.get_editor_property('mesh').skeletal_mesh_asset.get_path_name())
    for comp in (cdo.get_editor_property('capsule_component'), cdo.get_editor_property('mesh')):
        P("  %s: Camera %s, Visibility %s" % (comp.get_name(), comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA), comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY)))
sec("Blueprint", s_blueprint)

open(out, "w").write("\n".join(L))
