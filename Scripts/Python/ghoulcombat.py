import unreal, traceback
# Ghoul attacks (the replacement for the Swipe / Lunge placeholders). Builds the three montages from the
# Animations/Enemies/Ghoul/Combat root-motion clips and fills DA_Archetype_Ghoul (attacks + nimble movement numbers).
# Run with the editor closed (commandlet; Scripts\RunPy.ps1 Scripts/Python/ghoulcombat.py). Idempotent.
#   AM_Ghoul_ScratchCombo  three swipes, one MD Weapon Trace window per swipe (the trace's hit-once list resets at each window)
#   AM_Ghoul_ClawHeavy     one heavy claw
#   AM_Ghoul_LeapScratch   gap closer: leap, slam and rake
# Each montage: MDAN_Tell at the wind-up start, Motion Warping "AttackTarget" window, 0.12 s blend in/out.
# Trace windows sit on the hit frames of Robert's animation notes (30 fps): combo swipes at frames 10 / 21 / 33, claw 12-14,
# leap slam 27-28 with the rake to 31. Report: Saved/ClaudeDump/ghoulcombat.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulcombat.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
def sec(name, fn):
    try:
        P("== " + name); return fn()
    except Exception:
        P("EXC in %s: %s" % (name, traceback.format_exc()))

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.AnimationLibrary
SK_PATH = "/Game/Characters/UE5_Mannequins/Meshes/SK_Mannequin"
COMBAT = "/Game/Animations/Enemies/Ghoul/Combat"
WARP_TARGET = "AttackTarget"
BLEND = 0.12

# name, source clip, trace windows (start, end), warp window end, tell notify time
SPECS = [
    ("AM_Ghoul_ScratchCombo", "A_Ghoul_ScratchCombo_RM", [(0.25, 0.42), (0.62, 0.79), (1.02, 1.19)], 1.20),
    ("AM_Ghoul_ClawHeavy",    "A_Ghoul_ClawHeavy_RM",    [(0.34, 0.55)], 0.60),
    ("AM_Ghoul_LeapScratch",  "A_Ghoul_LeapScratch_RM",  [(0.84, 1.08)], 1.00),
]

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
        # DEFAULT: turn to the warp target's rotation (UMDEnemyAttackComponent::ApplyWarp sets it to the facing yaw at swing start).
        try: obj.set_editor_property("rotation_type", unreal.MotionWarpRotationType.DEFAULT)
        except Exception: pass
        state.set_editor_property(prop, obj)
        return prop
    return None

def build(name, src_name, windows, warp_end):
    path = COMBAT + "/" + name
    src = unreal.load_asset(COMBAT + "/" + src_name)
    sk = unreal.load_asset(SK_PATH)
    if src is None:
        P("  MISSING source " + src_name); return None
    if not EAL.does_asset_exist(path):
        f = unreal.AnimMontageFactory()
        f.set_editor_property('target_skeleton', sk)
        f.set_editor_property('source_animation', src)
        AT.create_asset(name, COMBAT, unreal.AnimMontage, f)
    m = unreal.load_asset(path)
    tracks = m.get_editor_property('slot_anim_tracks')
    segs = tracks[0].get_editor_property('anim_track').get_editor_property('anim_segments') if len(tracks) else []
    if len(segs) == 0 or segs[0].get_editor_property('anim_reference') is None:
        seg = unreal.AnimSegment(); seg.set_editor_property('anim_reference', src)
        seg.set_editor_property('anim_end_time', src.get_play_length())
        at = unreal.AnimTrack(); at.set_editor_property('anim_segments', [seg])
        st = unreal.SlotAnimationTrack(); st.set_editor_property('slot_name', 'DefaultSlot'); st.set_editor_property('anim_track', at)
        m.set_editor_property('slot_anim_tracks', [st])
    try:
        mode = unreal.MontageBlendMode.INERTIALIZATION
        m.set_editor_property('blend_mode_in', mode); m.set_editor_property('blend_mode_out', mode)
    except Exception as ex: P("  blend mode ERR %s" % ex)
    for prop in ('blend_in', 'blend_out'):
        try:
            b = m.get_editor_property(prop)
            b.set_editor_property('blend_time', BLEND)
            m.set_editor_property(prop, b)
        except Exception as ex: P("  %s ERR %s" % (prop, ex))
    # rebuild the notifies
    for nm in ("MD Weapon Trace", "AnimNotifyState_MotionWarping", "MotionWarping", "MD Tell", "MDAN_Tell"):
        try: lib.remove_animation_notify_events_by_name(m, nm)
        except Exception: pass
    names = [str(x) for x in lib.get_animation_notify_track_names(m)]
    for tn, col in (("Tell", unreal.LinearColor(1.0, 0.85, 0.2, 1.0)), ("Trace", unreal.LinearColor(0.9, 0.2, 0.2, 1.0)), ("Warp", unreal.LinearColor(1.0, 0.6, 0.1, 1.0))):
        if tn not in names: lib.add_animation_notify_track(m, tn, col)
    lib.add_animation_notify_event(m, "Tell", 0.02, unreal.load_class(None, "/Script/MazeDemons.MDAN_Tell"))
    trace_cls = unreal.load_class(None, "/Script/MazeDemons.MDANS_WeaponTrace")
    for (a, b) in windows:
        ev = lib.add_animation_notify_state_event(m, "Trace", a, b - a, trace_cls)
        try: ev.set_editor_property('trace_hand', unreal.MDTraceHandOverride.BOTH)
        except Exception as ex: P("  trace_hand ERR %s" % ex)
    warp_cls = unreal.load_class(None, "/Script/MotionWarping.AnimNotifyState_MotionWarping")
    wv = lib.add_animation_notify_state_event(m, "Warp", 0.0, warp_end, warp_cls)
    info = make_modifier(wv)
    m.modify(); EAL.save_asset(path, only_if_is_dirty=False)
    P("  %s <- %s len %.2f: tell 0.02, traces %s, warp 0..%.2f (%s), blend %.2f" % (name, src_name, src.get_play_length(), windows, warp_end, info, BLEND))
    return unreal.load_asset(path)

