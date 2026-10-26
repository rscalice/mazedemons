import unreal, traceback
# Root-motion attacks. For each combat montage:
#   1. swaps the slot segment's animation for the root-motion source (only when the lengths match within 0.02 s; the source
#      must have Enable Root Motion on; it is only referenced, never modified or saved),
#   2. sets BlendModeIn / BlendModeOut to Inertialization (times kept),
#   3. adds a Motion Warping notify state (SkewWarp, "AttackTarget") on a "Warp" track from 0 to the first MD Weapon Trace window.
# Idempotent: every step checks the current state first and the montage is saved only when something changed.
# Run with the editor closed (Scripts\ApplyRootMotionAttacks.ps1). Report: rmswap.txt
out = r"C:\Users\Robert Scalice\Documents\Unreal Projects\mazedemons\Saved\ClaudeDump\rmswap.txt"
L = []
def P(s): L.append(str(s))

lib = unreal.AnimationLibrary
COMBAT = "/Game/Animations/Combat/"
DS = "/Game/Animations/WeaponStances/TwinSwordExpansion/Attacks/RM/UE5_TwinSword_Attack"
DGS = "/Game/Animations/WeaponStances/DynamicGreatSwordAnims/Animations/RootMotion/Manny_UE5/ComboAttack/AS_DGS_ComboA"
EGS = "/Game/Animations/WeaponStances/EssentialGreatswordAnimation/"
JOBS = [
    ("DualSword/AM_DS_Light1", DS + "34"), ("DualSword/AM_DS_Light2", DS + "36"), ("DualSword/AM_DS_Light3", DS + "14"),
    ("DualSword/AM_DS_Light4", DS + "20"), ("DualSword/AM_DS_Light5", DS + "44"), ("DualSword/AM_DS_Heavy", DS + "42"),
    ("DualSword/AM_DS_Charged", DS + "52"),
    ("Greatsword/AM_GS_Light1", DGS + "1"), ("Greatsword/AM_GS_Light2", DGS + "2"), ("Greatsword/AM_GS_Light3", DGS + "3"),
    ("Greatsword/AM_GS_Heavy", EGS + "2H_Attack/UE5_2H_Attack_02_Seq"),
    ("Greatsword/AM_GS_Charged", EGS + "2H_Attack_Charge/UE5_2H_Attack_Charge_02_Seq"),
]
LEN_TOL = 0.02
WARP_TARGET = "AttackTarget"
WARP_TRACK = "Warp"

def try_set(obj, names, value):
    # first property name that exists wins
    for n in names:
        try:
            obj.set_editor_property(n, value)
            return n
        except Exception:
            continue
    return None

def enum_val(enum_cls, *names):
    for n in names:
        if hasattr(enum_cls, n):
            return getattr(enum_cls, n)
    return None

def get_skel(a):
    try: return a.get_editor_property('skeleton')
    except Exception: return None

def swap_animation(path, m, src_path):
    """Returns (changed, ok)."""
    src = unreal.load_asset(src_path)
    if src is None:
        P("  SKIP swap: source not found " + src_path); return False, False
    if not isinstance(src, unreal.AnimSequenceBase):
        P("  SKIP swap: source is not an animation sequence " + src_path); return False, False
    try:
        rm = src.get_editor_property('enable_root_motion')
    except Exception:
        rm = None
    if rm is not True:
        P("  SKIP swap: source has Enable Root Motion = %s (not modified): %s" % (rm, src_path)); return False, False
    tracks = m.get_editor_property('slot_anim_tracks')
    if len(tracks) != 1:
        P("  SKIP swap: montage has %d slot tracks" % len(tracks)); return False, False
    track = tracks[0]
    atrack = track.get_editor_property('anim_track')
    segs = list(atrack.get_editor_property('anim_segments'))
    if len(segs) != 1:
        P("  SKIP swap: slot track has %d segments" % len(segs)); return False, False
    seg = segs[0]
    old = seg.get_editor_property('anim_reference')
    if old is not None and old.get_path_name().split('.')[0] == src_path:
        P("  swap: already uses " + src.get_name()); return False, True
    new_len = src.get_play_length()
    old_len = old.get_play_length() if old else m.get_play_length()
    if abs(new_len - old_len) > LEN_TOL or abs(new_len - m.get_play_length()) > LEN_TOL * 5:
        P("  SKIP swap: length mismatch old=%.3f new=%.3f montage=%.3f (%s)" % (old_len, new_len, m.get_play_length(), src_path)); return False, False
    so, sn = get_skel(old), get_skel(src)
    if so is not None and sn is not None and so.get_path_name() != sn.get_path_name():
        P("  SKIP swap: skeleton mismatch %s vs %s" % (so.get_name(), sn.get_name())); return False, False
    seg.set_editor_property('anim_reference', src)
    atrack.set_editor_property('anim_segments', [seg])
    track.set_editor_property('anim_track', atrack)
    m.set_editor_property('slot_anim_tracks', [track])
    # read back: the nested struct copies must have been written through
    chk = m.get_editor_property('slot_anim_tracks')[0].get_editor_property('anim_track').get_editor_property('anim_segments')[0].get_editor_property('anim_reference')
    if chk is None or chk.get_path_name().split('.')[0] != src_path:
        P("  FAIL swap: read-back shows %s (struct write did not stick)" % (chk.get_name() if chk else None)); return False, False
    P("  swap: %s (%.3fs) -> %s (%.3fs) rm=True" % (old.get_name() if old else None, old_len, src.get_name(), new_len))
    return True, True

