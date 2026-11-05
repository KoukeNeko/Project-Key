# Types into a textarea in Microsoft Edge, a Chromium app that calls TSF's
# OnKeyDown/OnKeyUp without the test phase, and checks a lone Shift switches
# 中/英. Edge cannot be told to use one keyboard alone, so this makes Project
# Key the session's keyboard for the test and switches back to US English after.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -Namespace PKB -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr hwnd);
'@
$edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
$profile = Join-Path $env:TEMP "ProjectKey-edge-$PID"
$page = Join-Path $env:TEMP "ProjectKey-edge-$PID.html"
Set-Content $page '<!doctype html><meta charset="utf-8"><title>Project Key browser test</title><textarea id="t" style="width:600px;height:200px;font-size:20px" autofocus></textarea>' -Encoding utf8
$pwsh = (Get-Process -Id $PID).Path
$browser = Start-Process $edge -PassThru -ArgumentList "--user-data-dir=`"$profile`"", '--no-first-run', '--new-window', "file:///$($page.Replace('\', '/'))"
try {
  $A = [Windows.Automation.AutomationElement]
  $window = $null
  for ($i = 0; $i -lt 100 -and -not $window; $i++) {
    Start-Sleep -Milliseconds 200
    $window = $A::RootElement.FindFirst('Children', [Windows.Automation.PropertyCondition]::new($A::NameProperty, 'Project Key browser test - Profile 1 - Microsoft Edge'))
    if (-not $window) { $window = $A::RootElement.FindAll('Children', [Windows.Automation.Condition]::TrueCondition) | Where-Object { $_.Current.Name -like 'Project Key browser test*' } | Select-Object -First 1 }
  }
  if (-not $window) { throw 'Edge did not open the test page' }
  $hwnd = [IntPtr]$window.Current.NativeWindowHandle
  [PKB.Native]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [PKB.Native]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
  [void][PKB.Native]::SetForegroundWindow($hwnd)
  Start-Sleep -Milliseconds 500
  [PKB.Native]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero); [PKB.Native]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
  $area = $null
  # The page's textarea, not the address bar: the first edit inside the document.
  for ($i = 0; $i -lt 50 -and -not $area; $i++) {
    Start-Sleep -Milliseconds 200
    $document = $window.FindFirst('Descendants', [Windows.Automation.PropertyCondition]::new($A::ControlTypeProperty, [Windows.Automation.ControlType]::Document))
    if ($document) { $area = $document.FindFirst('Descendants', [Windows.Automation.PropertyCondition]::new($A::ControlTypeProperty, [Windows.Automation.ControlType]::Edit)) }
  }
  if (-not $area) { throw 'no textarea' }
  $area.SetFocus()
  & $pwsh -NoProfile -STA -Command @'
Add-Type @"
using System; using System.Runtime.InteropServices;
namespace PKS { [ComImport, Guid("71c6e74c-0f28-11d8-a82a-00065b84435c"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface M { [PreserveSig] int ActivateProfile(uint t, ushort l, ref Guid c, ref Guid p, IntPtr h, uint f); }
public static class S { public static int Run() { var m = (M)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("33C53A50-F456-4884-B049-85FD643ECFED")));
Guid c = new Guid("A11BBC41-85CF-449F-96B0-F1C5DBDB2213"), p = new Guid("570A6239-8C24-43D1-A2E9-C7C45C7512DA");
return m.ActivateProfile(1, 0x0404, ref c, ref p, IntPtr.Zero, 0x20000000 | 0x4); } } }
"@
exit [PKS.S]::Run()
'@
  Start-Sleep -Milliseconds 800

  function Press([byte]$vk) { [PKB.Native]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); [PKB.Native]::keybd_event($vk, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60 }
  function Tap-Shift { [PKB.Native]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero); [PKB.Native]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200 }
  function Value { Start-Sleep -Milliseconds 300; $area.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value }

  Press 0x4A; Press 0x49; Press 0x33; Press 0x0D   # ji3 Enter
  "after ji3 Enter: [$(Value)]"
  Tap-Shift
  Press 0x4F; Press 0x4B                          # ok
  Tap-Shift
  Press 0x52; Press 0x55; Press 0x50; Press 0x20; Press 0x0D   # rup Space Enter
  $text = Value
  "after Shift ok Shift rup: [$text]"
  if ($text -ne '我ok今') { throw "expected 我ok今, got [$text]" }
  'BrowserTest passed'
} finally {
  & $pwsh -NoProfile -STA -File (Join-Path $PSScriptRoot 'RestoreKeyboard.ps1') | Out-Null
  Stop-Process -Id $browser.Id -ErrorAction SilentlyContinue
  Get-Process msedge -ErrorAction SilentlyContinue | Where-Object { $_.Path -and (Get-CimInstance Win32_Process -Filter "ProcessId=$($_.Id)").CommandLine -like "*$profile*" } | Stop-Process -Force
}
