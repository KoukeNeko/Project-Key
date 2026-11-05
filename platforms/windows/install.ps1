# Installs .build\out into Program Files (where apps in a sandbox can read it),
# registers the input method, and starts the engine server now and at sign-in.
# Adding it as a keyboard is left to Settings > Time & language > Language & region.
param([switch]$Elevated)
$ErrorActionPreference = 'Stop'
$Source = Join-Path $PSScriptRoot '.build\out'
$Dest = Join-Path $env:ProgramFiles 'Project Key'
if (-not (Test-Path "$Source\ProjectKey.dll")) { throw 'run .\build.ps1 first' }

if ($Elevated) {
  # Apps keep the old DLL loaded and the server keeps its files open: stop the
  # server and move loaded files aside; they are deleted at the next install.
  Get-Process ProjectKeyServer -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 300
  New-Item -ItemType Directory -Force $Dest | Out-Null
  Get-ChildItem $Dest -Filter '*.old' -Recurse | ForEach-Object { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }
  if (Test-Path "$Dest\ProjectKey.dll") { Move-Item "$Dest\ProjectKey.dll" "$Dest\ProjectKey.$([guid]::NewGuid()).old" }
  # Filtered first: Copy-Item -Recurse -Exclude flattens subfolders into odd names.
  Get-ChildItem $Source | Where-Object Extension -notin '.lib', '.exp' | Copy-Item -Destination $Dest -Recurse -Force
  & "$env:SystemRoot\System32\regsvr32.exe" /s "$Dest\ProjectKey.dll"
  exit $LASTEXITCODE
}

$pwsh = (Get-Process -Id $PID).Path
$admin = Start-Process $pwsh -Verb RunAs -Wait -PassThru -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Elevated'
if ($admin.ExitCode) { throw "installing into $Dest failed with exit code $($admin.ExitCode)" }

# The server runs as the user, not elevated.
$server = Join-Path $Dest 'ProjectKeyServer.exe'
Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' 'ProjectKeyServer' "`"$server`""
Start-Process $server
Write-Host "installed $Dest"
