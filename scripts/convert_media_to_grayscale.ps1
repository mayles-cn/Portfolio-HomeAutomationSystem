param(
    [string]$MediaRoot = "resources/videos",
    [string]$Suffix = "_gray",
    [switch]$Overwrite
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    throw "ffmpeg is not installed or not in PATH."
}

$rootPath = Resolve-Path -LiteralPath $MediaRoot
$root = $rootPath.Path

$videoExtensions = @(".mp4", ".mov", ".avi", ".mkv", ".wmv", ".flv", ".webm", ".m4v")
$coverExtensions = @(".png", ".jpg", ".jpeg", ".webp", ".bmp")

$files = Get-ChildItem -LiteralPath $root -File
$videos = $files | Where-Object {
    ($videoExtensions -contains $_.Extension.ToLowerInvariant()) -and
    (-not $_.BaseName.EndsWith($Suffix, [System.StringComparison]::OrdinalIgnoreCase))
}
$covers = $files | Where-Object {
    ($coverExtensions -contains $_.Extension.ToLowerInvariant()) -and
    (-not $_.BaseName.EndsWith($Suffix, [System.StringComparison]::OrdinalIgnoreCase))
}

$created = New-Object System.Collections.Generic.List[string]

function Invoke-Ffmpeg {
    param(
        [string[]]$Arguments
    )
    & ffmpeg @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed with args: $($Arguments -join ' ')"
    }
}

foreach ($video in $videos) {
    $outputFile = if ($Overwrite) {
        Join-Path $video.DirectoryName ($video.BaseName + "_tmp_gray" + $video.Extension)
    } else {
        Join-Path $video.DirectoryName ($video.BaseName + $Suffix + $video.Extension)
    }

    Invoke-Ffmpeg -Arguments @(
        "-y",
        "-i", $video.FullName,
        "-vf", "format=gray",
        "-c:v", "libx264",
        "-preset", "medium",
        "-crf", "18",
        "-c:a", "copy",
        $outputFile
    )

    if ($Overwrite) {
        Move-Item -Force -LiteralPath $outputFile -Destination $video.FullName
        $created.Add($video.FullName) | Out-Null
    } else {
        $created.Add($outputFile) | Out-Null
    }
}

foreach ($cover in $covers) {
    $outputFile = if ($Overwrite) {
        Join-Path $cover.DirectoryName ($cover.BaseName + "_tmp_gray" + $cover.Extension)
    } else {
        Join-Path $cover.DirectoryName ($cover.BaseName + $Suffix + $cover.Extension)
    }

    Invoke-Ffmpeg -Arguments @(
        "-y",
        "-i", $cover.FullName,
        "-vf", "format=gray",
        "-update", "1",
        "-frames:v", "1",
        $outputFile
    )

    if ($Overwrite) {
        Move-Item -Force -LiteralPath $outputFile -Destination $cover.FullName
        $created.Add($cover.FullName) | Out-Null
    } else {
        $created.Add($outputFile) | Out-Null
    }
}

Write-Output "Done. Processed videos=$($videos.Count), covers=$($covers.Count)."
Write-Output "Output files:"
$created | ForEach-Object { Write-Output $_ }
