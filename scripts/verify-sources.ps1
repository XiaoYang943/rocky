# Verify mapped sources and full upstream patches without mutating the source tree.
. "$PSScriptRoot/common.ps1"
$manifest = Get-Content "$NativeRoot/provenance/manifest.json" -Raw | ConvertFrom-Json
$rootPrefix = [IO.Path]::GetFullPath($NativeRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
$expectedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $manifest.sources) {
    $fileManifest = Join-Path $NativeRoot $entry.fileManifest
    if ((Get-FileHash -LiteralPath $fileManifest -Algorithm SHA256).Hash -ne $entry.fileManifestSha256) { throw "Source map identity mismatch: $($entry.name)" }
    $files = Get-Content -LiteralPath $fileManifest -Raw | ConvertFrom-Json
    foreach ($file in $files) {
        if ($null -eq $file.path) {
            if ($file.PSObject.Properties.Name -contains 'removedPath') {
                $removed = [IO.Path]::GetFullPath((Join-Path $NativeRoot $file.removedPath))
                if (-not $removed.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Removed path escapes SDK.' }
                if (Test-Path -LiteralPath $removed) { throw "Removed source reappeared: $($file.removedPath)" }
            }
            continue
        }
        [void]$expectedPaths.Add($file.path)
        $path = [IO.Path]::GetFullPath((Join-Path $NativeRoot $file.path))
        if (-not $path.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Source path escapes SDK.' }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw "Source changed: $($file.path)" }
    }
    foreach ($deletion in $entry.binaryDeletions) {
        $original = @($files | Where-Object upstreamPath -EQ $deletion.upstreamPath)
        if ($deletion.operation -ne 'delete' -or $original.Count -ne 1 -or $null -ne $original[0].path -or
            $original[0].upstreamSha256 -ne $deletion.upstreamSha256 -or
            $original[0].removedPath -ne $deletion.removedPath -or $deletion.upstreamBytes -le 0) {
            throw "Invalid binary deletion record: $($deletion.upstreamPath)"
        }
    }
    foreach ($patch in $entry.patches) {
        $path = Join-Path $NativeRoot "provenance/$($entry.name)/$($patch.path)"
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $patch.sha256) { throw "Patch identity mismatch: $path" }
        Invoke-Checked 'git' @('-C', $NativeRoot, 'apply', '--reverse', '--check', '--ignore-space-change', $path)
    }
    Write-Host "Verified mapped $($entry.name) at $($entry.upstreamCommit)"
}

foreach ($file in $manifest.ownedFiles.PSObject.Properties) {
    $path = Join-Path $NativeRoot $file.Name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.Value) { throw "SDK source changed: $($file.Name)" }
    [void]$expectedPaths.Add($file.Name)
}
foreach ($scope in $manifest.sourceScopes) {
    foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $NativeRoot $scope) -File -Recurse)) {
        $relative = [IO.Path]::GetRelativePath($NativeRoot, $file.FullName).Replace('\', '/')
        if (-not $expectedPaths.Contains($relative)) { throw "Unregistered module source: $relative" }
    }
}
