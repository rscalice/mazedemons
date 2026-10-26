import unreal
# Run AFTER rebuilding with UMDComboSet::SwingSoundPitch. Sets DA_Combo_Greatsword SwingSoundPitch = 0.5 and saves.
path = "/Game/Data/Combat/DA_Combo_Greatsword"
da = unreal.load_asset(path)
if da is None:
    print("setpitch: asset not found " + path)
else:
    try:
        old = da.get_editor_property('swing_sound_pitch')
        da.set_editor_property('swing_sound_pitch', 0.5)
        da.modify()
        ok = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        print("setpitch: SwingSoundPitch %s -> 0.5, saved=%s" % (old, ok))
    except Exception as ex:
        print("setpitch: FAILED (is the project rebuilt?) " + str(ex))
