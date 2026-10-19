param([int]$Jobs = 6, [string]$BuildDirectory = '.build/package-consumer-modules')
. "$PSScriptRoot/common.ps1"
Enable-NativeEnvironment
Enable-RuntimeEnvironment
$env:PATH = "$NativeInstallRoot/bin;$env:PATH"
$build = Join-Path $NativeRoot $BuildDirectory
$prefix = "$NativeInstallRoot;$InstallRoot;$DepsRoot/cs-installed/x64-geostudio-cs"
Invoke-Checked 'cmake' (@('-S', "$NativeRoot/examples/package-consumer", '-B', $build) + (Get-DependencyArguments) + @("-DCMAKE_PREFIX_PATH=$prefix", "-Dspdlog_DIR=$NativeInstallRoot/lib/cmake/spdlog", "-Dglm_DIR=$InstallRoot/share/glm"))
Invoke-Checked 'cmake' @('--build', $build, '--parallel', "$Jobs")
Invoke-Checked "$build/native-sdk-consumer.exe" @()