def s_montages():
    res = {}
    for name, src, wins, warp_end in SPECS:
        m = build(name, src, wins, warp_end)
        if m: res[name] = m
    return res
montages = sec("Montages", s_montages) or {}

# name, montage, range, min_range, tell, cooldown, damage x, poise, knockback, warp range, warp stop, weight
ATTACKS = [
    ("ScratchCombo", "AM_Ghoul_ScratchCombo", 200.0,   0.0, 0.40, 0.5, 0.55,  8.0,   0.0, 120.0, 70.0, 3.0),
    ("ClawHeavy",    "AM_Ghoul_ClawHeavy",    190.0,   0.0, 0.55, 1.2, 1.40, 20.0, 150.0, 100.0, 80.0, 1.0),
    # Leap: a gap closer, 300-480 cm. The tell is the whole authored crouch + jump (0.85 s to the slam): slowing the montage to
    # a shorter "tell" would play the airborne part in slow motion, so TellTime equals the slam time (play as authored).
    ("LeapScratch",  "AM_Ghoul_LeapScratch",  480.0, 300.0, 0.85, 4.0, 1.20, 15.0, 100.0, 480.0, 90.0, 1.0),
]

def s_archetype():
    da = unreal.load_asset("/Game/Data/Enemies/DA_Archetype_Ghoul")
    attacks = []
    for nm, mn, rng, mnr, tell, cd, dmg, poise, kb, warp, stop, weight in ATTACKS:
        m = montages.get(mn)
        if m is None:
            P("  no montage for %s" % nm); continue
        a = unreal.MDEnemyAttack()
        a.set_editor_property('name', nm); a.set_editor_property('montage', m)
        a.set_editor_property('range', rng); a.set_editor_property('min_range', mnr)
        a.set_editor_property('tell_time', tell); a.set_editor_property('cooldown', cd)
        a.set_editor_property('selection_weight', weight)
        a.set_editor_property('trace_sockets', ["hand_r", "hand_l"])
        a.set_editor_property('claw_length', 35.0); a.set_editor_property('claw_back', 10.0)
        step = a.get_editor_property('step')
        step.set_editor_property('damage_mult', dmg); step.set_editor_property('poise_damage', poise)
        step.set_editor_property('reaction', unreal.MDHitReaction.FLINCH)
        step.set_editor_property('knockback_strength', kb); step.set_editor_property('hit_stop_frames', 3)
        step.set_editor_property('warp_range', warp); step.set_editor_property('warp_stop_distance', stop)
        step.set_editor_property('root_motion_scale', 1.0)
        a.set_editor_property('step', step)
        attacks.append(a)
    da.set_editor_property('attacks', attacks)
    # Quick and nimble.
    da.set_editor_property('move_speed', 520.0)
    da.set_editor_property('walk_speed', 150.0)
    da.set_editor_property('max_acceleration', 2400.0)
    da.set_editor_property('braking_deceleration', 2000.0)
    da.set_editor_property('rotation_yaw_rate', 720.0)
    da.set_editor_property('engage_face_range', 600.0)
    ai = da.get_editor_property('ai_params'); ai.set_editor_property('threat_range', 520.0); da.set_editor_property('ai_params', ai)
    for prop, cue in (("tell_sound", "11_Menacing_Growl_Cue"), ("threat_sting", "10_Attack_Roar_Cue")):
        snd = unreal.load_asset("/Game/Fab/MonsterRoarsAndGrowls/cues/" + cue)
        if snd: da.set_editor_property(prop, snd)
    da.set_editor_property("tell_volume", 0.3); da.set_editor_property("threat_sting_volume", 0.7)
    da.modify(); EAL.save_asset("/Game/Data/Enemies/DA_Archetype_Ghoul", only_if_is_dirty=False)
    da = unreal.load_asset("/Game/Data/Enemies/DA_Archetype_Ghoul")
    P("  attacks now: %s" % [(str(x.name), x.montage.get_name() if x.montage else None, x.range, x.min_range, x.selection_weight) for x in da.get_editor_property('attacks')])
    P("  speeds: run %s walk %s accel %s brake %s turn %s engage %s" % tuple(da.get_editor_property(p) for p in
      ('move_speed', 'walk_speed', 'max_acceleration', 'braking_deceleration', 'rotation_yaw_rate', 'engage_face_range')))
sec("Archetype", s_archetype)
open(out, "w").write("\n".join(L))
