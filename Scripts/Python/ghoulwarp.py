import unreal, traceback
# Ghoul attack montages: set every Motion Warping window's rotation type to DEFAULT (turn to the warp target's rotation,
# which UMDEnemyAttackComponent::ApplyWarp sets to the facing yaw at swing start). FACING recomputed a yaw from the
# attacker's own position each tick and degenerated once the skew warp had placed the capsule on the warp point.
# Touches only AM_Ghoul_Swipe / AM_Ghoul_Lunge. Run with the editor closed (commandlet). Idempotent.
# Report: Saved/ClaudeDump/ghoulwarp.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulwarp.txt"
L = []
def P(s):
    L.append(str(s)); print(s)

EAL = unreal.EditorAssetLibrary
def g(o, *names):
    for n in names:
        try: return o.get_editor_property(n)
        except Exception: continue
    return "?"
MONTAGES = ["/Game/Animations/Enemies/Ghoul/AM_Ghoul_Swipe", "/Game/Animations/Enemies/Ghoul/AM_Ghoul_Lunge"]

def fix(path):
    m = unreal.load_asset(path)
    if m is None:
        P("  MISSING " + path); return
    changed = 0; seen = 0
    for ev in unreal.AnimationLibrary.get_animation_notify_events(m):
        state = ev.get_editor_property("notify_state_class")
        if state is None or "MotionWarping" not in state.get_class().get_name():
            continue
        for prop in ("root_motion_modifier_config", "root_motion_modifier"):
            try: mod = state.get_editor_property(prop)
            except Exception: continue
            if mod is None: continue
            seen += 1
            before = mod.get_editor_property("rotation_type")
            if before != unreal.MotionWarpRotationType.DEFAULT:
                m.modify(); mod.modify()
                mod.set_editor_property("rotation_type", unreal.MotionWarpRotationType.DEFAULT); changed += 1
            t0 = unreal.AnimationLibrary.get_anim_notify_event_trigger_time(ev)
            t1 = t0 + unreal.AnimationLibrary.get_anim_notify_event_duration(ev)
            P("  %s window %.2f..%.2f: rotation_type %s -> %s, warp_rotation %s, warp_translation %s, ignore_z %s, target %s" % (
                m.get_name(), t0, t1, before, mod.get_editor_property("rotation_type"), g(mod, "warp_rotation", "b_warp_rotation"),
                g(mod, "warp_translation", "b_warp_translation"), g(mod, "ignore_z_axis", "b_ignore_z_axis"), g(mod, "warp_target_name")))
            break
    if changed:
        P("  saved %s -> %s" % (path, EAL.save_asset(path, only_if_is_dirty=False)))
    P("  %s: %d warp window(s), %d changed" % (m.get_name(), seen, changed))

try:
    for p in MONTAGES:
        P("== " + p); fix(p)
except Exception:
    P("EXC: " + traceback.format_exc())
open(out, "w").write("\n".join(L))
