param([int]$Jobs = 6, [switch]$Offline, [string]$From = '', [string]$Only = '', [switch]$WithHostUi)
. "$PSScriptRoot/common.ps1"
Enable-NativeEnvironment
Enable-RuntimeEnvironment
New-Item -ItemType Directory -Force (Join-Path $DepsRoot 'downloads'), (Join-Path $DepsRoot 'src'), (Join-Path $NativeRoot 'out/logs') | Out-Null
$hostUi = $WithHostUi -or $Only -eq 'imgui' -or $From -eq 'imgui'
foreach ($entry in ($SourceLock.sources | Where-Object { $_.name -notin @('rocky', 'vsgCs', 'vsgXchange', 'cesium-native', 'vcpkg') -and ($_.name -ne 'imgui' -or $hostUi) })) {
    $archive = Join-Path $DepsRoot "downloads/$($entry.name)-$($entry.commit).zip"
    if (-not (Test-Path $archive)) {
        if ($Offline) { throw "Missing archive: $archive" }
        Invoke-WebRequest -Uri $entry.url -OutFile $archive
    }
    if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.archiveSha256) { throw "Archive hash mismatch: $archive. File preserved for inspection." }
    $source = Get-Source $entry.name
    if (-not (Test-Path $source)) { Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $DepsRoot 'src') }
}
$steps = @(
    @{ Name = 'vulkan-headers'; Options = @('-DVULKAN_HEADERS_ENABLE_TESTS=OFF') },
    @{ Name = 'vulkan-loader'; Options = @('-DBUILD_TESTS=OFF', '-DUPDATE_DEPS=OFF') },
    @{ Name = 'glslang'; Options = @('-DBUILD_SHARED_LIBS=OFF', '-DENABLE_OPT=OFF', '-DBUILD_EXTERNAL=OFF', '-DENABLE_HLSL=OFF', '-DGLSLANG_TESTS=OFF', '-DGLSLANG_ENABLE_INSTALL=ON') },
    @{ Name = 'glm'; Options = @('-DGLM_BUILD_LIBRARY=OFF', '-DGLM_BUILD_TESTS=OFF', '-DGLM_BUILD_INSTALL=ON') },
    @{ Name = 'entt'; Options = @('-DENTT_BUILD_TESTING=OFF', '-DENTT_INSTALL=ON') },
    @{ Name = 'spdlog'; Options = @('-DSPDLOG_BUILD_SHARED=OFF', '-DSPDLOG_BUILD_EXAMPLE=OFF', '-DSPDLOG_BUILD_TESTS=OFF') },
    @{ Name = 'imgui'; Options = @("-DIMGUI_SOURCE=$((Get-Source 'imgui').Replace('\', '/'))") },
    @{ Name = 'assimp'; Options = @('-DBUILD_SHARED_LIBS=ON', '-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF', '-DASSIMP_BUILD_OBJ_IMPORTER=ON', '-DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=OFF', '-DASSIMP_BUILD_TESTS=OFF', '-DASSIMP_BUILD_ASSIMP_TOOLS=OFF', '-DASSIMP_INSTALL=ON', '-DASSIMP_BUILD_ZLIB=OFF') },
    @{ Name = 'vsg'; Options = @('-DBUILD_SHARED_LIBS=ON', '-DVSG_SUPPORTS_ShaderCompiler=ON', '-DVSG_SUPPORTS_ShaderOptimizer=OFF') }
)
$started = ($From -eq '')
foreach ($step in $steps) {
    if ($step.Name -eq 'imgui' -and -not $hostUi) { continue }
    if ($Only -and $step.Name -ne $Only) { continue }
    if ($step.Name -eq $From) { $started = $true }
    if (-not $started) { continue }
    $source = if ($step.Name -eq 'imgui') { Join-Path $NativeRoot 'cmake/imgui' } else { Get-Source $step.Name }
    $build = Join-Path $DepsRoot "build/$($step.Name)"
    Write-Host "Building $($step.Name)"
    $configureArgs = @('-S', $source, '-B', $build) + (Get-DependencyArguments) + $step.Options
    Invoke-Checked 'cmake' $configureArgs
    Invoke-Checked 'cmake' @('--build', $build, '--parallel', "$Jobs")
    Invoke-Checked 'cmake' @('--install', $build)
}
if (-not $started) { throw "Unknown -From dependency: $From" }
Write-Host 'Base dependencies are ready. Run scripts/bootstrap-cesium.ps1, then scripts/build.ps1.'