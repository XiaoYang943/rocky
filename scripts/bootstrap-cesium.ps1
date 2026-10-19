param([int]$Jobs = 6, [switch]$Offline)
. "$PSScriptRoot/common.ps1"
Enable-NativeEnvironment
New-Item -ItemType Directory -Force (Join-Path $DepsRoot 'downloads'), (Join-Path $DepsRoot 'src'), (Join-Path $NativeRoot 'out/logs') | Out-Null
# Reuse an explicitly configured Git proxy for this process only.
$proxy = & git config --get http.proxy
if ($proxy) { $env:HTTPS_PROXY = $proxy; $env:HTTP_PROXY = $proxy }
foreach ($name in @('cesium-native', 'vcpkg')) {
    $entry = $SourceLock.sources | Where-Object name -EQ $name
    $archive = Join-Path $DepsRoot "downloads/$($entry.name)-$($entry.commit).zip"
    if (-not (Test-Path -LiteralPath $archive)) {
        if ($Offline) { throw "Missing archive: $archive" }
        Invoke-Checked 'curl.exe' @('--fail', '--location', '--retry', '3', '--output', $archive, $entry.url)
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.archiveSha256) { throw "Source hash mismatch: $archive" }
    if (-not (Test-Path -LiteralPath (Get-Source $name))) { Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $DepsRoot 'src') }
}
$vcpkg = Get-Source 'vcpkg'
$env:VCPKG_ROOT = $vcpkg
$env:VCPKG_MAX_CONCURRENCY = "$Jobs"
$env:VCPKG_DISABLE_METRICS = '1'
if (-not (Test-Path -LiteralPath "$vcpkg/vcpkg.exe")) {
    if ($Offline) { throw 'Offline setup requires the pinned vcpkg tool to be bootstrapped already.' }
    & "$vcpkg/bootstrap-vcpkg.bat" -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }
}
$packages = @('asyncplusplus', 'curl', 'doctest', 'expected-lite', 'fmt', 'glm', 'ms-gsl', 'rapidjson', 'spdlog', 'stb', 'ada-url', 'draco', 'ktx[core]', 'modp-base64', 'meshoptimizer', 'openssl', 's2geometry', 'libjpeg-turbo', 'sqlite3', 'tinyxml2', 'libwebp[core]', 'zlib-ng', 'picosha2', 'earcut-hpp', 'cpp-httplib[core]', 'libmorton', 'zstd', 'blend2d', 'asmjit', 'spz', 'zlib')
$arguments = @('install', '--classic', '--triplet=x64-geostudio-cs', "--overlay-triplets=$NativeRoot/cmake/triplets", "--overlay-ports=$(Get-Source 'cesium-native')/extern/vcpkg/ports", "--x-install-root=$DepsRoot/cs-installed", "--x-buildtrees-root=$DepsRoot/cs-buildtrees", "--x-packages-root=$DepsRoot/cs-packages", '--no-print-usage')
if ($Offline) { $arguments += '--no-downloads' }
Invoke-Checked "$vcpkg/vcpkg.exe" ($arguments + $packages)
Write-Host 'Cesium Native dependencies are ready.'
