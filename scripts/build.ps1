param([int]$Jobs = 6, [switch]$WithTests, [string]$TestRegex = '')
. "$PSScriptRoot/common.ps1"
& "$PSScriptRoot/verify-sources.ps1"
Invoke-Checked 'python' @('-X', 'utf8', '-B', "$PSScriptRoot/apply-vsg-patch.py", '--deps', $DepsRoot)
Enable-NativeEnvironment
Enable-RuntimeEnvironment
$build = Join-Path $NativeRoot '.build/capabilities-r1'
$vcpkg = Get-Source 'vcpkg'
$env:VCPKG_ROOT = $vcpkg
$arguments = @("-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake", '-DVCPKG_MANIFEST_MODE=OFF', '-DVCPKG_TARGET_TRIPLET=x64-geostudio-cs', "-DVCPKG_OVERLAY_TRIPLETS=$NativeRoot/cmake/triplets", "-DVCPKG_INSTALLED_DIR=$DepsRoot/cs-installed", '-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON', "-Dspdlog_DIR=$InstallRoot/lib/cmake/spdlog", "-Dglm_DIR=$InstallRoot/share/glm", "-DCURL_DIR=$NativeRoot/cmake/sdk", '-DCMAKE_OBJECT_PATH_MAX=240', "-DGEOSTUDIO_DEPENDENCIES=$DepsRoot", "-DCMAKE_INSTALL_PREFIX=$NativeInstallRoot")
$arguments += "-DGEOSTUDIO_BUILD_TESTS=$($WithTests.IsPresent)"
Invoke-Checked 'cmake' (@('-S', $NativeRoot, '-B', $build) + (Get-DependencyArguments) + $arguments)
Invoke-Checked 'cmake' @('--build', $build, '--target', 'geostudio-runtime', '--parallel', "$Jobs")
if ($WithTests) {
    Invoke-Checked 'cmake' @('--build', $build, '--target', 'geostudio-globe-tests', 'geostudio-code-tests', 'geostudio-streaming-tests', '--parallel', "$Jobs")
    if (Select-String -LiteralPath "$build/CMakeCache.txt" -Pattern '^GEOSTUDIO_NATIVE_OSGB:BOOL=ON$' -Quiet) {
        Invoke-Checked 'cmake' @('--build', $build, '--target', 'geostudio-osgb-probe', '--parallel', "$Jobs")
    }
    $testArguments = @('--test-dir', $build, '--output-on-failure')
    if ($TestRegex) { $testArguments += @('-R', $TestRegex) }
    Invoke-Checked 'ctest' $testArguments
}
# Install the SDK libraries and public package without standalone applications.
foreach ($part in @('spdlog', 'src/core', 'src/data', 'src/scene', 'src/drivers', 'src/streaming', 'src/render/import', 'src/render', 'cmake/cesium-adapter', 'src/runtime', '')) {
    Invoke-Checked 'cmake' @('--install', (Join-Path $build $part), '--config', 'Release', '--component', 'Unspecified', '--prefix', $NativeInstallRoot)
}
& "$PSScriptRoot/write-build-manifest.ps1"
Write-Host "GeoStudio Native SDK installed at $NativeInstallRoot"
