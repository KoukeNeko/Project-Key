# Talks to a running ProjectKeyServer over its pipe, as the text service does,
# and checks a typed sentence comes back as a composition and a commit.
# Usage: Tests\ServerTest.ps1   (on a fresh server: stop the running one, start build\out\ProjectKeyServer.exe;
# the server takes clearing the choices only from the settings app, so what an earlier
# run or typing taught it would change the guesses)
$ErrorActionPreference = 'Stop'
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$pipe = [IO.Pipes.NamedPipeClientStream]::new('.', "ProjectKey-$sid", [IO.Pipes.PipeDirection]::InOut)
$pipe.Connect(3000)
$pipe.ReadMode = [IO.Pipes.PipeTransmissionMode]::Message

function Send([byte]$type, [byte]$key = 0, [string]$chars = '', [byte]$flags = 0) {
  $m = [IO.MemoryStream]::new(); $w = [IO.BinaryWriter]::new($m)
  $w.Write([uint32]3); $w.Write($type); $w.Write($key)  # Protocol.h kVersion
  $b = [Text.Encoding]::UTF8.GetBytes($chars); $w.Write([uint32]$b.Length); $w.Write($b); $w.Write($flags)
  $w.Write([byte]0); $w.Write([uint32]0)
  $bytes = $m.ToArray(); $pipe.Write($bytes, 0, $bytes.Length); $pipe.Flush()
  $reply = [IO.MemoryStream]::new(); $buffer = [byte[]]::new(65536)
  do { $n = $pipe.Read($buffer, 0, $buffer.Length); $reply.Write($buffer, 0, $n) } while (-not $pipe.IsMessageComplete)
  $r = [IO.BinaryReader]::new([IO.MemoryStream]::new($reply.ToArray()))
  $str = { [Text.Encoding]::UTF8.GetString($r.ReadBytes($r.ReadUInt32())) }
  $result = @{ Version = $r.ReadUInt32(); Handled = $r.ReadByte(); English = $r.ReadByte(); Appearance = $r.ReadInt32(); Ops = @() }
  $count = $r.ReadUInt32()
  for ($i = 0; $i -lt $count; $i++) {
    $op = $r.ReadByte()
    switch ($op) {
      1 { $result.Ops += [pscustomobject]@{ Op = 'insert'; Text = & $str } }
      2 { $result.Ops += [pscustomobject]@{ Op = 'composition'; Text = & $str; Caret = $r.ReadUInt32(); ActiveStart = $r.ReadUInt32(); ActiveLength = $r.ReadUInt32(); Replace = $r.ReadUInt32() } }
      { $_ -in 3, 7 } {  # the panel, or the Tab hint's own window with the same content
        $segments = @(); $n = $r.ReadUInt32(); for ($k = 0; $k -lt $n; $k++) { $segments += & $str }
        $n = $r.ReadUInt32(); $r.ReadBytes($n) | Out-Null
        $active = $r.ReadInt32(); $pending = & $str; $caret = $r.ReadUInt32(); $proof = $r.ReadByte(); $hint = & $str; $replaced = & $str
        $rows = @(); $n = $r.ReadUInt32(); for ($k = 0; $k -lt $n; $k++) { $row = @(); $c = $r.ReadUInt32(); for ($j = 0; $j -lt $c; $j++) { $row += & $str }; $rows += , $row }
        $result.Ops += [pscustomobject]@{ Op = $(if ($op -eq 7) { 'hint' } else { 'panel' }); Segments = $segments; Rows = $rows; SelectedRow = $r.ReadInt32(); Selection = $r.ReadInt32(); Page = & $str; Expanded = $r.ReadByte() }
      }
      4 { $result.Ops += [pscustomobject]@{ Op = 'hide' } }
      5 { $result.Ops += [pscustomobject]@{ Op = 'indicator'; Text = & $str } }
      6 { $result.Ops += [pscustomobject]@{ Op = 'beep' } }
      8 { $result.Ops += [pscustomobject]@{ Op = 'hide hint' } }
    }
  }
  $result
}

function Type-Keys([string]$keys) {
  foreach ($c in $keys.ToCharArray()) { $last = if ($c -eq ' ') { Send 1 1 ' ' } else { Send 1 0 ([string]$c) }; if (-not $last.Handled) { throw "not handled: $c" } }
  $last
}

$reply = Type-Keys 'ji3ap7183ru.42k7t/6g4tj/6e.4t/6ai6yj3'
$composition = ($reply.Ops | Where-Object Op -eq 'composition')[-1].Text
"composition: $composition"
# The dictionary alone guesses 城市; with the context models installed (bigram.bin), 程式.
if ($composition -match '的城市') {
  $reply = Send 1 1 ' '  # Space: candidates on the doubtful 城市
  $panel = ($reply.Ops | Where-Object Op -eq 'panel')[-1]
  "candidates: $($panel.Rows[0] -join ' ')  page $($panel.Page)"
  $reply = Send 1 0 '2'
  $composition = ($reply.Ops | Where-Object Op -eq 'composition')[-1].Text
}
if ($composition -notmatch '的程式') { throw "expected 程式: $composition" }
$reply = Send 1 2  # Return commits
$insert = ($reply.Ops | Where-Object Op -eq 'insert')[-1].Text
"committed: $insert"
if ($insert -notmatch '的程式重') { throw 'expected the sentence to be committed' }
$pipe.Dispose()
'ServerTest passed'