def set_inertialization(m):
    mode = enum_val(unreal.MontageBlendMode, "INERTIALIZATION")
    if mode is None:
        P("  FAIL blend: unreal.MontageBlendMode.INERTIALIZATION missing"); return False
    changed = False
    for prop in ("blend_mode_in", "blend_mode_out"):
        try:
            cur = m.get_editor_property(prop)
            if cur != mode:
                m.set_editor_property(prop, mode); changed = True
                P("  blend: %s %s -> INERTIALIZATION" % (prop, cur))
        except Exception as ex:
            P("  FAIL blend %s: %s" % (prop, ex))
    try:
        bi, bo = m.get_editor_property('blend_in'), m.get_editor_property('blend_out')
        P("  blend times kept: in=%s out=%s" % (bi.get_editor_property('blend_time'), bo.get_editor_property('blend_time')))
    except Exception:
        pass
    return changed

def make_modifier(state):
    """Fills the SkewWarp modifier on a Motion Warping notify state (5.x config object, or the older modifier object)."""
    for prop, cls_name in (("root_motion_modifier_config", "RootMotionModifierConfig_SkewWarp"), ("root_motion_modifier", "RootMotionModifier_SkewWarp")):
        try:
            state.get_editor_property(prop)
        except Exception:
            continue
        cls = unreal.load_class(None, "/Script/MotionWarping." + cls_name)
        if cls is None:
            continue
        obj = unreal.new_object(cls, outer=state)
        got = []
        for names, val in ((["warp_target_name"], WARP_TARGET), (["warp_translation", "b_warp_translation"], True),
                           (["ignore_z_axis", "b_ignore_z_axis"], True), (["warp_rotation", "b_warp_rotation"], True),
                           (["rotation_type"], enum_val(unreal.MotionWarpRotationType, "FACING"))):
            n = try_set(obj, names, val)
            got.append("%s=%s" % (n, val))
        state.set_editor_property(prop, obj)
        return prop + ":" + cls_name + " " + ", ".join(got)
    return None

def modifier_ok(state):
    for prop in ("root_motion_modifier_config", "root_motion_modifier"):
        try:
            o = state.get_editor_property(prop)
        except Exception:
            continue
        if o is None or "SkewWarp" not in o.get_class().get_name():
            continue
        try:
            if str(o.get_editor_property('warp_target_name')) == WARP_TARGET:
                return True
        except Exception:
            pass
    return False

def add_warp_notify(m):
    warp_cls = unreal.load_class(None, "/Script/MotionWarping.AnimNotifyState_MotionWarping")
    if warp_cls is None:
        P("  FAIL warp: AnimNotifyState_MotionWarping class not found"); return False
    evs = lib.get_animation_notify_events(m)
    first = None
    existing_warps = []
    for e in evs:
        st = e.get_editor_property('notify_state_class')
        if st is None:
            continue
        if str(e.get_editor_property('notify_name')) == "MD Weapon Trace":
            t = lib.get_anim_notify_event_trigger_time(e)
            first = t if first is None else min(first, t)
        if st.get_class() == warp_cls:
            existing_warps.append((e, st))
    if first is None:
        P("  SKIP warp: no MD Weapon Trace window"); return False
    if first < 0.05:
        P("  SKIP warp: first trace window starts at %.3f (no approach)" % first); return False
    if len(existing_warps) == 1:
        e, st = existing_warps[0]
        if abs(lib.get_anim_notify_event_trigger_time(e)) < 0.001 and abs(lib.get_anim_notify_event_duration(e) - first) < 0.005 and modifier_ok(st):
            P("  warp: already present (0 -> %.3f)" % first); return False
    for name in set(str(e.get_editor_property('notify_name')) for e, st in existing_warps):
        try: lib.remove_animation_notify_events_by_name(m, name)
        except Exception as ex: P("  remove old warp ERR " + str(ex))
    names = [str(x) for x in lib.get_animation_notify_track_names(m)]
    if WARP_TRACK not in names:
        lib.add_animation_notify_track(m, WARP_TRACK, unreal.LinearColor(1.0, 0.6, 0.1, 1.0))
    st = lib.add_animation_notify_state_event(m, WARP_TRACK, 0.0, first, warp_cls)
    info = make_modifier(st)
    if info is None:
        P("  FAIL warp: no modifier property found on the notify state; props=" + str([p for p in dir(st) if not p.startswith('_') and 'modifier' in p.lower()]))
    P("  warp: Motion Warping 0.000 -> %.3f on '%s' (%s)" % (first, WARP_TRACK, info))
    return True

try:
    ok_count = 0
    for mn, src_path in JOBS:
        path = COMBAT + mn
        P("== " + path)
        if not path.startswith(COMBAT):
            P("  SKIP: outside combat folder"); continue
        m = unreal.load_asset(path)
        if m is None:
            P("  SKIP: montage not found"); continue
        try:
            c1, ok = swap_animation(path, m, src_path)
            c2 = set_inertialization(m)
            c3 = add_warp_notify(m)
            if c1 or c2 or c3:
                m.modify()
                saved = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
                P("  saved %s -> %s" % (path, saved))
            else:
                P("  unchanged (not saved)")
            ok_count += 1 if ok else 0
        except Exception:
            P("  EXC " + traceback.format_exc())
    P("DONE: %d of %d montages have the root-motion source" % (ok_count, len(JOBS)))
except Exception:
    P("EXC " + traceback.format_exc())
txt = "\n".join(L)
open(out, "w").write(txt)
print(txt)
