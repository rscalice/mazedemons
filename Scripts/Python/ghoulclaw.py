import unreal, traceback, math
# Ghoul claw animation (attack fix): retargets the Werewolf's claw attack (Fab/Werewolf Anim_Werewolf_attack1, the only
# creature melee attack in the project) onto SK_Ghoul (SK_Mannequin skeleton) with the IK retargeter, the same way the ghoul
# locomotion was done (ghoulassets.py). Creates, outside Content/Fab:
#   Animations/Enemies/Ghoul/Retarget/IK_Werewolf        (auto-generated humanoid chains of the werewolf skeleton)
#   Animations/Enemies/Ghoul/Retarget/RTG_Werewolf_to_Ghoul
#   Animations/Enemies/Ghoul/A_Ghoul_Claw                 (retargeted source, no root motion)
# Run with the editor closed (commandlet). Idempotent. Report: Saved/ClaudeDump/ghoulclaw.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulclaw.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
def sec(name, fn):
    try:
        P("== " + name); return fn()
    except Exception:
        P("EXC in %s: %s" % (name, traceback.format_exc()))
    finally:
        open(out, "w").write("\n".join(L))

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
ANIM_DIR = "/Game/Animations/Enemies/Ghoul"
RT_DIR = ANIM_DIR + "/Retarget"
WW_MESH = "/Game/Fab/Werewolf/Mesh/Full/SK_Werewolf_Full1"
WW_ANIM = "/Game/Fab/Werewolf/Animations/Anim_Werewolf_attack1"
GHOUL_MESH = "/Game/Characters/Enemies/Ghoul/SK_Ghoul"
TARGET_RIG = "/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget"

def save(path):
    ok = EAL.save_asset(path, only_if_is_dirty=False)
    P("  saved %s -> %s" % (path, ok)); return ok

def s_rig():
    path = RT_DIR + "/IK_Werewolf"
    mesh = unreal.load_asset(WW_MESH)
    if EAL.does_asset_exist(path):
        rig = unreal.load_asset(path)
    else:
        EAL.make_directory(RT_DIR)
        rig = AT.create_asset("IK_Werewolf", RT_DIR, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    if c.get_skeletal_mesh() != mesh:
        P("  set_skeletal_mesh -> %s" % c.set_skeletal_mesh(mesh))
    P("  chains before: %d" % len(c.get_retarget_chains()))
    if len(c.get_retarget_chains()) <= 1:
        P("  apply_auto_generated_retarget_definition -> %s" % c.apply_auto_generated_retarget_definition())
    for ch in c.get_retarget_chains():
        P("    chain %s | %s | %s" % (ch.get_editor_property("chain_name"), ch.get_editor_property("start_bone").get_editor_property("bone_name"), ch.get_editor_property("end_bone").get_editor_property("bone_name")))
    P("  retarget root: %s" % c.get_retarget_root())
    save(path)
    return rig
rig = sec("IKRig", s_rig)

def s_retargeter():
    path = RT_DIR + "/RTG_Werewolf_to_Ghoul"
    src_mesh = unreal.load_asset(WW_MESH); tgt_mesh = unreal.load_asset(GHOUL_MESH)
    tgt_rig = unreal.load_asset(TARGET_RIG)
    if EAL.does_asset_exist(path):
        rtg = unreal.load_asset(path)
    else:
        rtg = AT.create_asset("RTG_Werewolf_to_Ghoul", RT_DIR, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rig)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, tgt_rig)
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, src_mesh)
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, tgt_mesh)
    if c.get_num_retarget_ops() == 0:
        c.add_default_ops()
    P("  ops: %d" % c.get_num_retarget_ops())
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    try:
        for s in c.get_all_chain_settings() if hasattr(c, 'get_all_chain_settings') else []:
            pass
    except Exception: pass
    for fn_name in ("auto_align_all_bones",):
        try:
            getattr(c, fn_name)(unreal.RetargetSourceOrTarget.TARGET)
            P("  %s(TARGET) ok" % fn_name)
        except Exception as ex:
            P("  %s ERR %s" % (fn_name, ex))
    save(path)
    return rtg
rtg = sec("Retargeter", s_retargeter)

