# mazedemons

## Build and relaunch Unreal Editor

Save your work and close Unreal Editor before running the build script. The
script waits for Unreal Editor, game, and Live Coding processes to exit, builds
the `MazeDemonsEditor` Win64 Development target, and relaunches the project
after a successful build. It does not force-close Unreal processes.

From the repository root, run:

```powershell
.\Scripts\BuildAndRelaunchEditor.ps1
```

If Unreal Engine is installed in a different directory, pass its install root:

```powershell
.\Scripts\BuildAndRelaunchEditor.ps1 -EngineRoot "E:\Epic Games\UE_5.8"
```
