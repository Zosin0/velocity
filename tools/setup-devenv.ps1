# Downloads the portable toolchain dependencies that cannot come from FetchContent:
#   1. Windows SDK headers/libs/tools as NuGet packages (no admin rights needed)
#   2. Prebuilt FFmpeg 7.1 LGPL shared binaries (BtbN autobuilds)
# Everything lands in external/ (gitignored). Idempotent: skips completed items
# via .stamp files. Safe to re-run.

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$External = Join-Path $RepoRoot 'external'
New-Item -ItemType Directory -Force $External | Out-Null

$SdkVersion = '10.0.26100.8249'
$SdkBuildToolsVersion = ''   # resolved below: latest stable 10.0.26100.*
$FFmpegUrl = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-lgpl-shared-7.1.zip'

function Get-NuGet([string]$PackageId, [string]$Version, [string]$DestName) {
    $dest = Join-Path $External $DestName
    $stamp = Join-Path $dest '.stamp'
    if ((Test-Path $stamp) -and ((Get-Content $stamp) -eq $Version)) {
        Write-Host "[skip] $PackageId $Version already present"
        return
    }
    Write-Host "[get ] $PackageId $Version"
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    $idLower = $PackageId.ToLowerInvariant()
    $zip = Join-Path $External "$idLower.zip"
    Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/$idLower/$Version/$idLower.$Version.nupkg" -OutFile $zip
    Expand-Archive $zip -DestinationPath $dest
    Remove-Item $zip
    Set-Content $stamp $Version
}

# --- Windows SDK ------------------------------------------------------------
Get-NuGet 'Microsoft.Windows.SDK.CPP'     $SdkVersion 'winsdk-headers'
Get-NuGet 'Microsoft.Windows.SDK.CPP.x64' $SdkVersion 'winsdk-libs-x64'

if (-not $SdkBuildToolsVersion) {
    $versions = (Invoke-RestMethod 'https://api.nuget.org/v3-flatcontainer/microsoft.windows.sdk.buildtools/index.json').versions
    $SdkBuildToolsVersion = $versions | Where-Object { $_ -like '10.0.26100*' -and $_ -notmatch 'preview|experimental' } | Select-Object -Last 1
}
Get-NuGet 'Microsoft.Windows.SDK.BuildTools' $SdkBuildToolsVersion 'winsdk-buildtools'

# --- FFmpeg -----------------------------------------------------------------
$ffDest = Join-Path $External 'ffmpeg'
$ffStamp = Join-Path $ffDest '.stamp'
if (-not ((Test-Path $ffStamp) -and ((Get-Content $ffStamp) -eq $FFmpegUrl))) {
    Write-Host "[get ] FFmpeg shared (LGPL, n7.1)"
    if (Test-Path $ffDest) { Remove-Item -Recurse -Force $ffDest }
    $zip = Join-Path $External 'ffmpeg.zip'
    Invoke-WebRequest $FFmpegUrl -OutFile $zip
    Expand-Archive $zip -DestinationPath $External
    Remove-Item $zip
    # The zip contains a single versioned top-level dir; normalize its name.
    $extracted = Get-ChildItem $External -Directory | Where-Object { $_.Name -like 'ffmpeg-n7.1*' } | Select-Object -First 1
    Rename-Item $extracted.FullName $ffDest
    Set-Content $ffStamp $FFmpegUrl
} else {
    Write-Host "[skip] FFmpeg already present"
}

Write-Host "`nDone. external/ contents:"
Get-ChildItem $External -Directory | ForEach-Object { "  $($_.Name)" }
