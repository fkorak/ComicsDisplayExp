<#
.SYNOPSIS
    Builds ComicsDisplayExp and packages it into a Windows installer (Inno Setup).

.DESCRIPTION
    Used both locally and by the GitHub workflow, so both produce the same setup:
      1. NuGet restore + Release build (skipped with -SkipBuild)
      2. Inno Setup compiles installer\ComicsDisplayExp.iss
    The result is dist\ComicsDisplayExp-Setup-<version>-<platform>.exe.

.EXAMPLE
    .\installer\build-setup.ps1
.EXAMPLE
    .\installer\build-setup.ps1 -Platform ARM64 -Version 1.2.0
.EXAMPLE
    .\installer\build-setup.ps1 -InstallInnoSetup    # installs Inno Setup (per user) if missing
#>
param(
    [ValidateSet('x64', 'ARM64')]
    [string]$Platform = 'x64',

    # Defaults to the newest git tag like v1.2.3, else 0.1.0.
    [string]$Version,

    # Package an existing build instead of building (the CI builds in an earlier step).
    [switch]$SkipBuild,

    # Install Inno Setup through winget (current user, no admin) if it is missing.
    [switch]$InstallInnoSetup
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

# ---------------------------------------------------------------- version
if (-not $Version) {
    # Newest version tag; listing (unlike `git describe`) is silent when there are none.
    $tag = $null
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $tag = git -C $root tag --list 'v[0-9]*' --sort=-version:refname | Select-Object -First 1
    }
    $Version = if ($tag) { $tag.TrimStart('v') } else { '0.1.0' }
}
# Windows file versions are four numbers: 1.2.3-beta -> 1.2.3.0
$numbers = @(($Version -replace '[^0-9.].*$', '').Split('.') | Where-Object { $_ -ne '' } | Select-Object -First 4)
while ($numbers.Count -lt 4) { $numbers += '0' }
$numericVersion = $numbers -join '.'

# ---------------------------------------------------------------- build
$source = Join-Path $root "$Platform\Release\ComicsDisplayExp"
if (-not $SkipBuild) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    # Prefer a stable Visual Studio; fall back to a preview one.
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' |
        Select-Object -First 1
    if (-not $msbuild) {
        $msbuild = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\amd64\MSBuild.exe' |
            Select-Object -First 1
    }
    if (-not $msbuild) { throw 'MSBuild not found. Install Visual Studio with the "Desktop development with C++" workload.' }

    Write-Host "Building $Platform Release with $msbuild"
    & $msbuild "$root\ComicsDisplayExp.sln" -t:restore -p:RestorePackagesConfig=true -p:Configuration=Release -p:Platform=$Platform -v:minimal -nologo
    if ($LASTEXITCODE) { throw 'NuGet restore failed' }
    & $msbuild "$root\ComicsDisplayExp.sln" -m -p:Configuration=Release -p:Platform=$Platform -v:minimal -nologo
    if ($LASTEXITCODE) { throw 'Build failed' }
}
if (-not (Test-Path "$source\ComicsDisplayExp.exe")) { throw "No build found in $source" }

# ---------------------------------------------------------------- Inno Setup
function Find-InnoSetup {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    $found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $found) { $found = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source }
    return $found
}

$iscc = Find-InnoSetup
if (-not $iscc -and $InstallInnoSetup) {
    Write-Host 'Installing Inno Setup (current user)...'
    winget install --id JRSoftware.InnoSetup --exact --scope user --silent --accept-package-agreements --accept-source-agreements
    $iscc = Find-InnoSetup
}
if (-not $iscc) {
    throw 'Inno Setup 6 not found. Install it with "winget install JRSoftware.InnoSetup", or rerun with -InstallInnoSetup.'
}

$output = Join-Path $root 'dist'
Write-Host "Packaging $Version ($numericVersion) for $Platform with $iscc"
& $iscc /Qp "/DAppVersion=$Version" "/DNumericVersion=$numericVersion" "/DPlatform=$Platform" `
    "/DSourceDir=$source" "/DOutputDir=$output" "$PSScriptRoot\ComicsDisplayExp.iss"
if ($LASTEXITCODE) { throw 'Inno Setup failed' }

$setup = Get-Item "$output\ComicsDisplayExp-Setup-$Version-$Platform.exe"
Write-Host ("Setup: {0} ({1:N1} MB)" -f $setup.FullName, ($setup.Length / 1MB))
if ($env:GITHUB_OUTPUT) { "setup=$($setup.FullName)" >> $env:GITHUB_OUTPUT }
