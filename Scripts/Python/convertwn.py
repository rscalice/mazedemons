import unreal, traceback
# Replaces every stock AnimNotifyState_TimedNiagaraEffect in /Game/Animations/Combat montages with MDANS_WeaponNiagara
# (same time / duration / track, same Template / SocketName / offsets / Scale / bDestroyAtEnd). Idempotent: montages with no
# stock events left are untouched and not saved. Needs the C++ build with UMDANS_WeaponNiagara.
out = r"C:\Users\Robert Scalice\Documents\Unreal Projects\mazedemons\Saved\ClaudeDump\convertwn.txt"
L = []
def P(s): L.append(str(s))
lib = unreal.AnimationLibrary
STOCK_NAME = "TimedNiagaraEffect"
try:
    stock_cls = unreal.load_class(None, "/Script/NiagaraAnimNotifies.AnimNotifyState_TimedNiagaraEffect")
    new_cls = unreal.load_class(None, "/Script/MazeDemons.MDANS_WeaponNiagara")
    if new_cls is None or stock_cls is None:
        P("ABORT: class not found (new=%s stock=%s). Rebuild the project first." % (new_cls, stock_cls))
    else:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        assets = ar.get_assets_by_path("/Game/Animations/Combat", recursive=True)
        changed = 0
        for ad in assets:
            path = str(ad.package_name)
            m = unreal.load_asset(path)
            if not isinstance(m, unreal.AnimMontage):
                continue
            evs = lib.get_animation_notify_events(m)
            tracks = [str(x) for x in lib.get_animation_notify_track_names(m)]
            found = []
            for e in evs:
                st = e.get_editor_property('notify_state_class')
                if st is not None and st.get_class() == stock_cls:
                    try: ti = int(e.get_editor_property('track_index'))
                    except Exception: ti = 0
                    tn = tracks[ti] if 0 <= ti < len(tracks) else tracks[0]
                    found.append(dict(
                        start=lib.get_anim_notify_event_trigger_time(e), dur=lib.get_anim_notify_event_duration(e), track=tn,
                        template=st.get_editor_property('template'), socket=st.get_editor_property('socket_name'),
                        loc=st.get_editor_property('location_offset'), rot=st.get_editor_property('rotation_offset'),
                        scale=st.get_editor_property('scale'), destroy=st.get_editor_property('destroy_at_end')))
            if not found:
                continue
            try: lib.remove_animation_notify_events_by_name(m, STOCK_NAME)
            except Exception as ex: P("  remove ERR " + str(ex))
            for f in found:
                if f['track'] not in tracks:
                    lib.add_animation_notify_track(m, f['track'], unreal.LinearColor(0.2, 0.7, 1.0, 1.0))
                    tracks.append(f['track'])
                st = lib.add_animation_notify_state_event(m, f['track'], f['start'], f['dur'], new_cls)
                st.set_editor_property('template', f['template'])
                st.set_editor_property('socket_name', f['socket'])
                st.set_editor_property('location_offset', f['loc'])
                st.set_editor_property('rotation_offset', f['rot'])
                st.set_editor_property('scale', f['scale'])
                st.set_editor_property('destroy_at_end', f['destroy'])
                P("%s: start=%.3f dur=%.3f socket=%s track=%s tpl=%s" % (path, f['start'], f['dur'], f['socket'], f['track'], f['template'].get_name() if f['template'] else None))
            m.modify()
            ok = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
            P("  saved %s -> %s (%d converted)" % (path, ok, len(found)))
            changed += 1
        P("DONE: %d montage(s) converted" % changed)
except Exception:
    P("EXC " + traceback.format_exc())
open(out, "w").write("\n".join(L))
