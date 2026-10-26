import unreal
out = r"C:\Users\Robert Scalice\Documents\Unreal Projects\mazedemons\Saved\ClaudeDump\verify1.txt"
L=[]
lib = unreal.AnimationLibrary
for ad in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path("/Game/Animations/Combat", recursive=True):
    if str(ad.asset_class_path.asset_name) != "AnimMontage": continue
    m = ad.get_asset()
    for e in lib.get_animation_notify_events(m):
        st = e.get_editor_property('notify_state_class')
        if st is not None and st.get_class().get_name() == "AnimNotifyState_TimedNiagaraEffect":
            L.append("%s start=%.3f dur=%.3f socket=%s template=%s loc=%s rot=%s scale=%s" % (ad.asset_name, lib.get_anim_notify_event_trigger_time(e), lib.get_anim_notify_event_duration(e), st.get_editor_property('socket_name'), st.get_editor_property('template').get_name(), st.get_editor_property('location_offset'), st.get_editor_property('rotation_offset'), st.get_editor_property('scale')))
open(out, "w").write("\n".join(L) if L else "NONE")
