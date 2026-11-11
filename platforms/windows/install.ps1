# Installs build\out (or -Source, an unpacked release) into Program Files
# (where apps in a sandbox can read it), registers the input method, and starts
# the engine server now and at sign-in, then opens the welcome window, which adds
# it as a keyboard (the settings app with --welcome, which closes when it is added).
# -Update: run by the settings window's 更新 button, which it stops; opens it again instead.
# -Zip and -Digest: a downloaded release and its SHA-256 (sha256:<hex>), unpacked only once
# elevated, in a folder under Program Files that only administrators can change.
param([string]$Source = (Join-Path $PSScriptRoot 'build\out'), [string]$Zip, [string]$Digest, [switch]$Elevated, [switch]$Update)
$ErrorActionPreference = 'Stop'
$Dest = Join-Path $env:ProgramFiles 'Project Key'
if (-not $Zip -and -not (Test-Path "$Source\ProjectKey.dll")) { throw 'run .\build.ps1 first' }

if ($Elevated) {
  $log = Join-Path $env:TEMP 'ProjectKey-install.log'
  $staging = Join-Path $env:ProgramFiles 'Project Key Update'
  try {
    if ($Zip) {
      # The zip came from the user's %TEMP%, which anything they run can change: it is checked
      # again only once copied where they cannot.
      if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
      New-Item -ItemType Directory $staging | Out-Null
      Copy-Item -LiteralPath $Zip "$staging\package.zip"
      if ("sha256:$((Get-FileHash "$staging\package.zip" -Algorithm SHA256).Hash.ToLowerInvariant())" -ne $Digest) {
        throw 'the downloaded release does not match its SHA-256'
      }
      Expand-Archive "$staging\package.zip" "$staging\package"
      $Source = "$staging\package"
      if (-not (Test-Path "$Source\ProjectKey.dll")) { throw 'the downloaded release has no ProjectKey.dll' }
    }
    # Apps keep the old DLL loaded and the server and settings keep their files
    # open: stop those and move loaded DLLs aside; they are deleted once unused.
    # The server moves aside first: an open app starts it again the moment it stops,
    # which would lock the files about to be replaced.
    $serverExe = "$Dest\ProjectKeyServer.exe"
    $serverAside = "$Dest\ProjectKeyServer.$([guid]::NewGuid()).old"
    if (Test-Path $serverExe) { Move-Item $serverExe $serverAside }
    Get-Process ProjectKeyServer, ProjectKeySettings -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300
    New-Item -ItemType Directory -Force $Dest | Out-Null
    Get-ChildItem $Dest -Filter '*.old' | ForEach-Object { Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue }
    if (Test-Path "$Dest\ProjectKey.dll") { Move-Item "$Dest\ProjectKey.dll" "$Dest\ProjectKey.$([guid]::NewGuid()).old" }
    # robocopy, not Copy-Item, which flattens subfolders into odd names when they exist.
    # The server goes last, so an app that starts it finds every model in place. A locked
    # file fails the install: robocopy's default is a million retries, 30 seconds apart.
    & robocopy $Source $Dest /E /R:3 /W:1 /XF *.lib *.exp ProjectKeyServer.exe /NJH /NJS /NFL /NDL /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
    Copy-Item "$Source\ProjectKeyServer.exe" $serverExe -Force
    # The updater runs this copy, which only administrators can change; a release brings its own.
    if (-not (Test-Path "$Source\install.ps1") -and $PSCommandPath -ne "$Dest\install.ps1") { Copy-Item $PSCommandPath "$Dest\install.ps1" -Force }
    # Files an earlier Copy-Item flattened out of Settings\.
    Remove-Item "$Dest\SettingsApp.xbf", "$Dest\SettingsMainWindow.xbf" -ErrorAction SilentlyContinue
    # A GUI program: & would not wait for it, nor set its exit code.
    $register = Start-Process "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList '/s', "`"$Dest\ProjectKey.dll`"" -Wait -PassThru
    if ($register.ExitCode) { throw "regsvr32 failed with exit code $($register.ExitCode)" }
  } catch {
    Set-Content $log "$_"
    # A failed install leaves the server it found, so the input method keeps working.
    if ($serverAside -and (Test-Path $serverAside) -and -not (Test-Path $serverExe)) { Move-Item $serverAside $serverExe }
    exit 1
  } finally {
    if ($Zip -and (Test-Path $staging)) { Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue }
  }
  exit 0
}

$pwsh = (Get-Process -Id $PID).Path
$arguments = '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-Elevated'
$arguments += if ($Zip) { '-Zip', "`"$Zip`"", '-Digest', $Digest } else { '-Source', "`"$Source`"" }
$admin = Start-Process $pwsh -Verb RunAs -Wait -PassThru -ArgumentList $arguments
if ($admin.ExitCode) { throw "installing into $Dest failed: $(Get-Content (Join-Path $env:TEMP 'ProjectKey-install.log') -ErrorAction SilentlyContinue)" }

# The server runs as the user, not elevated.
$server = Join-Path $Dest 'ProjectKeyServer.exe'
Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' 'ProjectKeyServer' "`"$server`""
Start-Process $server
$settings = Join-Path $Dest 'Settings\ProjectKeySettings.exe'
# In the Start menu, so the settings open even when the keyboard list or its tray icon does not.
$shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path ([Environment]::GetFolderPath('Programs')) 'Project Key.lnk'))
$shortcut.TargetPath = $settings
$shortcut.Description = 'Settings for the Project Key Zhuyin input method'
$shortcut.Save()
if ($Update) {
  Start-Process $settings
  exit 0  # nobody sees the apps still on the old version; they switch when they restart
}
Start-Process $settings '--welcome'
Write-Host "installed $Dest"

# A loaded DLL cannot be replaced: apps keep the version they loaded until they
# restart. Builds are deterministic, so the PE header stamp tells versions apart.
Add-Type -Namespace ProjectKeyInstall -Name Native -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenProcess(uint access, bool inherit, int pid);
[DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(System.IntPtr process, System.IntPtr address, byte[] buffer, int size, out int read);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr handle);
'@
function Get-Stamp([byte[]]$header) { [BitConverter]::ToUInt32($header, [BitConverter]::ToInt32($header, 0x3C) + 8) }
$current = Get-Stamp ([IO.File]::ReadAllBytes("$Dest\ProjectKey.dll"))
$stale = Get-Process | ForEach-Object {
  $process = $_
  $module = try { $process.Modules | Where-Object ModuleName -eq 'ProjectKey.dll' } catch { $null }
  if (-not $module) { return }
  $handle = [ProjectKeyInstall.Native]::OpenProcess(0x0410, $false, $process.Id)
  $header = [byte[]]::new(4096)
  $read = 0
  if ([ProjectKeyInstall.Native]::ReadProcessMemory($handle, $module.BaseAddress, $header, 4096, [ref]$read) -and (Get-Stamp $header) -ne $current) {
    $process.ProcessName
  }
  [void][ProjectKeyInstall.Native]::CloseHandle($handle)
} | Sort-Object -Unique
if ($stale) { Write-Warning "restart these apps (or sign out) to use the new version: $($stale -join ', ')" }
