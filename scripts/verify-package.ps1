param([int]$Jobs = 6, [string]$BuildDirectory = '.build/package-consumer-layout', [string]$InstallDirectory = '')
. "$PSScriptRoot/common.ps1"
if ($InstallDirectory) { $NativeInstallRoot = [IO.Path]::GetFullPath($InstallDirectory) }
Enable-NativeEnvironment
Enable-RuntimeEnvironment
$env:PATH = "$NativeInstallRoot/bin;$env:PATH"
$build = Join-Path $NativeRoot $BuildDirectory
$prefix = "$NativeInstallRoot;$InstallRoot;$DepsRoot/cs-installed/x64-geostudio-cs"
Invoke-Checked 'cmake' (@('-S', "$NativeRoot/tests/package-consumer", '-B', $build) + (Get-DependencyArguments) + @("-DCMAKE_PREFIX_PATH=$prefix", "-DGeoStudioNative_DIR=$NativeInstallRoot/lib/cmake/GeoStudioNative", "-Dspdlog_DIR=$NativeInstallRoot/lib/cmake/spdlog", "-Dglm_DIR=$InstallRoot/share/glm"))
Invoke-Checked 'cmake' @('--build', $build, '--parallel', "$Jobs")
Invoke-Checked "$build/native-sdk-consumer.exe" @()
Invoke-Checked "$build/native-sdk-core-consumer.exe" @()
Invoke-Checked 'python' @('-B', "$NativeRoot/tests/architecture/check_boundaries.py", '--installed', $NativeInstallRoot)
