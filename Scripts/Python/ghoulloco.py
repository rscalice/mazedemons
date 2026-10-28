import unreal, traceback
# Read-only: root bone translation and pelvis world position over the ghoul locomotion clips (idle / walk / run), plus the
# ABP's root motion mode. Looks for root / pelvis travel baked into the loops (a mesh that runs away from its capsule).
# Report: Saved/ClaudeDump/ghoulloco.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulloco.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
D = "/Game/Animations/Enemies/Ghoul/"
try:
    E = unreal.AnimPoseExtensions
    for name in ("A_Ghoul_Idle", "A_Ghoul_Walk", "A_Ghoul_Run", "A_Ghoul_Swipe_RM"):
        a = unreal.load_asset(D + name)
        if not a:
            P("== %s MISSING" % name); continue
        P("== %s len %.2f root_motion=%s lock=%s force=%s additive=%s" % (name, a.get_play_length(), a.get_editor_property('enable_root_motion'),
            a.get_editor_property('root_motion_root_lock'), a.get_editor_property('force_root_lock'), a.get_editor_property('additive_anim_type')))
        dm = a.data_model_interface
        names = [str(n) for n in dm.get_bone_track_names()]
        P("  tracks %d first %s" % (len(names), names[:3]))
        opts = unreal.AnimPoseEvaluationOptions(); opts.set_editor_property('extract_root_motion', False)
        n = max(1, int(a.get_play_length() * 10))
        for i in range(0, n + 1):
            t = min(i / 10.0, a.get_play_length())
            pose = E.get_anim_pose_at_time(a, t, opts)
            r = E.get_bone_pose(pose, 'root', unreal.AnimPoseSpaces.LOCAL)
            pl = E.get_bone_pose(pose, 'pelvis', unreal.AnimPoseSpaces.LOCAL)
            pw = E.get_bone_pose(pose, 'pelvis', unreal.AnimPoseSpaces.WORLD)
            rr = r.rotation.rotator()
            P("  t=%.1f root loc (%.0f,%.0f,%.0f) rot P%.0f Y%.0f R%.0f | pelvis local (%.0f,%.0f,%.0f) | pelvis comp (%.0f,%.0f,%.0f)" % (
                t, r.translation.x, r.translation.y, r.translation.z, rr.pitch, rr.yaw, rr.roll,
                pl.translation.x, pl.translation.y, pl.translation.z, pw.translation.x, pw.translation.y, pw.translation.z))
    abp = unreal.load_asset(D + "ABP_Ghoul")
    cdo = unreal.get_default_object(abp.generated_class())
    P("ABP_Ghoul root_motion_mode=%s" % cdo.get_editor_property('root_motion_mode'))
    bs = unreal.load_asset(D + "BS_Ghoul_Locomotion")
    P("BS samples: %s" % [ (str(s.animation.get_name()), s.sample_value) for s in bs.get_editor_property('sample_data') ])
    sk = unreal.load_asset("/Game/Characters/Enemies/Ghoul/SK_Ghoul")
    P("SK_Ghoul skeleton %s" % sk.skeleton.get_name())
except Exception:
    P("EXC: " + traceback.format_exc())
open(out, "w").write("\n".join(L))