def s_retarget_anim():
    dst = ANIM_DIR + "/A_Ghoul_Claw"
    src_mesh = unreal.load_asset(WW_MESH); tgt_mesh = unreal.load_asset(GHOUL_MESH)
    AR = unreal.AssetRegistryHelpers.get_asset_registry()
    ad = AR.get_asset_by_object_path(WW_ANIM + ".Anim_Werewolf_attack1")
    P("  source valid=%s" % ad.is_valid())
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.assets_to_retarget = [ad]
    inputs.source_mesh = src_mesh; inputs.target_mesh = tgt_mesh
    inputs.ik_retarget_asset = rtg
    inputs.search = "Anim_Werewolf_attack1"; inputs.replace = "A_Ghoul_Claw"
    inputs.target_path = ANIM_DIR; inputs.use_source_path = False
    inputs.include_referenced_assets = False; inputs.overwrite_existing_files = True
    res = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    for r in res: P("  retargeted: %s" % str(r.package_name))
    a = unreal.load_asset(dst)
    P("  %s: skeleton %s len %.2f" % (dst, a.get_editor_property('skeleton').get_name() if a else None, a.get_play_length() if a else -1))
    if a: save(dst)
sec("RetargetAnim", s_retarget_anim)

def s_metrics():
    a = unreal.load_asset(ANIM_DIR + "/A_Ghoul_Claw")
    length = a.get_play_length(); n = int(length * 30); dt = length / n
    opts = unreal.AnimPoseEvaluationOptions(); opts.set_editor_property('extract_root_motion', False)
    W = unreal.AnimPoseSpaces.WORLD; E = unreal.AnimPoseExtensions
    prev = {}; peak = {'hand_r': (0, 0), 'hand_l': (0, 0)}
    pel0 = None; rows = []
    for i in range(n + 1):
        t = i * dt
        pose = E.get_anim_pose_at_time(a, t, opts)
        pel = E.get_bone_pose(pose, 'pelvis', W)
        head = E.get_bone_pose(pose, 'head', W).translation
        tl = E.get_bone_pose(pose, 'thigh_l', W).translation; tr = E.get_bone_pose(pose, 'thigh_r', W).translation
        hip_yaw = math.degrees(math.atan2(tr.y - tl.y, tr.x - tl.x))
        if pel0 is None: pel0 = pel.translation; yaw0 = hip_yaw
        hr = E.get_bone_pose(pose, 'hand_r', W).translation; hl = E.get_bone_pose(pose, 'hand_l', W).translation
        for nm, loc in (('hand_r', hr), ('hand_l', hl)):
            if nm in prev:
                sp = (loc - prev[nm]).length() / dt
                if sp > peak[nm][1]: peak[nm] = (t, sp)
            prev[nm] = loc
        if i % 6 == 0:
            rows.append("  t=%.2f pelvis dz %.0f dxy (%.0f,%.0f) head-pelvis z %.0f hip yaw delta %.0f handR fwd(y) %.0f handL fwd(y) %.0f" % (
                t, pel.translation.z - pel0.z, pel.translation.x - pel0.x, pel.translation.y - pel0.y, head.z - pel.translation.z,
                (hip_yaw - yaw0 + 180) % 360 - 180, hr.y - pel.translation.y, hl.y - pel.translation.y))
    for r in rows: P(r)
    P("  peaks: hand_r %.2fs %.0f cm/s, hand_l %.2fs %.0f cm/s" % (peak['hand_r'][0], peak['hand_r'][1], peak['hand_l'][0], peak['hand_l'][1]))
sec("Metrics", s_metrics)

# ---------------------------------------------------------------- root-motion variants
# The werewolf clip has no root motion (the creature attacks in place). The ghoul needs a short step-in for the Swipe and a
# forward leap for the Lunge, so both are baked into copies of the retargeted clip: a smoothstep translation of the 'root'
# bone along the animation's forward axis (read from a project root-motion clip), no vertical or rotational root motion, so
# nothing can flip or spin. The pose itself is untouched.
FORWARD_REF = "/Game/Animations/WeaponStances/TwinSwordExpansion/Attacks/RM/UE5_TwinSword_Attack16"
# name: (travel cm, start s, end s) in clip time
VARIANTS = {"A_Ghoul_Swipe_RM": (45.0, 0.45, 0.85), "A_Ghoul_Lunge_RM": (360.0, 0.50, 0.90)}

