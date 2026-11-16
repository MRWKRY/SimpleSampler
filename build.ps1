# SimpleSampler build script
#   .\build.ps1            -> configure + build Release (VST3, Standalone, tests) and run the tests
#   .\build.ps1 -Clean     -> delete the build folder first
#   .\build.ps1 -NoTests   -> skip running the tests
param(
    [switch]$Clean,
    [switch]$NoTests,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
. "$root\scripts\common.ps1"

$cmake    = Find-CMake
$buildDir = Get-BuildDir $root
Write-Host "CMake     : $cmake"
Write-Host "Build dir : $buildDir"

if ($Clean -and (Test-Path $buildDir)) { Remove-Item -Recurse -Force $buildDir }

$generator = Get-VSGenerator
Write-Host "Generator : $generator"

& $cmake -S $root -B $buildDir -G $generator -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

# MSBuild's incremental build has been seen to miss header changes for this project
# (stale object files then get linked together with new ones). The project's own
# sources compile quickly, so their object files are always rebuilt; JUCE's are kept.
$ownSources = @(Get-ChildItem -Recurse -Filter *.cpp (Join-Path $root "Source")) + @(Get-ChildItem -Filter *.cpp (Join-Path $root "Tests"))
foreach ($dir in @("SimpleSampler.dir", "SimpleSamplerTests.dir")) {
    $objDir = Join-Path $buildDir "$dir\$Config"
    if (Test-Path $objDir) {
        foreach ($src in $ownSources) {
            $obj = Join-Path $objDir ($src.BaseName + ".obj")
            if (Test-Path $obj) { Remove-Item -Force $obj }
        }
    }
}

& $cmake --build $buildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

if (-not $NoTests) {
    $tests = Join-Path $buildDir "SimpleSamplerTests_artefacts\$Config\SimpleSamplerTests.exe"
    if (Test-Path $tests) {
        & $tests
        if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
    }
}

$vst3 = Join-Path $buildDir "SimpleSampler_artefacts\$Config\VST3\SimpleSampler.vst3"
Write-Host ""
Write-Host "Build OK."
Write-Host "VST3       : $vst3"
Write-Host "Standalone : $(Join-Path $buildDir "SimpleSampler_artefacts\$Config\Standalone\SimpleSampler.exe")"
Write-Host "Run .\install.ps1 to install the VST3."
