# 显式准备固定 OSGB 依赖；检查和普通构建不下载或安装依赖。
param([int]$Jobs = 4, [switch]$Offline)
. "$PSScriptRoot/common.ps1"
$entry = $SourceLock.sources | Where-Object name -EQ 'osg2vsg'
$archive = Join-Path $DepsRoot "downloads/$($entry.name)-$($entry.commit).zip"
New-Item -ItemType Directory -Force "$DepsRoot/downloads", "$DepsRoot/src" | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    if ($Offline) { throw "Missing fixed archive: $archive" }
    Invoke-WebRequest -Uri $entry.url -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() -ne $entry.archiveSha256) { throw 'osg2vsg archive identity mismatch.' }
if (-not (Test-Path -LiteralPath (Get-Source 'osg2vsg'))) { Expand-Archive -LiteralPath $archive -DestinationPath "$DepsRoot/src" }
& "$WorkspaceRoot/packages/osg/scripts/build-sdk.ps1" -Jobs $Jobs
if ($LASTEXITCODE -ne 0) { throw 'OSG dependency preparation failed.' }
Invoke-Checked 'python' @('-B', "$PSScriptRoot/verify-osgb.py", '--dependencies', $DepsRoot, '--osg', "$WorkspaceRoot/packages/osg/sdk/osg-3.6.5-windows-x64-msvc", '--gdal', $GdalSdk)
