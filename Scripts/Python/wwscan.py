import unreal, traceback, math, re
# Read-only: scan every attack-like AnimSequence in the project (Fab included) and report root travel, jump height,
# upside-down / spin metrics, hand speed peak. Report: Saved/ClaudeDump/wwscan.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/wwscan.txt"
L = []
def P(s):
    L.append(str(s)); print(s)

AR = unreal.AssetRegistryHelpers.get_asset_registry()
flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "AnimSequence")], package_paths=["/Game"], recursive_paths=True)
assets = AR.get_assets(flt)
P("total AnimSequence assets: %d" % len(assets))
pat = re.compile(r"attack|slash|swing|stab|punch|strike|smash|claw|swipe|bite|lunge|combo|skill|melee|slam|kick|impact", re.I)
skip = re.compile(r"Mirror|_Air|AirAttack|Plunge|Dodge_Attack|Idle|Walk|Run|Jog|Sprint|Hit|Hurt|Death|Die|Jump|Crossbow|Reload|Aim|Parry|Counter|Block|Guard|Equip|Charge|Buff", re.I)
rows = []
cands = []
for ad in assets:
    name = str(ad.asset_name); pkg = str(ad.package_name)
    if pat.search(name) and not skip.search(name):
        cands.append(ad)
P("candidates: %d" % len(cands))
opts = unreal.AnimPoseEvaluationOptions()
opts.set_editor_property('extract_root_motion', False)
def has(names, n): return n in names
for ad in cands:
    try:
        a = ad.get_asset()
        length = a.get_play_length()
        if length < 0.3 or length > 4.0: continue
        n = max(int(length * 15), 4); dt = length / n
        pose0 = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, 0.0, opts)
        names = set(str(x) for x in unreal.AnimPoseExtensions.get_bone_names(pose0))
        if not (has(names, 'pelvis') and has(names, 'head') and has(names, 'hand_r')): continue
        root_b = 'root' if has(names, 'root') else 'pelvis'
        r0 = None; maxd = 0.0; pz = []; hd = []; yawsum = 0.0; prevyaw = None
        hp = {'hand_r': None, 'hand_l': None}; peak = {'hand_r': (0, 0), 'hand_l': (0, 0)}
        endpos = None
        for i in range(n + 1):
            t = i * dt
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, t, opts)
            W = unreal.AnimPoseSpaces.WORLD
            root = unreal.AnimPoseExtensions.get_bone_pose(pose, root_b, W).translation
            pel = unreal.AnimPoseExtensions.get_bone_pose(pose, 'pelvis', W)
            head = unreal.AnimPoseExtensions.get_bone_pose(pose, 'head', W).translation
            if r0 is None: r0 = root; pel0z = pel.translation.z
            endpos = root
            pz.append(pel.translation.z - pel0z)
            hd.append(head.z - pel.translation.z)
            yaw = pel.rotation.rotator().yaw
            if prevyaw is not None:
                d = (yaw - prevyaw + 180) % 360 - 180
                yawsum += d
            prevyaw = yaw
            for h in ('hand_r', 'hand_l'):
                loc = unreal.AnimPoseExtensions.get_bone_pose(pose, h, W).translation
                if hp[h] is not None:
                    sp = (loc - hp[h]).length() / dt
                    if sp > peak[h][1]: peak[h] = (t, sp)
                hp[h] = loc
        travel = math.sqrt((endpos.x - r0.x) ** 2 + (endpos.y - r0.y) ** 2)
        rows.append((str(ad.package_name), length, travel, max(pz), min(hd), yawsum, peak))
    except Exception as ex:
        pass
P("analysed: %d" % len(rows))
for r in sorted(rows, key=lambda r: r[0]):
    pk = r[6]
    P("%s | len %.2f | travel %.0f | pelvisUp %.0f | headMin %.0f | yaw %.0f | peakR %.2fs %.0f | peakL %.2fs %.0f" % (
        r[0].replace('/Game/', ''), r[1], r[2], r[3], r[4], r[5], pk['hand_r'][0], pk['hand_r'][1], pk['hand_l'][0], pk['hand_l'][1]))
open(out, "w").write("\n".join(L))
