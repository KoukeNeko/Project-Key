# Installs .build\out into Program Files (where apps in a sandbox can read it),
# registers the input method, and starts the engine server now and at sign-in.
# Adding it as a keyboard is left to Settings > Time & language > Language & region.
param([switch]$Elevated)
$ErrorActionPreference = 'Stop'
$Source = Join-Path $PSScriptRoot '.build\out'
$Dest = Join-Path $env:ProgramFiles 'Project Key'
if (-not (Test-Path "$Source\ProjectKey.dll")) { throw 'run .\build.ps1 first' }

if ($Elevated) {
  $log = Join-Path $env:TEMP 'ProjectKey-install.log'
  try {
    # Apps keep the old DLL loaded and the server and settings keep their files
    # open: stop those and move loaded DLLs aside; they are deleted once unused.
    Get-Process ProjectKeyServer, ProjectKeySettings -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300
    New-Item -ItemType Directory -Force $Dest | Out-Null
    Get-ChildItem $Dest -Filter '*.old' | ForEach-Object { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }
    if (Test-Path "$Dest\ProjectKey.dll") { Move-Item "$Dest\ProjectKey.dll" "$Dest\ProjectKey.$([guid]::NewGuid()).old" }
    # robocopy, not Copy-Item, which flattens subfolders into odd names when they exist.
    & robocopy $Source $Dest /E /XF *.lib *.exp /NJH /NJS /NFL /NDL /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
    # Files an earlier Copy-Item flattened out of Settings\.
    Remove-Item "$Dest\SettingsApp.xbf", "$Dest\SettingsMainWindow.xbf" -ErrorAction SilentlyContinue
    # A GUI program: & would not wait for it, nor set its exit code.
    $register = Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList '/s', "`"$Dest\ProjectKey.dll`"" -Wait -PassThru
    if ($register.ExitCode) { throw "regsvr32 failed with exit code $($register.ExitCode)" }
  } catch {
    Set-Content $log "$_"
    exit 1
  }
  exit 0
}

$pwsh = (Get-Process -Id $PID).Path
$admin = Start-Process $pwsh -Verb RunAs -Wait -PassThru -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Elevated'
if ($admin.ExitCode) { throw "installing into $Dest failed: $(Get-Content (Join-Path $env:TEMP 'ProjectKey-install.log') -ErrorAction SilentlyContinue)" }

# The server runs as the user, not elevated.
$server = Join-Path $Dest 'ProjectKeyServer.exe'
Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' 'ProjectKeyServer' "`"$server`""
Start-Process $server
Write-Host "installed $Dest"