def author(name, dist, t0, t1, fwd, ref_lock):
    dst = ANIM_DIR + "/" + name
    if not EAL.does_asset_exist(dst):
        EAL.duplicate_asset(ANIM_DIR + "/A_Ghoul_Claw", dst)
    a = unreal.load_asset(dst)
    dm = a.data_model_interface
    fr = dm.get_frame_rate(); fps = float(fr.numerator) / float(fr.denominator)
    nkeys = dm.get_number_of_keys()
    opts = unreal.AnimPoseEvaluationOptions(); opts.set_editor_property('extract_root_motion', False)
    E = unreal.AnimPoseExtensions
    # Always rebuild from the clean source so the script is idempotent.
    src = unreal.load_asset(ANIM_DIR + "/A_Ghoul_Claw")
    pos, rot, sca = [], [], []
    for i in range(nkeys):
        t = min(i / fps, src.get_play_length())
        xf = E.get_bone_pose(E.get_anim_pose_at_time(src, t, opts), 'root', unreal.AnimPoseSpaces.LOCAL)
        u = max(0.0, min(1.0, (t - t0) / (t1 - t0)))
        d = dist * u * u * (3.0 - 2.0 * u)
        p = xf.translation
        pos.append(unreal.Vector(p.x + fwd.x * d, p.y + fwd.y * d, p.z))
        rot.append(xf.rotation); sca.append(xf.scale3d)
    ctl = a.controller
    ctl.open_bracket(unreal.Text("Bake ghoul attack root motion"), True)
    ctl.set_bone_track_keys('root', pos, rot, sca, True)
    ctl.close_bracket(True)
    a.set_editor_property('enable_root_motion', True)
    try: a.set_editor_property('root_motion_root_lock', ref_lock)
    except Exception as ex: P("  root lock ERR %s" % ex)
    save(dst)
    a = unreal.load_asset(dst)
    q0 = E.get_bone_pose(E.get_anim_pose_at_time(a, 0.0, opts), 'root', unreal.AnimPoseSpaces.WORLD)
    q1 = E.get_bone_pose(E.get_anim_pose_at_time(a, a.get_play_length(), opts), 'root', unreal.AnimPoseSpaces.WORLD)
    tr = q1.translation - q0.translation
    P("  %s: keys %d @ %.1f fps, enable_root_motion=%s, root travel over the clip (%.0f,%.0f,%.0f) = %.0f cm 2D, root yaw change %.1f" % (
        name, nkeys, fps, a.get_editor_property('enable_root_motion'), tr.x, tr.y, tr.z, math.sqrt(tr.x ** 2 + tr.y ** 2), q1.rotation.rotator().yaw - q0.rotation.rotator().yaw))
def s_rm():
    ref = unreal.load_asset(FORWARD_REF)
    opts = unreal.AnimPoseEvaluationOptions(); opts.set_editor_property('extract_root_motion', False)
    E = unreal.AnimPoseExtensions; W = unreal.AnimPoseSpaces.WORLD
    p0 = E.get_bone_pose(E.get_anim_pose_at_time(ref, 0.0, opts), 'root', W).translation
    p1 = E.get_bone_pose(E.get_anim_pose_at_time(ref, ref.get_play_length(), opts), 'root', W).translation
    v = p1 - p0; n = math.sqrt(v.x ** 2 + v.y ** 2)
    fwd = unreal.Vector(v.x / n, v.y / n, 0.0)
    if abs(fwd.x) < 0.15: fwd = unreal.Vector(0.0, 1.0, 0.0)  # animation forward is +Y (mesh faces +Y; the BP rotates the mesh -90)
    ref_lock = ref.get_editor_property('root_motion_root_lock')
    P("  animation forward axis from %s: (%.2f,%.2f), ref root lock %s, ref enable_root_motion %s" % (ref.get_name(), fwd.x, fwd.y, ref_lock, ref.get_editor_property('enable_root_motion')))
    for name, (dist, t0, t1) in VARIANTS.items():
        author(name, dist, t0, t1, fwd, ref_lock)
sec("RootMotionVariants", s_rm)
open(out, "w").write("\n".join(L))
