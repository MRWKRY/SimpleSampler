# Validates the built VST3 with pluginval (https://github.com/Tracktion/pluginval).
# pluginval is downloaded from its official GitHub release the first time.
param(
    [int]$Strictness = 5,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot\common.ps1"

$buildDir = Get-BuildDir $root
$vst3 = Join-Path $buildDir "SimpleSampler_artefacts\$Config\VST3\SimpleSampler.vst3"
if (-not (Test-Path $vst3)) { throw "Build the plugin first (.\build.ps1): $vst3 not found" }

$toolDir = Join-Path $buildDir "pluginval"
$exe = Join-Path $toolDir "pluginval.exe"
if (-not (Test-Path $exe)) {
    New-Item -ItemType Directory -Force $toolDir | Out-Null
    $zip = Join-Path $toolDir "pluginval_Windows.zip"
    Write-Host "Downloading pluginval..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip" -OutFile $zip
    Expand-Archive -Force $zip $toolDir
}

Write-Host "Running pluginval (strictness $Strictness) on $vst3"
$p = Start-Process -FilePath $exe -ArgumentList @("--strictness-level", "$Strictness", "--validate-in-process", "--validate", "`"$vst3`"") `
                   -NoNewWindow -PassThru -Wait
Write-Host "pluginval exit code: $($p.ExitCode)"
exit $p.ExitCode
