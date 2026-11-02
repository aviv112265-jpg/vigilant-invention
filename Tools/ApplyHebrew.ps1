# MISHPACHA - Hebrew localization: gather strings from the C++ code, fill in Tools/Hebrew.tsv, compile.
#   powershell -File Tools\ApplyHebrew.ps1 [-Engine "D:\Epic Games\UE_5.8"]
# Hebrew.tsv: "Namespace,Key<TAB>Hebrew" per line. Strings without a line fall back to English.
# Editor must be closed.
param([string]$Engine = "D:\Epic Games\UE_5.8")

$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$Project = Join-Path $Root "Mishpacha.uproject"
$Cmd = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"

function Run-Step($Name) {
    & $Cmd $Project -run=GatherText "-config=Config/Localization/Game_$Name.ini" -unattended -nosplash | Out-Null
    Write-Output "$Name : exit $LASTEXITCODE"
}

Run-Step "Gather"
Run-Step "Export"

# Fill msgstr for every msgctxt we have a translation for.
$utf8 = New-Object System.Text.UTF8Encoding($false)
$table = @{}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $PSScriptRoot "Hebrew.tsv"), $utf8)) {
    $parts = $line.Split("`t", 2)
    if ($parts.Count -eq 2 -and $parts[0].Trim()) { $table[$parts[0].Trim()] = $parts[1] }
}
$po = Join-Path $Root "Content\Localization\Game\he\Game.po"
$lines = [IO.File]::ReadAllLines($po, $utf8)
$context = $null
$filled = 0
for ($i = 0; $i -lt $lines.Length; $i++) {
    if ($lines[$i] -match '^msgctxt "(.*)"$') { $context = $Matches[1] }
    elseif ($lines[$i] -match '^msgstr "' -and $context) {
        if ($table.ContainsKey($context)) {
            $value = $table[$context].Replace('\', '\\').Replace('"', '\"')
            $lines[$i] = "msgstr `"$value`""
            $filled++
        }
        $context = $null
    }
}
[IO.File]::WriteAllLines($po, $lines, $utf8)
Write-Output "Hebrew strings filled: $filled"

Run-Step "Import"
Run-Step "Compile"
Get-ChildItem (Join-Path $Root "Content\Localization\Game") -Recurse -Filter *.locres | ForEach-Object { "{0}  {1} bytes" -f $_.FullName, $_.Length }
