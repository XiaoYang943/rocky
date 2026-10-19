param([int]$Jobs = 6, [switch]$WithTests)
. "$PSScriptRoot/common.ps1"
& "$PSScriptRoot/verify-sources.ps1"
Enable-NativeEnvironment
Enable-RuntimeEnvironment
$build = Join-Path $NativeRoot '.build/modules-r1'
$vcpkg = Get-Source 'vcpkg'
$env:VCPKG_ROOT = $vcpkg
$arguments = @("-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake", '-DVCPKG_MANIFEST_MODE=OFF', '-DVCPKG_TARGET_TRIPLET=x64-geostudio-cs', "-DVCPKG_OVERLAY_TRIPLETS=$NativeRoot/cmake/triplets", "-DVCPKG_INSTALLED_DIR=$DepsRoot/cs-installed", '-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON', "-Dspdlog_DIR=$InstallRoot/lib/cmake/spdlog", "-Dglm_DIR=$InstallRoot/share/glm", "-DCURL_DIR=$NativeRoot/cmake/sdk", '-DCMAKE_OBJECT_PATH_MAX=240', "-DGEOSTUDIO_DEPENDENCIES=$DepsRoot", "-DCMAKE_INSTALL_PREFIX=$NativeInstallRoot")
$arguments += "-DGEOSTUDIO_BUILD_TESTS=$($WithTests.IsPresent)"
Invoke-Checked 'cmake' (@('-S', $NativeRoot, '-B', $build) + (Get-DependencyArguments) + $arguments)
Invoke-Checked 'cmake' @('--build', $build, '--target', 'geostudio-globe', 'geostudio-io', 'geostudio-tiles3d', '--parallel', "$Jobs")
if ($WithTests) {
    Invoke-Checked 'cmake' @('--build', $build, '--target', 'geostudio-globe-tests', '--parallel', "$Jobs")
    Invoke-Checked 'ctest' @('--test-dir', $build, '--output-on-failure')
}
# Install the SDK libraries and public package without standalone applications.
foreach ($part in @('spdlog', 'src/io', 'src/globe', 'src/tiles3d', '')) {
    Invoke-Checked 'cmake' @('--install', (Join-Path $build $part), '--config', 'Release', '--component', 'Unspecified', '--prefix', $NativeInstallRoot)
}
& "$PSScriptRoot/write-build-manifest.ps1"
Write-Host "GeoStudio Native SDK installed at $NativeInstallRoot"
