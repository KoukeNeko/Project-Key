# Builds the Windows installer from a folder with the release zip's files (default build\out,
# with the icon build.ps1 made). WiX 5 is pinned: from version 7 it needs its EULA accepted.
param([string]$Source = (Join-Path $PSScriptRoot 'build\out'), [Parameter(Mandatory)][string]$Version, [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
$WixVersion = '5.0.2'
$Tools = Join-Path $PSScriptRoot 'build\wix'
$wix = Join-Path $Tools 'wix.exe'
function Invoke-Native { & $args[0] @($args | Select-Object -Skip 1); if ($LASTEXITCODE) { throw "$($args[0]) failed with exit code $LASTEXITCODE" } }

$dotnet = (Get-Command dotnet -ErrorAction SilentlyContinue).Source
if (-not $dotnet) { $dotnet = "$env:ProgramFiles\dotnet\dotnet.exe" }
if (-not (Test-Path $wix)) { Invoke-Native $dotnet tool install wix --version $WixVersion --tool-path $Tools }
Invoke-Native $wix extension add -g "WixToolset.Util.wixext/$WixVersion"
$Icon = Join-Path $PSScriptRoot 'build\obj\ProjectKey.ico'
if (-not (Test-Path $Icon)) { throw 'run .\build.ps1 first' }
Invoke-Native $wix build (Join-Path $PSScriptRoot 'installer\ProjectKey.wxs') -arch x64 -ext "WixToolset.Util.wixext/$WixVersion" `
  -d "Version=$Version" -d "Package=$((Resolve-Path $Source).Path)" -d "Icon=$Icon" -o $Output
Write-Host "built $Output"
