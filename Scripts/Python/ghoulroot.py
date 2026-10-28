import unreal, traceback
# Read-only: per-frame root bone transform (local) and extracted root-motion deltas of the ghoul claw clips.
# Report: Saved/ClaudeDump/ghoulroot.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulroot.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
D = "/Game/Animations/Enemies/Ghoul/"
try:
    E = unreal.AnimPoseExtensions
    for name in ("A_Ghoul_Claw", "A_Ghoul_Swipe_RM", "A_Ghoul_Lunge_RM"):
        a = unreal.load_asset(D + name)
        P("== %s len %.2f root_motion=%s lock=%s force=%s" % (name, a.get_play_length(), a.get_editor_property('enable_root_motion'),
            a.get_editor_property('root_motion_root_lock'), a.get_editor_property('force_root_lock')))
        dm = a.data_model_interface
        names = [str(n) for n in dm.get_bone_track_names()]
        P("  tracks %d, has root track %s" % (len(names), 'root' in names))
        opts = unreal.AnimPoseEvaluationOptions(); opts.set_editor_property('extract_root_motion', False)
        n = int(a.get_play_length() * 24)
        for i in range(0, n + 1, 2):
            t = min(i / 24.0, a.get_play_length())
            pose = E.get_anim_pose_at_time(a, t, opts)
            r = E.get_bone_pose(pose, 'root', unreal.AnimPoseSpaces.LOCAL)
            pw = E.get_bone_pose(pose, 'pelvis', unreal.AnimPoseSpaces.WORLD)
            rr = r.rotation.rotator(); pr = pw.rotation.rotator()
            P("  t=%.2f root loc (%.0f,%.0f,%.0f) rot P%.1f Y%.1f R%.1f | pelvis(world) loc (%.0f,%.0f,%.0f) rot P%.1f Y%.1f R%.1f" % (
                t, r.translation.x, r.translation.y, r.translation.z, rr.pitch, rr.yaw, rr.roll,
                pw.translation.x, pw.translation.y, pw.translation.z, pr.pitch, pr.yaw, pr.roll))
except Exception:
    P("EXC: " + traceback.format_exc())
open(out, "w").write("\n".join(L))
