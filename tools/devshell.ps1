# Configures the current PowerShell session for building Velocity:
# MSVC toolset (located via vswhere) + portable Windows SDK from external/.
# Dot-source this:   . tools\devshell.ps1
# Requires tools\setup-devenv.ps1 to have been run once.

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$External = Join-Path $RepoRoot 'external'

# --- MSVC -------------------------------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'No Visual Studio with C++ toolset found.' }
$msvcVer = (Get-ChildItem "$vsRoot\VC\Tools\MSVC" | Sort-Object Name | Select-Object -Last 1).Name
$msvc = "$vsRoot\VC\Tools\MSVC\$msvcVer"

# --- Portable Windows SDK ----------------------------------------------------
$sdkInc = Get-ChildItem "$External\winsdk-headers\c\Include" -Directory | Select-Object -Last 1
$sdkLib = "$External\winsdk-libs-x64\c"
$sdkBin = Get-ChildItem "$External\winsdk-buildtools\bin" -Directory | Where-Object Name -match '^10\.' | Select-Object -Last 1
if (-not (Test-Path $sdkLib)) { throw 'Portable SDK missing — run tools\setup-devenv.ps1 first.' }

$env:INCLUDE = @(
    "$msvc\include"
    "$($sdkInc.FullName)\ucrt"
    "$($sdkInc.FullName)\um"
    "$($sdkInc.FullName)\shared"
    "$($sdkInc.FullName)\winrt"
    "$($sdkInc.FullName)\cppwinrt"
) -join ';'

$env:LIB = @(
    "$msvc\lib\x64"
    "$sdkLib\ucrt\x64"
    "$sdkLib\um\x64"
) -join ';'

$env:Path = @(
    "$msvc\bin\Hostx64\x64"
    "$($sdkBin.FullName)\x64"
    [Environment]::GetEnvironmentVariable('Path','Machine')
    [Environment]::GetEnvironmentVariable('Path','User')
) -join ';'

$env:VELOCITY_FFMPEG_ROOT = Join-Path $External 'ffmpeg'

Write-Host "devshell ready: MSVC $msvcVer · SDK $($sdkInc.Name) · FFmpeg $env:VELOCITY_FFMPEG_ROOT"
