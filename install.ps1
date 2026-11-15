# Installs SimpleSampler.vst3 into  C:\Program Files\Common Files\VST3
# (builds it first if it has not been built yet). Asks for administrator rights by itself.
param(
    [string]$Source = "",       # path to a built SimpleSampler.vst3 (found automatically when empty)
    [switch]$Elevated
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
. "$root\scripts\common.ps1"

function Wait-Key { if ($Elevated) { Write-Host ""; Read-Host "Press Enter to close" | Out-Null } }

try {
    if (-not $Source) {
        $Source = Join-Path (Get-BuildDir $root) "SimpleSampler_artefacts\Release\VST3\SimpleSampler.vst3"
        if (-not (Test-Path $Source)) {
            Write-Host "SimpleSampler.vst3 has not been built yet - building it now (this takes a few minutes)..."
            & "$root\build.ps1" -NoTests
        }
    }
    if (-not (Test-Path $Source)) { throw "Built plugin not found: $Source" }

    $destDir = Join-Path $env:CommonProgramFiles "VST3"
    $dest    = Join-Path $destDir "SimpleSampler.vst3"

    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
                   [Security.Principal.WindowsBuiltInRole]::Administrator)

    if (-not $isAdmin) {
        Write-Host "Administrator rights are needed to copy into $destDir - a Windows prompt will appear."
        $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-Source", "`"$Source`"", "-Elevated")
        $p = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -PassThru -Wait
        if ($p.ExitCode -ne 0) { throw "Installation did not finish (exit code $($p.ExitCode))." }
        Write-Host "Installed: $dest"
        exit 0
    }

    if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Force $destDir | Out-Null }
    if (Test-Path $dest) {
        try { Remove-Item -Recurse -Force $dest }
        catch { throw "Could not replace the installed plugin. Close Studio One (or any host using SimpleSampler) and run this again." }
    }
    Copy-Item -Recurse -Force $Source $dest

    Write-Host ""
    Write-Host "Installed: $dest"
    Write-Host "Start Studio One - SimpleSampler appears under Instruments (vendor: HomeMade)."
    Wait-Key
    exit 0
}
catch {
    Write-Host ""
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    Wait-Key
    exit 1
}
