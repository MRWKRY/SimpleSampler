# Installs SimpleSampler.vst3 into  C:\Program Files\Common Files\VST3
# (builds it first if it has not been built yet). Asks for administrator rights by itself.
#
# The copy is verified afterwards (file hash), so "Installed" really means the new version is in place.
# If the old plugin file is in use (Studio One still running), it is renamed out of the way instead of
# failing half-way; Studio One then picks up the new version the next time it starts.
param(
    [string]$Source = "",       # path to a built SimpleSampler.vst3 (found automatically when empty)
    [switch]$Elevated,          # internal: this is the elevated copy step
    [switch]$NoPause,           # do not wait for a key press in the elevated window
    [string]$LogFile = ""       # internal: the elevated step writes its messages here
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
. "$root\scripts\common.ps1"

$binaryRel = "Contents\x86_64-win\SimpleSampler.vst3"

function Say([string]$text, [string]$colour = "") {
    if ($colour) { Write-Host $text -ForegroundColor $colour } else { Write-Host $text }
    if ($LogFile) { Add-Content -Path $LogFile -Value $text -Encoding UTF8 }
}

function Wait-Key { if ($Elevated -and -not $NoPause) { Write-Host ""; Read-Host "Press Enter to close" | Out-Null } }

function Get-Hosts {
    @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^(Studio One|PlugInScanner|Plug-in Scanner|SimpleSampler)$' })
}

function Describe([string]$bundle) {
    $f = Join-Path $bundle $binaryRel
    if (-not (Test-Path $f)) { return "(missing)" }
    $i = Get-Item $f
    return "{0:yyyy-MM-dd HH:mm:ss}, {1:N0} bytes" -f $i.LastWriteTime, $i.Length
}

function Same-Binary([string]$a, [string]$b) {
    $fa = Join-Path $a $binaryRel; $fb = Join-Path $b $binaryRel
    if (-not (Test-Path $fa) -or -not (Test-Path $fb)) { return $false }
    return (Get-FileHash $fa).Hash -eq (Get-FileHash $fb).Hash
}

# Copies one file. If the existing file cannot be overwritten because a host has it loaded,
# it is renamed (Windows allows renaming a loaded DLL) and the new file is put in its place.
function Copy-Replacing([string]$from, [string]$to) {
    $dir = Split-Path $to -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }

    try { Copy-Item -Force $from $to; return }
    catch {
        if (-not (Test-Path $to)) { throw }
        $aside = "$to.old-$([DateTime]::Now.Ticks)"
        try { Rename-Item -Path $to -NewName (Split-Path $aside -Leaf) }
        catch { throw "The installed plugin file is in use and could not be replaced: $to`nClose Studio One completely (check the task bar / Task Manager) and run install.bat again." }
        Copy-Item -Force $from $to
        Say "  (the old file was in use and has been set aside; restart Studio One to load the new version)" "Yellow"
    }
}

try {
    if (-not $Source) {
        $Source = Join-Path (Get-BuildDir $root) "SimpleSampler_artefacts\Release\VST3\SimpleSampler.vst3"
        if (-not (Test-Path $Source)) {
            Write-Host "SimpleSampler.vst3 has not been built yet - building it now (this takes a few minutes)..."
            & "$root\build.ps1" -NoTests
        }
    }
    if (-not (Test-Path (Join-Path $Source $binaryRel))) { throw "Built plugin not found: $Source" }

    $destDir = Join-Path $env:CommonProgramFiles "VST3"
    $dest    = Join-Path $destDir "SimpleSampler.vst3"

    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
                   [Security.Principal.WindowsBuiltInRole]::Administrator)

    if (-not $isAdmin) {
        Write-Host "New build : $(Describe $Source)"
        Write-Host "Installed : $(Describe $dest)"

        if (Same-Binary $Source $dest) {
            Write-Host ""
            Write-Host "The installed plugin is already this build. Nothing to do." -ForegroundColor Green
            exit 0
        }

        $running = Get-Hosts
        if ($running.Count -gt 0) {
            Write-Host ""
            Write-Host "NOTE: still running: $(($running | ForEach-Object { $_.ProcessName } | Sort-Object -Unique) -join ', ')" -ForegroundColor Yellow
            Write-Host "      Close it first if you can. Otherwise the new version loads after the next restart of Studio One." -ForegroundColor Yellow
        }

        Write-Host ""
        Write-Host "Administrator rights are needed to copy into $destDir - a Windows prompt will appear."
        $log = Join-Path $env:TEMP "SimpleSampler-install.log"
        if (Test-Path $log) { Remove-Item -Force $log }
        $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-Source", "`"$Source`"", "-Elevated", "-NoPause", "-LogFile", "`"$log`"")
        $p = Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden
        if (Test-Path $log) { Get-Content $log -Encoding UTF8 | ForEach-Object { Write-Host "  $_" } }

        # never trust the exit code alone: look at what is really installed
        Write-Host ""
        Write-Host "Installed now: $(Describe $dest)"
        if (-not (Same-Binary $Source $dest)) {
            throw "The installed plugin is NOT the new build (exit code $($p.ExitCode)). Close Studio One completely and run install.bat again."
        }
        Write-Host "OK - the new version is installed and verified." -ForegroundColor Green
        Write-Host "Start Studio One: the SimpleSampler window shows its version next to the name (top left)."
        exit 0
    }

    # ---------------------------------------------------------------- elevated: copy
    if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Force $destDir | Out-Null }

    # leftovers of earlier installs that were in use at the time
    if (Test-Path $dest) {
        Get-ChildItem $dest -Recurse -File -Filter "*.old-*" -ErrorAction SilentlyContinue | ForEach-Object {
            try { Remove-Item -Force $_.FullName } catch { }
        }
    }

    foreach ($file in Get-ChildItem $Source -Recurse -File) {
        $relative = $file.FullName.Substring($Source.Length).TrimStart('\')
        Copy-Replacing $file.FullName (Join-Path $dest $relative)
    }

    if (-not (Same-Binary $Source $dest)) { throw "Copy finished but the installed file differs from the build." }

    Say "Copied to $dest"
    Wait-Key
    exit 0
}
catch {
    Say ""
    Say "ERROR: $($_.Exception.Message)" "Red"
    Wait-Key
    exit 1
}
