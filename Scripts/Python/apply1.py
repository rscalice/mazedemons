import unreal, traceback
out = r"C:\Users\Robert Scalice\Documents\Unreal Projects\mazedemons\Saved\ClaudeDump\apply1.txt"
L=[]
def P(s): L.append(str(s))
lib = unreal.AnimationLibrary
cls = unreal.load_class(None, "/Script/NiagaraAnimNotifies.AnimNotifyState_TimedNiagaraEffect")
NS10 = unreal.load_asset("/Game/Fab/SwordTrailVFX/VFX/NS_Trail_10")
NS01 = unreal.load_asset("/Game/Fab/SwordTrailVFX/VFX/NS_Trail_01")
# (montage, {hand-override-or-step-hand})
GS = {"template": NS10, "socket": {"Main": ["TwoHanded"], "Off": ["OffHand"], "Both": ["TwoHanded"]}, "loc": unreal.Vector(0, 0, 20)}
DS = {"template": NS01, "socket": {"Main": ["MainHand"], "Off": ["OffHand"], "Both": ["MainHand", "OffHand"]}, "loc": unreal.Vector(0, 0, 10)}
CFG = [
 ("Greatsword/AM_GS_Light1", GS, "Main"), ("Greatsword/AM_GS_Light2", GS, "Main"), ("Greatsword/AM_GS_Light3", GS, "Main"),
 ("Greatsword/AM_GS_Heavy", GS, "Main"), ("Greatsword/AM_GS_Charged", GS, "Main"),
 ("DualSword/AM_DS_Light1", DS, "Main"), ("DualSword/AM_DS_Light2", DS, "Off"), ("DualSword/AM_DS_Light3", DS, "Main"),
 ("DualSword/AM_DS_Light4", DS, "Main"), ("DualSword/AM_DS_Light5", DS, "Both"), ("DualSword/AM_DS_Heavy", DS, "Both"), ("DualSword/AM_DS_Charged", DS, "Both"),
]
OV = {"MAIN": "Main", "OFF": "Off", "BOTH": "Both"}
TRACKS = {"TwoHanded": "Trail", "MainHand": "Trail Main", "OffHand": "Trail Off"}
try:
    for mn, cfg, stephand in CFG:
        path = "/Game/Animations/Combat/" + mn
        m = lib.get_animation_notify_events and unreal.load_asset(path)
        length = m.get_play_length()
        evs = lib.get_animation_notify_events(m)
        wins = []
        for e in evs:
            st = e.get_editor_property('notify_state_class')
            if st is not None and str(e.get_editor_property('notify_name')) == "MD Weapon Trace":
                ov = str(st.get_editor_property('trace_hand')).split('.')[-1].split(':')[0].strip('<>')
                hand = OV.get(ov, stephand)
                wins.append((lib.get_anim_notify_event_trigger_time(e), lib.get_anim_notify_event_duration(e), hand))
        # remove old timed niagara notifies (idempotent)
        try: lib.remove_animation_notify_events_by_name(m, "TimedNiagaraEffect")
        except Exception as ex: P("  remove ERR " + str(ex))
        existing = [str(x) for x in lib.get_animation_notify_track_names(m)]
        for ts, td, hand in wins:
            start = max(0.0, ts - 0.04)
            end = min(length, ts + td + 0.06)
            dur = max(0.2, end - start)
            if start + dur > length: dur = length - start
            for sock in cfg["socket"][hand]:
                tn = TRACKS[sock]
                if tn not in existing:
                    lib.add_animation_notify_track(m, tn, unreal.LinearColor(0.2, 0.7, 1.0, 1.0))
                    existing.append(tn)
                ev = lib.add_animation_notify_state_event(m, tn, start, dur, cls)
                st = ev
                st.set_editor_property('template', cfg["template"])
                st.set_editor_property('socket_name', sock)
                st.set_editor_property('location_offset', cfg["loc"])
                st.set_editor_property('rotation_offset', unreal.Rotator(0, 0, 0))
                st.set_editor_property('scale', unreal.Vector(1, 1, 1))
                P("%s: %s start=%.3f dur=%.3f socket=%s track=%s tpl=%s loc=%s (trace win %.3f+%.3f hand=%s)" % (mn, "ADD", start, dur, sock, tn, cfg["template"].get_name(), cfg["loc"], ts, td, hand))
        m.modify()
        ok = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        P("  saved %s -> %s" % (path, ok))
except Exception as e:
    P("EXC " + traceback.format_exc())
open(out, "w").write("\n".join(L))
