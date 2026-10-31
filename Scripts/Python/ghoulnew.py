import unreal, traceback
# Read-only: length, root-motion travel, notifies and skeleton of the new ghoul clips (Combat/, MM/) + PSS_Ghoul.
# Report: Saved/ClaudeDump/ghoulnew.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulnew.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
D = "/Game/Animations/Enemies/Ghoul/"
try:
    E = unreal.AnimPoseExtensions
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for sub in ("Combat", "MM", ""):
        for ad in reg.get_assets_by_path(D + sub if sub else D[:-1], recursive=False):
            name = str(ad.asset_name); cls = str(ad.asset_class_path.asset_name)
            a = unreal.load_asset(str(ad.package_name))
            if cls != "AnimSequence":
                P("-- %s/%s [%s]" % (sub, name, cls))
                if cls == "PoseSearchSchema":
                    for p in ("skeleton", "skeletons", "sample_rate", "channels", "data_preprocessor"):
                        try: P("   %s = %s" % (p, a.get_editor_property(p)))
                        except Exception as e: pass
                continue
            ln = a.get_play_length()
            opts = unreal.AnimPoseEvaluationOptions(); opts.set_editor_property('extract_root_motion', False)
            def root(t):
                r = E.get_bone_pose(E.get_anim_pose_at_time(a, t, opts), 'root', unreal.AnimPoseSpaces.LOCAL)
                return r.translation, r.rotation.rotator().yaw
            r0, y0 = root(0.0); r1, y1 = root(ln)
            samples = []
            for i in range(0, 9):
                t = ln * i / 8.0
                tr, yw = root(t)
                samples.append("%.2f:(%.0f,%.0f,%.0f)" % (t, tr.x, tr.y, tr.z))
            dist = ((r1.x - r0.x) ** 2 + (r1.y - r0.y) ** 2) ** 0.5
            sk = a.get_editor_property('skeleton')
            P("== %s/%s len %.2f rm=%s skel=%s travel %.0f cm (d=%.0f,%.0f,%.0f) yaw %.0f->%.0f speed %.0f cm/s" % (
                sub, name, ln, a.get_editor_property('enable_root_motion'), sk.get_name() if sk else None,
                dist, r1.x - r0.x, r1.y - r0.y, r1.z - r0.z, y0, y1, dist / ln if ln else 0))
            if sub == "Combat": P("   " + " ".join(samples))
            try:
                for ev in a.get_editor_property('notifies'):
                    P("   notify %s t=%.2f dur=%.2f" % (ev.get_editor_property('notify_name'), ev.get_editor_property('trigger_time_offset') if False else 0, 0))
            except Exception: pass
except Exception:
    P("EXC: " + traceback.format_exc())
open(out, "w").write("\n".join(L))
