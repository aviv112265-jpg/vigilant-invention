# MISHPACHA - run the game for ~60 s, measure FPS and take 4 screenshots (editor must be closed).
# Usage: powershell -File Tools\Capture.ps1 [-Engine "D:\Epic Games\UE_5.8"] [-Map /Engine/Maps/Templates/Template_Default]
param(
    [string]$Engine = "D:\Epic Games\UE_5.8",
    [string]$Map = "/Engine/Maps/Templates/Template_Default",
    [int]$Seconds = 80
)

$Project = Join-Path $PSScriptRoot "..\Mishpacha.uproject" | Resolve-Path
$Saved = Join-Path (Split-Path $Project) "Saved"
$Shots = Join-Path $Saved "Screenshots\WindowsEditor"
Get-ChildItem $Shots -Filter "Capture_*.png" -ErrorAction SilentlyContinue | Remove-Item

$exe = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor.exe"
$game = Start-Process -FilePath $exe -PassThru -ArgumentList "`"$Project`" $Map -game -windowed -resx=1600 -resy=900 -nosound -nosplash -MishpachaCapture -log=Capture.log"
Start-Sleep $Seconds
if (-not $game.HasExited) { Stop-Process -Id $game.Id -Force }

$log = Join-Path $Saved "Logs\Capture.log"
Select-String -Path $log -Pattern "frames collected|Average GPU frametime|Average GameThread frametime|LogMaterial: Warning|error X|LogMishpacha: (Warning|Error)" |
    ForEach-Object { $_.Line.Substring([Math]::Min(30, $_.Line.Length)) }
Get-ChildItem $Shots -Filter "Capture_*.png" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
