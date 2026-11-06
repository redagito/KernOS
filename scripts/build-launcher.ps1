# Compile the graphical launcher; run it from Explorer to use the user's
# visible desktop rather than an agent command runner's isolated desktop.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$compiler = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$launcherSource = Join-Path $PSScriptRoot 'KernOSLauncher.cs'
$launcherOutput = Join-Path $repo 'build\KernOSLauncher.exe'
$temporary = Join-Path $repo 'build\tmp'
New-Item -ItemType Directory -Force -Path $temporary | Out-Null
$savedTemp = $env:TEMP
$savedTmp = $env:TMP
try {
    $env:TEMP = $temporary
    $env:TMP = $temporary
    & $compiler /nologo /target:winexe "/out:$launcherOutput" /reference:System.Windows.Forms.dll $launcherSource
    if ($LASTEXITCODE -ne 0) { throw 'KernOS launcher compilation failed.' }
} finally {
    $env:TEMP = $savedTemp
    $env:TMP = $savedTmp
}
