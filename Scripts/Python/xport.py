# Diagnostic (R0): exports ABP_GenericRetarget as text to Saved\ClaudeDump\r0\ABP_GenericRetarget_after.t3d. Run via Scripts\RunPy.ps1.
import os
import unreal

out_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump", "r0")
os.makedirs(out_dir, exist_ok=True)
a = unreal.load_asset("/Game/Blueprints/GASP/RetargetedCharacters/ABP_GenericRetarget")
t = unreal.AssetExportTask()
t.object = a
t.filename = os.path.join(out_dir, "ABP_GenericRetarget_after.t3d")
t.automated = True
t.prompt = False
t.replace_identical = True
t.exporter = None
unreal.Exporter.run_asset_export_task(t)
