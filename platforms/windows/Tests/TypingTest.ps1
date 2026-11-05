# Types into a real TSF text box with the installed input method and checks
# what gets committed; saves screenshots of the candidate window.
# Needs install.ps1 first, Project Key as the active keyboard for new windows
# (Set-WinDefaultInputMethodOverride), and an unlocked desktop.
# -Kind winforms types into a classic Win32 edit control instead of WPF.
# -Flip puts the text near the bottom of the screen and only checks the
# candidate window opens above it.
param([string]$Shots = (Join-Path $PSScriptRoot '..\.build\shots'), [ValidateSet('wpf', 'winforms')][string]$Kind = 'wpf',
  [switch]$Flip)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace PK -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr hwnd);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr hwnd, out RECT rect);
public struct RECT { public int Left, Top, Right, Bottom; }
'@

[void][PK.Native]::SetProcessDPIAware()  # window and screen coordinates in the same pixels
# A fresh server forgets earlier runs' choices, so the engine guesses the same words each time.
$server = Get-Process ProjectKeyServer -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Path
if ($server) {
  Get-Process ProjectKeyServer | Stop-Process -Force
  Start-Process $server
  Start-Sleep -Milliseconds 500
}
New-Item -ItemType Directory -Force $Shots | Out-Null
$out = Join-Path $env:TEMP "ProjectKey-typing-$PID.txt"
Set-Content $out ''
$pwsh = (Get-Process -Id $PID).Path
$top = 200
if ($Flip) {
  # In WPF's device-independent pixels: the box's first line about 140 px above the work area's end.
  Add-Type -AssemblyName System.Windows.Forms
  $dpi = (Get-ItemProperty 'HKCU:\Control Panel\Desktop\WindowMetrics' -ErrorAction SilentlyContinue).AppliedDPI
  if (-not $dpi) { $dpi = 96 }
  $top = [int]([Windows.Forms.Screen]::PrimaryScreen.WorkingArea.Bottom * 96 / $dpi) - 140
}
$host_ = Start-Process $pwsh -PassThru -ArgumentList '-NoProfile', '-STA', '-File', "`"$PSScriptRoot\TypingHost.ps1`"", '-Out', "`"$out`"",
  '-Kind', $Kind, '-Top', $top
