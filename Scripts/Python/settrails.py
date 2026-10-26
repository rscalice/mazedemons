import unreal, traceback
# Sets Template on every MDANS_WeaponNiagara notify state: DualSword montages -> NS_Trail_03, Greatsword -> NS_Trail_12.
# Keeps socket/offsets/timing. Saves only changed montages. Idempotent. Report: settrails.txt
out = r"C:\Users\Robert Scalice\Documents\Unreal Projects\mazedemons\Saved\ClaudeDump\settrails.txt"
L = []
def P(s): L.append(str(s))
lib = unreal.AnimationLibrary
JOBS = [("/Game/Animations/Combat/DualSword", "/Game/Fab/SwordTrailVFX/VFX/NS_Trail_03"),
        ("/Game/Animations/Combat/Greatsword", "/Game/Fab/SwordTrailVFX/VFX/NS_Trail_12")]
def nm(o): return o.get_name() if o else "None"
try:
    cls = unreal.load_class(None, "/Script/MazeDemons.MDANS_WeaponNiagara")
    if cls is None:
        P("ABORT: MDANS_WeaponNiagara class not found. Rebuild the project first.")
    else:
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        changed = 0; total = 0
        for folder, tpl_path in JOBS:
            tpl = unreal.load_asset(tpl_path)
            if tpl is None:
                P("ABORT folder %s: template not found %s" % (folder, tpl_path)); continue
            for ad in ar.get_assets_by_path(folder, recursive=True):
                path = str(ad.package_name)
                m = unreal.load_asset(path)
                if not isinstance(m, unreal.AnimMontage):
                    continue
                dirty = False
                for e in lib.get_animation_notify_events(m):
                    st = e.get_editor_property('notify_state_class')
                    if st is None or st.get_class() != cls:
                        continue
                    total += 1
                    old = st.get_editor_property('template')
                    sock = st.get_editor_property('socket_name')
                    if old is not None and old.get_path_name() == tpl.get_path_name():
                        P("%s | %s | unchanged (%s)" % (path, sock, nm(old)))
                        continue
                    st.set_editor_property('template', tpl)
                    dirty = True
                    P("%s | %s | %s -> %s" % (path, sock, nm(old), nm(tpl)))
                if dirty:
                    m.modify()
                    ok = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
                    P("  saved %s -> %s" % (path, ok))
                    changed += 1
        P("DONE: %d notify state(s) seen, %d montage(s) changed" % (total, changed))
except Exception:
    P("EXC " + traceback.format_exc())
txt = "\n".join(L)
open(out, "w").write(txt)
print(txt)
