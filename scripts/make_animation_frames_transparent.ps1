param(
    [string]$RootDir = "resources/images",
    [double]$Similarity = 0.08,
    [double]$Blend = 0.0,
    [switch]$Overwrite = $true,
    [string]$Suffix = "_transparent"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    throw "ffmpeg is not installed or not in PATH."
}

if ($Similarity -lt 0.0 -or $Similarity -gt 1.0) {
    throw "Similarity must be within [0, 1]."
}
if ($Blend -lt 0.0 -or $Blend -gt 1.0) {
    throw "Blend must be within [0, 1]."
}

$root = (Resolve-Path -LiteralPath $RootDir).Path
$pngFiles = Get-ChildItem -LiteralPath $root -Recurse -File -Filter *.png |
    Where-Object {
        -not $_.BaseName.EndsWith($Suffix, [System.StringComparison]::OrdinalIgnoreCase) -and
        -not $_.BaseName.EndsWith("_tmp_transparent", [System.StringComparison]::OrdinalIgnoreCase)
    }

if ($pngFiles.Count -eq 0) {
    Write-Output "No PNG files found under: $root"
    exit 0
}

$similarityText = [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:0.####}", $Similarity)
$blendText = [string]::Format([System.Globalization.CultureInfo]::InvariantCulture, "{0:0.####}", $Blend)

$processed = New-Object System.Collections.Generic.List[string]

foreach ($file in $pngFiles) {
    $outputPath = if ($Overwrite) {
        Join-Path $file.DirectoryName ($file.BaseName + "_tmp_transparent" + $file.Extension)
    } else {
        Join-Path $file.DirectoryName ($file.BaseName + $Suffix + $file.Extension)
    }

    & ffmpeg -y -hide_banner -loglevel error `
        -i $file.FullName `
        -vf "format=rgba,colorkey=0xFFFFFF:${similarityText}:${blendText}" `
        -update 1 -frames:v 1 $outputPath

    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed for: $($file.FullName)"
    }

    if ($Overwrite) {
        Move-Item -Force -LiteralPath $outputPath -Destination $file.FullName
        $processed.Add($file.FullName) | Out-Null
    } else {
        $processed.Add($outputPath) | Out-Null
    }
}

Write-Output "Done. Processed PNG frames: $($processed.Count)"
Write-Output "Root: $root"
