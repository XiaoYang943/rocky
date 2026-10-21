Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$NativeRoot = Split-Path $PSScriptRoot -Parent
$WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $NativeRoot '../..'))
$NativeInstallRoot = Join-Path $NativeRoot 'out/install-capabilities'
$buildManifest = Join-Path $NativeInstallRoot 'share/GeoStudioNative/build-manifest.json'
if (-not (Test-Path -LiteralPath $buildManifest)) { $buildManifest = Join-Path $NativeRoot 'out/install-modules/share/GeoStudioNative/build-manifest.json' }
if (-not (Test-Path -LiteralPath $buildManifest)) { $buildManifest = Join-Path $NativeRoot 'out/install/share/GeoStudioNative/build-manifest.json' }
$rememberedDependencies = if (Test-Path -LiteralPath $buildManifest) { (Get-Content -LiteralPath $buildManifest -Raw | ConvertFrom-Json).dependencyRoot } else { $null }
$DepsRoot = if ($env:GEOSTUDIO_NATIVE_DEPS) { [IO.Path]::GetFullPath($env:GEOSTUDIO_NATIVE_DEPS) } elseif ($rememberedDependencies) { $rememberedDependencies } else { Join-Path $NativeRoot '.deps' }
$InstallRoot = Join-Path $DepsRoot 'install'
$GdalSdk = if ($env:NG_GDAL_SDK) { $env:NG_GDAL_SDK } else { Join-Path $WorkspaceRoot 'packages/gdal/sdk/gdal-3.12.2-windows-x64-msvc-sdk' }
$SourceLock = Get-Content (Join-Path $NativeRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json
# Resolve the fixed source location; SDK-owned engines never resolve to an old cache.
function Get-Source([string]$Name) {
    if ($Name -eq 'rocky') { return $NativeRoot }
    if ($Name -eq 'vsgCs') { return $NativeRoot }
    if ($Name -eq 'vsgXchange') { return $NativeRoot }
    $entry = $SourceLock.sources | Where-Object name -EQ $Name
    if (-not $entry) { throw "Unknown source: $Name" }
    return Join-Path $DepsRoot "src/$($entry.directory)"
}
# Import the installed MSVC x64 environment into this process.
function Enable-NativeEnvironment {
    $env:VSLANG = '1033'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Install Visual Studio C++ desktop tools first.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC x64 tools were not found.' }
    Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    foreach ($command in @('cmake', 'ninja')) { if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "$command is required on PATH." } }
}
# Set local runtime resources without changing machine-wide settings.
function Enable-RuntimeEnvironment {
    if (-not (Test-Path (Join-Path $GdalSdk 'bin/gdal.dll'))) { throw "GDAL SDK missing at $GdalSdk; set NG_GDAL_SDK." }
    $env:PATH = "$(Join-Path $InstallRoot 'bin');$(Join-Path $GdalSdk 'bin');$env:PATH"
    $env:PROJ_DATA = Join-Path $GdalSdk 'share/proj'
    $env:GDAL_DATA = Join-Path $GdalSdk 'share/gdal'
    $env:GEOSTUDIO_NATIVE_FILE_PATH = "$NativeRoot/resources/globe"
    $env:VSG_FILE_PATH = "$NativeRoot/resources/tiles3d"
}
# Run one command and stop the build on a nonzero exit code.
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
# Return explicit Release dependency and shared GDAL SDK locations.
function Get-DependencyArguments {
    $prefix = $InstallRoot.Replace('\', '/')
    $sdk = $GdalSdk.Replace('\', '/')
    $bridge = (Join-Path $NativeRoot 'cmake/sdk').Replace('\', '/')
    return @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_INSTALL_PREFIX=$prefix", "-DCMAKE_PREFIX_PATH=$prefix;$bridge", "-DNG_GDAL_SDK=$sdk", "-DGDAL_INCLUDE_DIR=$sdk/include", "-DGDAL_LIBRARY=$sdk/lib/gdal.lib", "-DZLIB_INCLUDE_DIR=$sdk/include", "-DZLIB_LIBRARY=$sdk/lib/zlib.lib", "-DVulkan_INCLUDE_DIR=$prefix/include", "-DVulkan_LIBRARY=$prefix/lib/vulkan-1.lib", '-DCMAKE_POLICY_VERSION_MINIMUM=3.10')
}
