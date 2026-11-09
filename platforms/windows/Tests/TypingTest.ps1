# Types into a real TSF text box with the installed input method and checks
# what gets committed; saves screenshots of the candidate window.
# Needs install.ps1 first, Project Key as the active keyboard for new windows
# (Set-WinDefaultInputMethodOverride), and an unlocked desktop.
# -Kind winforms types into a classic Win32 edit control instead of WPF.
# -Flip puts the text near the bottom of the screen and only checks the
# candidate window opens above it.
param([string]$Shots = (Join-Path $PSScriptRoot '..\build\shots'), [ValidateSet('wpf', 'winforms')][string]$Kind = 'wpf',
  [switch]$Flip)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace PK -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr hwnd);
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool GetWindowRect(System.IntPtr hwnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT point);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint flags, int x, int y, uint data, System.UIntPtr extra);
[DllImport("user32.dll")] public static extern uint GetDpiForWindow(System.IntPtr hwnd);
[DllImport("user32.dll")] static extern System.IntPtr FindWindowExW(System.IntPtr parent, System.IntPtr after, [MarshalAs(UnmanagedType.LPWStr)] string cls, string title);
[DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(System.IntPtr hwnd, out uint pid);
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
// The candidate window of one process: every app that has typed keeps its own, cloaked.
public static System.IntPtr CandidateWindow(uint process) {
  System.IntPtr at = System.IntPtr.Zero;
  while ((at = FindWindowExW(System.IntPtr.Zero, at, "IME_Candidate_Window", null)) != System.IntPtr.Zero) {
    uint pid; GetWindowThreadProcessId(at, out pid);
    if (pid == process) return at;
  }
  return at;
}
'@

[void][PK.Native]::SetProcessDPIAware()  # window and screen coordinates in the same pixels
$capsLockBefore = [Console]::CapsLock
$cursorBefore = [PK.Native+POINT]::new(); [void][PK.Native]::GetCursorPos([ref]$cursorBefore)
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
  if ((Text) -notmatch 'ok[今金]') { throw "expected English then ㄐㄧㄣ's word, got [$(Text)]" }

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

  # A real click on the row's third candidate picks it, and the document keeps the focus.
  Type-Keys 'rup '
  Press 0x0D
  Start-Sleep -Milliseconds 300
  $guess = (Text)
  Type-Keys 'rup '
  Start-Sleep -Milliseconds 300
  $candidate = [PK.Native]::CandidateWindow([uint32]$host_.Id)
  if ($candidate -eq 0) { throw 'no candidate window to click' }
  $r = [PK.Native+RECT]::new(); [void][PK.Native]::GetWindowRect($candidate, [ref]$r)
  $scale = [PK.Native]::GetDpiForWindow($candidate) / 96
  [void][PK.Native]::SetCursorPos($r.Left + [int](190 * $scale), $r.Bottom - [int](24 * $scale))  # the third column, in the row at the window's bottom
  Start-Sleep -Milliseconds 100
  [PK.Native]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); [PK.Native]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 300
  if ([PK.Native]::GetForegroundWindow() -ne $hwnd) { throw 'the click took the focus from the document' }
  Press 0x0D
  Start-Sleep -Milliseconds 300
  "after clicking: [$(Text)]"
  if ((Text).Length -ne $guess.Length + 1 -or (Text).Substring($guess.Length) -eq $guess.Substring($guess.Length - 1)) {
    throw "expected the click to pick another word than the guess, got [$(Text)]"
  }
  'TypingTest passed'
} finally {
  # The keys it pressed are global: leave no modifier down and Caps Lock as it was, pass or fail.
  foreach ($vk in 0x10, 0x11, 0x12) { [PK.Native]::keybd_event($vk, 0, 2, [UIntPtr]::Zero) }
  [void][PK.Native]::SetCursorPos($cursorBefore.X, $cursorBefore.Y)
  if ([Console]::CapsLock -ne $capsLockBefore) {
    [PK.Native]::keybd_event(0x14, 0, 0, [UIntPtr]::Zero); [PK.Native]::keybd_event(0x14, 0, 2, [UIntPtr]::Zero)
  }
  # A fresh server also drops the 中/英 mode the test left it in.
  $server = Get-Process ProjectKeyServer -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Path
  if ($server) {
    Get-Process ProjectKeyServer | Stop-Process -Force
    Start-Process $server
  }
  if (-not $host_.HasExited) { Stop-Process -Id $host_.Id -Force }
  Remove-Item $out -ErrorAction SilentlyContinue
}
