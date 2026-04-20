param(
    [string]$MediapipeRoot = "D:\SomeCppProjects\mediapipe",
    [string]$BazelExe = "C:\Tools\bazel\bazel-7.4.1.exe",
    [string]$PythonBin = "$env:LocalAppData\Programs\Python\Python312\python.exe",
    [string]$BazelSh = "C:/msys64/usr/bin/bash.exe",
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $MediapipeRoot)) {
    throw "mediapipe repo not found: $MediapipeRoot"
}
if (-not (Test-Path $BazelExe)) {
    throw "Bazel executable not found: $BazelExe"
}
if (-not (Test-Path $PythonBin)) {
    throw "Python not found: $PythonBin"
}
if (-not (Test-Path $BazelSh)) {
    throw "MSYS bash not found: $BazelSh"
}
if (-not (Test-Path $ProjectRoot)) {
    throw "project root not found: $ProjectRoot"
}

$pythonForBazel = $PythonBin -replace "\\", "//"

$env:BAZEL_SH = $BazelSh
$env:BAZEL_VS = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$env:BAZEL_VC = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC"
$env:BAZEL_VC_FULL_VERSION = "14.44.35207"
$env:BAZEL_WINSDK_FULL_VERSION = "10.0.26100.0"
$env:HERMETIC_PYTHON_VERSION = "3.12"

Set-Location $MediapipeRoot

& $BazelExe build -c opt `
    --verbose_failures `
    --cxxopt=/std:c++20 `
    --cxxopt=/Zc:preprocessor `
    --cxxopt=/utf-8 `
    --host_cxxopt=/std:c++20 `
    --host_cxxopt=/Zc:preprocessor `
    --host_cxxopt=/utf-8 `
    --conlyopt=/std:c11 `
    --conlyopt=/experimental:c11atomics `
    --conlyopt=/utf-8 `
    --host_conlyopt=/std:c11 `
    --host_conlyopt=/experimental:c11atomics `
    --host_conlyopt=/utf-8 `
    --define MEDIAPIPE_DISABLE_GPU=1 `
    --action_env PYTHON_BIN_PATH="$pythonForBazel" `
    //mediapipe/examples/desktop/hand_tracking:hand_landmarker_stream

$bridgeOutputDir = Join-Path $MediapipeRoot "bazel-bin\mediapipe\examples\desktop\hand_tracking"
$bridgeExe = Join-Path $bridgeOutputDir "hand_landmarker_stream.exe"
$opencvDll = Join-Path $bridgeOutputDir "opencv_world4110.dll"

if (-not (Test-Path $bridgeExe)) {
    Write-Host ""
    Write-Host "Build finished but bridge exe not found:"
    Write-Host "  $bridgeExe"
    exit 1
}

$toolsDir = Join-Path $ProjectRoot "tools"
New-Item -ItemType Directory -Path $toolsDir -Force | Out-Null

Copy-Item -Path $bridgeExe -Destination (Join-Path $toolsDir "hand_landmarker_stream.exe") -Force
if (Test-Path $opencvDll) {
    Copy-Item -Path $opencvDll -Destination (Join-Path $toolsDir "opencv_world4110.dll") -Force
}

Write-Host ""
Write-Host "Bridge build and sync success:"
Write-Host "  source: $bridgeExe"
Write-Host "  target: $(Join-Path $toolsDir 'hand_landmarker_stream.exe')"
if (Test-Path (Join-Path $toolsDir "opencv_world4110.dll")) {
    Write-Host "  target: $(Join-Path $toolsDir 'opencv_world4110.dll')"
}
