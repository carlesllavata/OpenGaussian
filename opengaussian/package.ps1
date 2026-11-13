# SPDX-License-Identifier: GPL-3.0-or-later
<#
.SYNOPSIS
  Packages an OpenGaussian build as opengaussian-<version>-windows-x64.zip.

.DESCRIPTION
  Runs `cmake --install` on a finished portable build into a clean staging folder (upstream's layout:
  bin\OpenGaussian.exe, share\LichtFeld-Studio, Lib, DLLs, extensions, licences and THIRD-PARTY-LICENSES.md),
  checks the result and zips the folder's contents, then writes SHA256SUMS.txt next to the zip.
  The exe stays in bin\ because the program finds its resources and Python relative to it.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File opengaussian\package.ps1 -BuildDir C:\dev\OpenGaussian-build\release -OutDir C:\dev\OpenGaussian-dist
#>
param(
    [Parameter(Mandatory)] [string] $BuildDir,
    [Parameter(Mandatory)] [string] $OutDir,
    [string] $VsDir = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools",
    [switch] $AllowDirty
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$cmakeText = Get-Content (Join-Path $PSScriptRoot 'OpenGaussian.cmake') -Raw
if ($cmakeText -notmatch 'set\(OG_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\)') { throw 'OG_VERSION not found in OpenGaussian.cmake' }
$version = $Matches[1]

$dirty = & git -C $repo status --porcelain --untracked-files=no
if ($dirty -and -not $AllowDirty) { throw "The working tree has uncommitted changes; commit them and rebuild (or pass -AllowDirty for a test package)." }

$exeBuilt = Join-Path $BuildDir 'OpenGaussian.exe'
if (-not (Test-Path $exeBuilt)) { throw "No OpenGaussian.exe in $BuildDir; build first." }
$builtVersion = (Get-Item $exeBuilt).VersionInfo.ProductVersion
if ($builtVersion -ne $version) { throw "The build is version $builtVersion but OpenGaussian.cmake says $version; rebuild." }

# The install's runtime-dependency scan walks into Windows system DLLs; on some machines it then cannot resolve
# VCRUNTIME140_1.dll although Windows has it. A copy in the build folder (one of the scan's search folders)
# fixes that; the MSVC runtime is redistributable and is installed next to the exe anyway.
$crt = Get-ChildItem (Join-Path $VsDir 'VC\Redist\MSVC') -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT\vcruntime140_1.dll' } |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if ($crt -and -not (Test-Path (Join-Path $BuildDir 'vcruntime140_1.dll'))) { Copy-Item $crt $BuildDir }

$name = "opengaussian-$version-windows-x64"
$stage = Join-Path $OutDir $name
$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force $OutDir | Out-Null

$vcvars = Join-Path $VsDir 'VC\Auxiliary\Build\vcvars64.bat'
$log = Join-Path $OutDir "$name-install.log"
Write-Host "Installing into $stage ..."
cmd /c "call `"$vcvars`" >nul && cmake --install `"$BuildDir`" --prefix `"$stage`" > `"$log`" 2>&1"
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed; see $log" }

# Checks: the exe and its version, our branding, nvJPEG, the licences.
$exe = Join-Path $stage 'bin\OpenGaussian.exe'
$required = @($exe, (Join-Path $stage 'bin\nvjpeg64_12.dll'), (Join-Path $stage 'LICENSE'),
    (Join-Path $stage 'LICENSE-EXCEPTION.txt'), (Join-Path $stage 'THIRD-PARTY-LICENSES.md'))
foreach ($file in $required) { if (-not (Test-Path $file)) { throw "Missing in the package: $file" } }
if ((Get-Item $exe).VersionInfo.ProductVersion -ne $version) { throw 'Installed exe has the wrong version' }
$locales = Get-ChildItem (Join-Path $stage 'share\LichtFeld-Studio\locales') -Filter *.json | ForEach-Object BaseName
if (($locales | Sort-Object) -join ',' -ne 'en,es') { throw "Unexpected locales in the package: $($locales -join ',')" }

Write-Host "Zipping $zip ..."
$entries = (Get-ChildItem -Force $stage).Name  # explicit names: tar on Windows does not expand wildcards
& tar.exe -a -c -f $zip -C $stage @entries
if ($LASTEXITCODE -ne 0) { throw 'tar failed' }

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
Set-Content -Path (Join-Path $OutDir 'SHA256SUMS.txt') -Value "$hash  $name.zip" -Encoding ascii
$sizeMb = [math]::Round((Get-Item $zip).Length / 1MB)
Write-Host "Done: $zip ($sizeMb MB), sha256 $hash"
