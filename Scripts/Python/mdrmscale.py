import unreal, traceback
# Sets FMDAttackStep.RootMotionScale on DA_Combo_DualSword / DA_Combo_Greatsword (run after the C++ rebuild and
# ApplyRootMotionAttacks.ps1). Works in the editor (Tools > Execute Python Script) or as a commandlet with the editor closed.
# Idempotent; saves a data asset only when a value changed. Report: rmscale.txt
out = r"C:\Users\Robert Scalice\Documents\Unreal Projects\mazedemons\Saved\ClaudeDump\rmscale.txt"
L = []
def P(s): L.append(str(s))

# asset -> {light chain index list, heavy, charged}
SCALES = {
    "/Game/Data/Combat/DA_Combo_DualSword": {"light_chain": [1.0, 1.0, 1.0, 0.7, 0.5], "heavy": 0.5, "charged": 0.5},
    "/Game/Data/Combat/DA_Combo_Greatsword": {"light_chain": [1.0, 1.0, 1.0], "heavy": 1.0, "charged": 0.6},
}

def put(step, value, label):
    cur = step.get_editor_property("root_motion_scale")
    if abs(cur - value) < 1e-4:
        P("  %s: %.2f (unchanged)" % (label, cur)); return False
    step.set_editor_property("root_motion_scale", value)
    P("  %s: %.2f -> %.2f" % (label, cur, value)); return True

try:
    for path, cfg in SCALES.items():
        a = unreal.load_asset(path)
        P("== " + path)
        if a is None:
            P("  SKIP: not found"); continue
        dirty = False
        chain = list(a.get_editor_property("light_chain"))
        if len(chain) != len(cfg["light_chain"]):
            P("  WARN: light chain has %d steps, expected %d (only the first %d are set)" % (len(chain), len(cfg["light_chain"]), min(len(chain), len(cfg["light_chain"]))))
        for i, s in enumerate(chain[:len(cfg["light_chain"])]):
            if put(s, cfg["light_chain"][i], "LightChain[%d]" % i):
                dirty = True
        if dirty:
            a.set_editor_property("light_chain", chain)  # struct elements are copies: write the array back
        for name in ("heavy", "charged"):
            s = a.get_editor_property(name)
            if put(s, cfg[name], name):
                a.set_editor_property(name, s)
                dirty = True
        if dirty:
            a.modify()
            P("  saved -> %s" % unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))
        else:
            P("  nothing to save")
except Exception:
    P("EXC " + traceback.format_exc())
txt = "\n".join(L)
open(out, "w").write(txt)
print(txt)
