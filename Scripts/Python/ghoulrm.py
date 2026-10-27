import unreal
# Read-only root-motion data check for the Ghoul attacks. Report: Saved/ClaudeDump/ghoulrm.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulrm.txt"
L = []
def P(s): L.append(str(s)); print(s)
def props(o, names):
    r = []
    for n in names:
        try: r.append("%s=%s" % (n, o.get_editor_property(n)))
        except Exception as ex: r.append("%s=?" % n)
    return "  ".join(r)

SEQ = ["/Game/Animations/WeaponStances/DynamicTwinAxe/Animations/Manny_UE5/RootMotion/ComboAttack/Anim_DTA_ComboA1_RM",
       "/Game/Animations/WeaponStances/TwinSwordExpansion/Attacks/RM/UE5_TwinSword_Attack48",
       "/Game/Animations/WeaponStances/DynamicTwinAxe/Animations/Manny_UE5/RootMotion/ComboAttack/Anim_DTA_ComboA3_RM"]
P("== sequences")
for p in SEQ:
    a = unreal.load_asset(p)
    if a is None: P("  MISSING " + p); continue
    P("  %s: %s" % (a.get_name(), props(a, ['enable_root_motion', 'root_motion_root_lock', 'force_root_lock', 'skeleton'])))
P("== montages")
for n in ("AM_Ghoul_Swipe", "AM_Ghoul_Lunge"):
    m = unreal.load_asset("/Game/Animations/Enemies/Ghoul/" + n)
    if m is None: P("  MISSING " + n); continue
    P("  %s: %s" % (n, props(m, ['enable_root_motion_translation', 'enable_root_motion_rotation', 'root_motion_root_lock', 'force_root_lock', 'blend_mode_in'])))
    for t in m.get_editor_property('slot_anim_tracks'):
        for s in t.get_editor_property('anim_track').get_editor_property('anim_segments'):
            P("    slot %s segment %s" % (t.get_editor_property('slot_name'), s.get_editor_property('anim_reference').get_name()))
# a player combo montage for comparison
P("== player montages (comparison)")
AR = unreal.AssetRegistryHelpers.get_asset_registry()
for ad in AR.get_assets_by_path("/Game/Animations", recursive=True):
    if str(ad.asset_class_path.asset_name) == "AnimMontage" and "Combo" in str(ad.asset_name):
        m = ad.get_asset()
        P("  %s: %s" % (ad.asset_name, props(m, ['enable_root_motion_translation', 'root_motion_root_lock', 'force_root_lock'])))
        break
P("== archetype attacks")
da = unreal.load_asset("/Game/Data/Enemies/DA_Archetype_Ghoul")
for a in da.get_editor_property('attacks'):
    st = a.get_editor_property('step')
    P("  %s: range %s  %s" % (a.name, a.range, props(st, ['root_motion_scale', 'warp_range', 'warp_stop_distance', 'play_rate'])))
P("== anim instance")
abp = unreal.load_asset("/Game/Animations/Enemies/Ghoul/ABP_Ghoul")
cdo = unreal.get_default_object(abp.generated_class())
P("  ABP_Ghoul CDO: %s" % props(cdo, ['root_motion_mode']))
bp = unreal.load_asset("/Game/Blueprints/Enemies/BP_Enemy_Ghoul")
pawn = unreal.get_default_object(bp.generated_class())
P("  BP_Enemy_Ghoul: motion warping component = %s" % pawn.get_component_by_class(unreal.MotionWarpingComponent))
open(out, "w").write("\n".join(L))
