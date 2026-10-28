"""Debug harness (never shipped): starts PIE a few seconds after the editor opens, so a command file can drive it.
Launch: UnrealEditor.exe MazeDemons.uproject /Game/Maps/L_MazeDemonsPrototype -MDCmdFile -ExecutePythonScript=<this file>
The editor-only ABP_GenericRetarget node (/Script/MazeDemonsEditor) does not load in a standalone -game run, so PIE it is.
"""
import unreal

_state = {"ticks": 0, "handle": None}


def _tick(delta):
    _state["ticks"] += 1
    if _state["ticks"] == 600:  # about ten seconds after startup
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
        unreal.unregister_slate_post_tick_callback(_state["handle"])


_state["handle"] = unreal.register_slate_post_tick_callback(_tick)
