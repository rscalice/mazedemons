import unreal, traceback, math
# Makes the ghoul locomotion loops in-place. The retargeted A_Ghoul_Walk / A_Ghoul_Run carry their travel on the PELVIS
# (6 m / 8.8 m per loop) with a flat root track, so root motion (which only reads the root) extracts nothing and the mesh
# runs 6-9 m ahead of its capsule every loop and snaps back: the "jumping around" of the 2026-10-28 playtest. Each clip is
# first copied once to <name>_Travel (the untouched source, also what the script rebuilds from, so it is idempotent), then
# the linear XY drift of the pelvis is removed key by key (the bob, the rotation and every other bone are untouched).
# Report: Saved/ClaudeDump/ghoulinplace.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulinplace.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
D = "/Game/Animations/Enemies/Ghoul"
EAL = unreal.EditorAssetLibrary
E = unreal.AnimPoseExtensions
try:
    for name in ("A_Ghoul_Walk", "A_Ghoul_Run"):
        dst = D + "/" + name
        src_path = D + "/" + name + "_Travel"
        if not EAL.does_asset_exist(src_path):
            if not EAL.duplicate_asset(dst, src_path):
                P("%s: could not keep the travel copy" % name); continue
            EAL.save_asset(src_path, only_if_is_dirty=False)
            P("%s: travel copy kept as %s" % (name, src_path))
        src = unreal.load_asset(src_path)
        a = unreal.load_asset(dst)
        dm = a.data_model_interface
        fr = dm.get_frame_rate(); fps = float(fr.numerator) / float(fr.denominator)
        nkeys = dm.get_number_of_keys()
        opts = unreal.AnimPoseEvaluationOptions(); opts.set_editor_property('extract_root_motion', False)
        pos, rot, sca = [], [], []
        first = None; last = None
        samples = []
        for i in range(nkeys):
            t = min(i / fps, src.get_play_length())
            pel = E.get_bone_pose(E.get_anim_pose_at_time(src, t, opts), 'pelvis', unreal.AnimPoseSpaces.LOCAL)
            samples.append(pel)
        first = samples[0].translation; last = samples[-1].translation
        drift = unreal.Vector(last.x - first.x, last.y - first.y, 0.0)
        for i, pel in enumerate(samples):
            u = i / float(max(nkeys - 1, 1))
            p = pel.translation
            pos.append(unreal.Vector(p.x - drift.x * u, p.y - drift.y * u, p.z))
            rot.append(pel.rotation); sca.append(pel.scale3d)
        ctl = a.controller
        ctl.open_bracket(unreal.Text("Make ghoul locomotion in-place"), True)
        ctl.set_bone_track_keys('pelvis', pos, rot, sca, True)
        ctl.close_bracket(True)
        EAL.save_asset(dst, only_if_is_dirty=False)
        a = unreal.load_asset(dst)
        q0 = E.get_bone_pose(E.get_anim_pose_at_time(a, 0.0, opts), 'pelvis', unreal.AnimPoseSpaces.LOCAL).translation
        q1 = E.get_bone_pose(E.get_anim_pose_at_time(a, a.get_play_length(), opts), 'pelvis', unreal.AnimPoseSpaces.LOCAL).translation
        P("%s: %d keys @ %.0f fps, removed pelvis drift (%.0f, %.0f) cm; pelvis now (%.0f,%.0f,%.0f) -> (%.0f,%.0f,%.0f), travel %.1f cm" % (
            name, nkeys, fps, drift.x, drift.y, q0.x, q0.y, q0.z, q1.x, q1.y, q1.z, math.sqrt((q1.x - q0.x) ** 2 + (q1.y - q0.y) ** 2)))
except Exception:
    P("EXC: " + traceback.format_exc())
open(out, "w").write("\n".join(L))
