param(
    [string]$QtRoot = "C:\Qt\6.11.0\msvc2022_64",
    [string]$OpenCvBin = "D:\SomeLibrarys\OpenCV4.11MSVC\build\x64\vc16\bin"
)

$ErrorActionPreference = "Stop"

function Test-Cmd($Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        Write-Host "[FAIL] $Name not found in current shell."
        return $false
    }
    Write-Host "[OK]   $Name -> $($cmd.Source)"
    return $true
}

Write-Host "=== HomeAutomationSystem build env check ==="

$ok = $true
$ok = (Test-Cmd "cmake") -and $ok
$hasMsbuild = Test-Cmd "msbuild"
$hasCl = Test-Cmd "cl"

if (-not $hasMsbuild -or -not $hasCl) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($vsInstall)) {
            Write-Host "[WARN] MSVC is installed but this shell is not a VS developer shell."
            Write-Host "       Install path: $vsInstall"
        } else {
            Write-Host "[FAIL] VS Build Tools (C++) not found."
            $ok = $false
        }
    } else {
        Write-Host "[FAIL] vswhere.exe not found."
        $ok = $false
    }
}

if (Test-Path $QtRoot) {
    Write-Host "[OK]   Qt root exists: $QtRoot"
} else {
    Write-Host "[FAIL] Qt root missing: $QtRoot"
    $ok = $false
}

if (Test-Path $OpenCvBin) {
    Write-Host "[OK]   OpenCV bin exists: $OpenCvBin"
} else {
    Write-Host "[WARN] OpenCV bin missing: $OpenCvBin"
    Write-Host "       (Only needed when you start adding OpenCV/MediaPipe runtime in this app)"
}

if (-not $ok) {
    Write-Host ""
    Write-Host "Environment check failed."
    exit 1
}

Write-Host ""
Write-Host "Environment check passed."
