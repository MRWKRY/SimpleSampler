# Shared helpers for build.ps1 / install.ps1

function Get-VSWhere {
    $p = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $p) { return $p }
    return $null
}

# Finds cmake.exe: PATH -> standard install -> the copy bundled with Visual Studio.
function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $std = "$env:ProgramFiles\CMake\bin\cmake.exe"
    if (Test-Path $std) { return $std }

    $vswhere = Get-VSWhere
    if ($vswhere) {
        $paths = & $vswhere -all -products * -property installationPath
        foreach ($p in $paths) {
            $c = Join-Path $p "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $c) { return $c }
        }
    }
    throw "cmake.exe not found. Install it with:  winget install Kitware.CMake"
}

# Picks a Visual Studio generator that has the C++ toolset installed (prefers VS 2022).
function Get-VSGenerator {
    $vswhere = Get-VSWhere
    if (-not $vswhere) { throw "Visual Studio not found. Install 'Visual Studio 2022' with the 'Desktop development with C++' workload." }
    $has2022 = & $vswhere -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($has2022) { return "Visual Studio 17 2022" }
    $has2026 = & $vswhere -version "[18.0,19.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($has2026) { return "Visual Studio 18 2026" }
    throw "No Visual Studio C++ toolset found. Add the 'Desktop development with C++' workload in the Visual Studio Installer."
}

# MSVC cannot handle paths longer than 260 characters and JUCE has deep folders,
# so when the project lives in a long path the build folder is placed in %LOCALAPPDATA%.
function Get-BuildDir([string]$root) {
    if ($env:SIMPLESAMPLER_BUILD_DIR) { return $env:SIMPLESAMPLER_BUILD_DIR }
    if ($root.Length -le 60) { return (Join-Path $root "build") }
    return (Join-Path $env:LOCALAPPDATA "SimpleSamplerBuild")
}
