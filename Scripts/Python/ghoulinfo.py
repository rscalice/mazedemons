import unreal, traceback
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulinfo.txt"
L = []
def P(s): L.append(str(s))
AR = unreal.AssetRegistryHelpers.get_asset_registry()

def mesh_bones(mesh):
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    n = comp.get_num_bones()
    return [str(comp.get_bone_name(i)) for i in range(n)]

def sec(name, fn):
    try:
        fn()
    except Exception:
        P("EXC in %s: %s" % (name, traceback.format_exc()))

names = []
def s_ghoul():
    global names
    ghoul = unreal.load_asset("/Game/Fab/Ghoul/Mesh/SK_Ghoul_UE5")
    P("SK_Ghoul_UE5 skeleton: %s" % ghoul.skeleton.get_path_name())
    P("  physics asset: %s" % (ghoul.physics_asset.get_path_name() if ghoul.physics_asset else None))
    P("  materials: %s" % [str(m.material_slot_name) for m in ghoul.materials])
    names = mesh_bones(ghoul)
    P("  ghoul mesh bones (%d): %s" % (len(names), names))
    try:
        sockets = ghoul.skeleton.get_editor_property('sockets')
        P("  skeleton sockets: %s" % [str(s.socket_name) + "@" + str(s.bone_name) for s in sockets])
    except Exception as ex:
        P("  skeleton sockets ERR %s" % ex)
sec("ghoul", s_ghoul)

def s_manny():
    for path in ["/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny", "/Game/Characters/UE5_Mannequins/Meshes/SK_Mannequin", "/Game/Characters/Mannequins/Meshes/SKM_Manny"]:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            m = unreal.load_asset(path)
            P("project mesh %s skeleton: %s" % (path, m.skeleton.get_path_name()))
            names2 = mesh_bones(m)
            P("  bones (%d)" % len(names2))
            P("  only in ghoul: %s" % [n for n in names if n not in names2])
            P("  only in manny: %s" % [n for n in names2 if n not in names])
            P("  order identical: %s" % (names == names2))
            try:
                sockets = m.skeleton.get_editor_property('sockets')
                P("  skeleton sockets: %s" % [str(s.socket_name) + "@" + str(s.bone_name) for s in sockets])
            except Exception as ex:
                P("  skeleton sockets ERR %s" % ex)
sec("manny", s_manny)

def s_skeletons():
    hits = AR.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "Skeleton"))
    P("== Skeleton assets (%d)" % len(hits))
    for h in hits:
        P("  %s" % str(h.package_name))
sec("skeletons", s_skeletons)

def s_anims():
    folders = [
        "/Game/Animations/WeaponStances/DynamicTwinAxe/Animations/Manny_UE5/RootMotion/ComboAttack",
        "/Game/Animations/WeaponStances/DynamicTwinAxe/Animations/Manny_UE5/RootMotion/Hit",
        "/Game/Animations/WeaponStances/DynamicSwordShieldV3/Animations/Manny_UE5/RootMotion/Attack",
        "/Game/Animations/WeaponStances/DynamicSwordShieldV3/Animations/Manny_UE5/RootMotion/Hit",
        "/Game/Animations/WeaponStances/DynamicSwordShieldV3/Animations/Manny_UE5/RootMotion/Die",
        "/Game/Animations/WeaponStances/DynamicSwordShieldV3/Animations/Manny_UE5/RootMotion/Locomotion",
        "/Game/Animations/WeaponStances/DynamicGreatSwordAnims/Animations/RootMotion/Manny_UE5/ComboAttack",
        "/Game/Animations/WeaponStances/DynamicGreatSwordAnims/Animations/RootMotion/Manny_UE5/Hit",
        "/Game/Animations/WeaponStances/TwinSwordExpansion/Attacks",
        "/Game/Animations/WeaponStances/TwinSwordExpansion/HitReactions",
        "/Game/Animations/WeaponStances/InsaneSwordShield/2_Attacks",
        "/Game/Animations/WeaponStances/InsaneSwordShield/5_Damage",
        "/Game/Fab/Ghoul/Demo/Demoscene_UE5/Animations",
        "/Game/Animations/Combat/Reactions",
    ]
    for f in folders:
        assets = AR.get_assets_by_path(unreal.Name(f), recursive=True)
        P("== %s (%d)" % (f, len(assets)))
        for a in assets:
            cls = str(a.asset_class_path.asset_name)
            if cls not in ("AnimSequence", "AnimMontage", "BlendSpace", "BlendSpace1D"): continue
            obj = a.get_asset()
            try:
                sk = obj.get_editor_property('skeleton')
                skp = sk.get_name() if sk else None
            except Exception:
                skp = "?"
            extra = ""
            if cls == "AnimSequence":
                try:
                    extra = " len=%.2f rm=%s" % (obj.get_play_length(), obj.get_editor_property('enable_root_motion'))
                except Exception as ex:
                    extra = " ERR " + str(ex)
            elif cls == "AnimMontage":
                try:
                    extra = " len=%.2f slots=%s" % (obj.get_play_length(), [str(t.slot_name) for t in obj.get_editor_property('slot_anim_tracks')])
                except Exception as ex:
                    extra = " ERR " + str(ex)
            P("  %s [%s] skel=%s%s" % (str(a.asset_name), cls, skp, extra))
