# Builds the Windows installer from a folder with the release zip's files (default build\out,
# with the icon build.ps1 made). The maintainer accepted the WiX 7 EULA (OSMF); this script records that on each machine.
param([string]$Source = (Join-Path $PSScriptRoot 'build\out'), [Parameter(Mandatory)][string]$Version, [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
$WixVersion = '7.0.1'
$Tools = Join-Path $PSScriptRoot "build\wix-$WixVersion"
$wix = Join-Path $Tools 'wix.exe'
function Invoke-Native { & $args[0] @($args | Select-Object -Skip 1); if ($LASTEXITCODE) { throw "$($args[0]) failed with exit code $LASTEXITCODE" } }

$dotnet = (Get-Command dotnet -ErrorAction SilentlyContinue).Source
if (-not $dotnet) { $dotnet = "$env:ProgramFiles\dotnet\dotnet.exe" }
if (-not (Test-Path $wix)) { Invoke-Native $dotnet tool install wix --version $WixVersion --tool-path $Tools }
Invoke-Native $wix eula accept wix7
Invoke-Native $wix extension add -g "WixToolset.Util.wixext/$WixVersion"
$Icon = Join-Path $PSScriptRoot 'build\obj\ProjectKey.ico'
if (-not (Test-Path $Icon)) { throw 'run .\build.ps1 first' }
Invoke-Native $wix build (Join-Path $PSScriptRoot 'installer\ProjectKey.wxs') -arch x64 -ext "WixToolset.Util.wixext/$WixVersion" `
  -d "Version=$Version" -d "Package=$((Resolve-Path $Source).Path)" -d "Icon=$Icon" -o $Output
Write-Host "built $Output"
