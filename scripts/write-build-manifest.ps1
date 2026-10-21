# Record the actual SDK source and installed binary identities for downstream packaging.
param([string]$InstallDirectory = '')
. "$PSScriptRoot/common.ps1"
if ($InstallDirectory) { $NativeInstallRoot = [IO.Path]::GetFullPath($InstallDirectory) }
$sourceFiles = [ordered]@{}
$paths = @('CMakeLists.txt', 'dependencies.lock.json', 'cmake', 'src', 'resources', 'provenance', 'scripts', 'tests')
foreach ($item in $paths) {
    Get-ChildItem -LiteralPath (Join-Path $NativeRoot $item) -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($NativeRoot, $_.FullName).Replace('\', '/')
        $sourceFiles[$relative] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$binaries = [ordered]@{}
Get-ChildItem -LiteralPath "$NativeInstallRoot/bin" -Filter '*.dll' -File | Sort-Object Name | ForEach-Object {
    $binaries[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$manifest = [ordered]@{ schemaVersion = 1; task = 'NativeSDK-004'; sdkVersion = '0.3.0'; configuration = 'Windows x64 MSVC Release /MD'; dependencyRoot = $DepsRoot; sources = $sourceFiles; binaries = $binaries }
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$NativeInstallRoot/share/GeoStudioNative/build-manifest.json" -Encoding utf8