sec("anims", s_anims)

def s_data():
    for path in ["/Game/Data/Combat/DA_Poise_Ghoul", "/Game/Data/Enemies/DA_Archetype_Ghoul", "/Game/Data/Combat/DA_Poise_Dummy"]:
        o = unreal.load_asset(path)
        P("== %s" % path)
        for prop in ["max_poise", "regen_delay", "regen_per_sec", "stun_duration", "stagger_immunity_after_stun", "knockback_resist", "hurt_sound", "reaction_montages",
                     "archetype_tag", "max_health", "attack_power", "base_defense", "move_speed", "kill_gold", "poise_profile", "ai_params", "min_tier", "max_tier"]:
            try:
                v = o.get_editor_property(prop)
                if prop == "reaction_montages":
                    v = [(str(r.reaction), str(r.direction), r.montage.get_path_name() if r.montage else None) for r in v]
                P("  %s = %s" % (prop, v))
            except Exception:
                pass
sec("data", s_data)

def s_bp():
    bp = unreal.load_asset("/Game/Blueprints/Enemies/BP_Enemy_Ghoul")
    gc = bp.generated_class()
    cdo = unreal.get_default_object(gc)
    P("== BP_Enemy_Ghoul class %s" % gc.get_name())
    mesh = cdo.get_editor_property('mesh')
    P("  mesh asset: %s" % (mesh.skeletal_mesh_asset.get_path_name() if mesh.skeletal_mesh_asset else None))
    P("  anim mode: %s anim class: %s" % (mesh.get_editor_property('animation_mode'), mesh.get_editor_property('anim_class')))
    try:
        P("  single anim: %s" % mesh.get_editor_property('animation_data'))
    except Exception as ex:
        P("  single anim ERR %s" % ex)
    P("  mesh rel loc/rot/scale: %s %s %s" % (mesh.get_editor_property('relative_location'), mesh.get_editor_property('relative_rotation'), mesh.get_editor_property('relative_scale3d')))
    cap = cdo.get_editor_property('capsule_component')
    P("  capsule: r=%s h=%s" % (cap.get_unscaled_capsule_radius(), cap.get_unscaled_capsule_half_height()))
    P("  archetype: %s" % cdo.get_editor_property('archetype'))
    P("  corpse lifetime: %s" % cdo.get_editor_property('corpse_lifetime'))
    cm = cdo.get_editor_property('character_movement')
    P("  walk speed %s accel %s" % (cm.get_editor_property('max_walk_speed'), cm.get_editor_property('max_acceleration')))
    try:
        P("  ubergraph pages: %d" % len(bp.get_editor_property('ubergraph_pages')))
    except Exception as ex:
        P("  graph ERR %s" % ex)
sec("bp", s_bp)

def s_level():
    hits = AR.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "World"))
    P("maps: %s" % [str(h.package_name) for h in hits if '/Game/' in str(h.package_name) and 'Fab' not in str(h.package_name)])
sec("level", s_level)

open(out, "w").write("\n".join(L))
