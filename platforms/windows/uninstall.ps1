# Removes the input method installed by install.ps1: the keyboard from the
# language list, the registration, the server and the files. Apps that still
# have the DLL loaded keep it until they quit, so it is moved to %TEMP% instead.
param([switch]$Elevated)
$ErrorActionPreference = 'Stop'
$Dest = Join-Path $env:ProgramFiles 'Project Key'
$Tip = '0404:{A11BBC41-85CF-449F-96B0-F1C5DBDB2213}{570A6239-8C24-43D1-A2E9-C7C45C7512DA}'

if ($Elevated) {
  Get-Process ProjectKeyServer -ErrorAction SilentlyContinue | Stop-Process -Force
  if (Test-Path "$Dest\ProjectKey.dll") {
    Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList '/u', '/s', "`"$Dest\ProjectKey.dll`"" -Wait
  }
  Start-Sleep -Milliseconds 300
  # Loaded DLLs cannot be deleted, only moved.
  Get-ChildItem $Dest -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
    try { Remove-Item $_.FullName -Force } catch {
      $temp = Join-Path $env:TEMP "ProjectKey-$([guid]::NewGuid()).old"
      Move-Item $_.FullName $temp
    }
  }
  Remove-Item $Dest -Recurse -Force -ErrorAction SilentlyContinue
  exit 0
}

$languages = Get-WinUserLanguageList
foreach ($language in $languages) { [void]$language.InputMethodTips.Remove($Tip) }
Set-WinUserLanguageList $languages -Force
Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' 'ProjectKeyServer' -ErrorAction SilentlyContinue
Remove-Item (Join-Path ([Environment]::GetFolderPath('Programs')) 'Project Key.lnk') -ErrorAction SilentlyContinue
$pwsh = (Get-Process -Id $PID).Path
$admin = Start-Process $pwsh -Verb RunAs -Wait -PassThru -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Elevated'
if ($admin.ExitCode) { throw "removing $Dest failed with exit code $($admin.ExitCode)" }
Write-Host "removed $Dest (settings stay in HKCU\Software\ProjectKey)"
