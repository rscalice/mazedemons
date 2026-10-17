# Launches the editor on the project (avoids the project-chooser quoting problem when called inline).
$editor = 'D:\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$project = 'D:\Users\Unreal Projects\mazedemons\MazeDemons.uproject'
Start-Process -FilePath $editor -ArgumentList @("`"$project`"")