try {
  for ($i = 0; $i -lt 100 -and $host_.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 100; $host_.Refresh() }
  $hwnd = $host_.MainWindowHandle
  if ($hwnd -eq 0) { throw 'the test window did not open' }
  # A background process may not take the foreground; an Alt press lifts that lock.
  [PK.Native]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [PK.Native]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
  [void][PK.Native]::SetForegroundWindow($hwnd)
  Start-Sleep -Milliseconds 800
  if ([PK.Native]::GetForegroundWindow() -ne $hwnd) { throw 'could not bring the test window to the foreground' }
  # The Alt tap put the window in menu mode, which would swallow the next key.
  [PK.Native]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero); [PK.Native]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 200

  function Press([byte]$vk, [switch]$Shift, [switch]$Control) {
    if ($Control) { [PK.Native]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero) }
    if ($Shift) { [PK.Native]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero) }
    [PK.Native]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    [PK.Native]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    if ($Shift) { [PK.Native]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero) }
    if ($Control) { [PK.Native]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 40
  }
  function Tap-Shift { [PK.Native]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero); [PK.Native]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150 }
  $keys = @{ ' ' = 0x20; ',' = 0xBC; '.' = 0xBE; '/' = 0xBF; ';' = 0xBA; '-' = 0xBD; '''' = 0xDE; '\' = 0xDC; '[' = 0xDB; ']' = 0xDD }
  function Type-Keys([string]$text) {
    foreach ($c in $text.ToCharArray()) {
      $vk = if ($keys.ContainsKey([string]$c)) { $keys[[string]$c] } else { [byte][char]([string]$c).ToUpper() }
      Press $vk
    }
    Start-Sleep -Milliseconds 300
  }
  function Text { [IO.File]::ReadAllText($out) }
  function Shot([string]$name) {
    Start-Sleep -Milliseconds 400
    $r = [PK.Native+RECT]::new(); [void][PK.Native]::GetWindowRect($hwnd, [ref]$r)
    $bitmap = [Drawing.Bitmap]::new($r.Right - $r.Left + 200, $r.Bottom - $r.Top + 120)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bitmap.Size)
    $bitmap.Save((Join-Path $Shots "$name.png"))
    $g.Dispose(); $bitmap.Dispose()
  }

  $loaded = (Get-Process -Id $host_.Id).Modules | Where-Object ModuleName -eq 'ProjectKey.dll'
  "text service loaded: $([bool]$loaded)"

  if ($Flip) {
    Type-Keys 'ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3'
    Press 0x20
    Shot 'flip'
    Press 0x1B; Press 0x1B; Press 0x1B
    'TypingTest -Flip done: see flip.png'
    return
  }

  # 我好: two syllables, then Enter commits.
  Type-Keys 'ji3cl3'
  Shot 'composing'
  Press 0x0D
  Start-Sleep -Milliseconds 300
  "after ji3cl3 Enter: [$(Text)]"
  if ((Text) -ne '我好') { throw "expected 我好, got [$(Text)]" }

  # The sentence with a doubtful word: Space opens candidates, 2 picks 程式.
  Type-Keys 'ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3'
  Shot 'sentence'
  Press 0x20
  Shot 'candidates'
  Press 0x28  # Down: expanded grid
  Shot 'expanded'
  Press 0x26  # Up folds it back
  Press 0x32  # 2
  Shot 'picked'
  Press 0x0D
  Start-Sleep -Milliseconds 300
  "after the sentence: [$(Text)]"
  if ((Text) -notmatch '的程式重構') { throw "expected 程式 to be picked, got [$(Text)]" }

  # A lone Shift switches to English and back; the badge shows the mode.
  Tap-Shift
  Shot 'badge-english'
  Type-Keys 'ok'
  Tap-Shift
  Type-Keys 'rup '
  Press 0x0D
  Start-Sleep -Milliseconds 300
  "after Shift: [$(Text)]"
  if ((Text) -notmatch 'ok今') { throw "expected English then 今, got [$(Text)]" }

  # Zhuyin typed in English mode: the preview shows the conversion and the Tab
  # keycap; Tab turns it into a composition and switches to 中.
  Tap-Shift
  Type-Keys ' ji3'
  Shot 'hint'
  if ($Kind -eq 'winforms') {
    # A Win32 edit control shows TSF no text, so there is nothing to convert: switch with Shift.
    Tap-Shift
    Type-Keys 'rup '
  } else {
    Press 0x09
  }
  Press 0x0D
  Start-Sleep -Milliseconds 300
  "after ji3 in English: [$(Text)]"
  $expected = if ($Kind -eq 'winforms') { ' ji3今' } else { ' 我' }
  if (-not (Text).EndsWith($expected)) { throw "expected the text to end with [$expected], got [$(Text)]" }

  # Caps Lock: with the 切換中英 setting it switches to English and letters stay
  # lowercase; off (Windows' default) it locks capitals, which pass to the app.
  $capsSwitches = (Get-ItemProperty 'HKCU:\Software\ProjectKey' -ErrorAction SilentlyContinue).CapsLockToggle -eq 1
  Press 0x14
  Type-Keys 'ab'
  Press 0x14
  Start-Sleep -Milliseconds 300
  "after Caps Lock: [$(Text)]"
  $expected = if ($capsSwitches) { 'ab' } else { 'AB' }
  if (-not (Text).EndsWith($expected)) { throw "expected $expected, got [$(Text)]" }

  # Punctuation keys without Shift, as on macOS.
  Type-Keys "'\[]"
  Press 0x0D
  Start-Sleep -Milliseconds 300
  "after punctuation: [$(Text)]"
  if (-not (Text).EndsWith('、、「」')) { throw "expected 、、「」, got [$(Text)]" }
  'TypingTest passed'
} finally {
  if (-not $host_.HasExited) { Stop-Process -Id $host_.Id -Force }
  Remove-Item $out -ErrorAction SilentlyContinue
}
