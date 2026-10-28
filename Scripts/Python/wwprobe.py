import unreal, traceback
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/wwprobe.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
try:
    a = unreal.load_asset("/Game/Animations/Enemies/Ghoul/A_Ghoul_Claw")
    ctl = a.controller
    P("controller: %s" % ctl)
    P("ctl methods: %s" % [m for m in dir(ctl) if not m.startswith('_') and ('bracket' in m or 'track' in m or 'key' in m or 'frame' in m or 'rate' in m)])
    dm = a.data_model_interface
    P("model methods: %s" % [m for m in dir(dm) if 'track' in m.lower() or 'frame' in m.lower() or 'bone' in m.lower()])
    P("num frames %s, keys %s" % (dm.get_number_of_frames(), dm.get_number_of_keys()))
    names = [str(n) for n in dm.get_bone_track_names()]
    P("tracks: %d, has root: %s, root idx: %s" % (len(names), 'root' in names, names.index('root') if 'root' in names else -1))
    tr = dm.get_bone_track_by_name("root")
    P("track obj: %s" % [m for m in dir(tr) if not m.startswith("_")])
    d = tr.get_editor_property("internal_track_data")
    pk = d.get_positional_keys(); rk = d.get_rotational_keys(); sk = d.get_scale_keys()
    P("claw root: pos %d rot %d scale %d; first %s last %s rot0 %s" % (len(pk), len(rk), len(sk), pk[0] if pk else None, pk[-1] if pk else None, rk[0] if rk else None))
    ref = unreal.load_asset("/Game/Animations/WeaponStances/TwinSwordExpansion/Attacks/RM/UE5_TwinSword_Attack16")
    rd = ref.data_model_interface.get_bone_track_by_name("root").get_editor_property("internal_track_data")
    pk = rd.get_positional_keys()
    P("ref Attack16 root: %d keys first %s mid %s last %s rot0 %s" % (len(pk), pk[0], pk[len(pk)//2], pk[-1], rd.get_rotational_keys()[0]))
    P("ref enable_root_motion %s lock %s fps %s" % (ref.get_editor_property("enable_root_motion"), ref.get_editor_property("root_motion_root_lock"), ref.data_model_interface.get_frame_rate()))
    P("claw enable_root_motion %s lock %s fps %s" % (a.get_editor_property("enable_root_motion"), a.get_editor_property("root_motion_root_lock"), dm.get_frame_rate()))
except Exception:
    P(traceback.format_exc())
open(out, "w").write("\n".join(L))
