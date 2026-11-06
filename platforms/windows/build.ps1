# Builds the Windows input method into build\out and runs the tests. Needs
# Visual Studio with the C++ desktop workload (MSVC and the Windows SDK) and
# uv (for the Python that generates McBopomofo's dictionary). The McBopomofo
# engine and dictionary (MIT) are fetched at a pinned commit into build\.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$Build = Join-Path $PSScriptRoot 'build'
$McbCommit = (Get-Content "$PSScriptRoot\..\..\MCBOPOMOFO_COMMIT").Trim()
$Mcb = Join-Path $Build 'mcbopomofo'
$Obj = Join-Path $Build 'obj'
$Out = Join-Path $Build 'out'
$Core = Join-Path $PSScriptRoot '..\..\core\engine'  # the engine shared with macOS
New-Item -ItemType Directory -Force $Build, $Obj, $Out | Out-Null

# MSVC environment from vcvars64.bat.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
  $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) { throw 'Visual Studio with the C++ desktop workload is required' }
  # A stray quote in PATH breaks vcvars64.bat's parenthesized blocks.
  $env:PATH = $env:PATH.Replace('"', '')
  # A batch file avoids cmd's quoting of a path with spaces and parentheses.
  $bat = Join-Path $Build 'vcvars.cmd'
  Set-Content $bat "@call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul`r`n@set" -Encoding ascii
  cmd /c $bat | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
  }
}

function Invoke-Native {
  & $args[0] @($args | Select-Object -Skip 1) 2>&1 | Out-Host
  if ($LASTEXITCODE) { throw "$($args[0]) failed with exit code $LASTEXITCODE" }
}

# McBopomofo at the pinned commit, and its dictionary.
if (-not (Test-Path "$Mcb\.git")) {
  Invoke-Native git init -q $Mcb
  Invoke-Native git -C $Mcb remote add origin https://github.com/openvanilla/McBopomofo
  Invoke-Native git -C $Mcb fetch -q --depth 1 origin $McbCommit
  Invoke-Native git -C $Mcb checkout -q FETCH_HEAD
}
$Data = Join-Path $Mcb 'Source\Data\data.txt'
if (-not (Test-Path $Data)) {
  $python = (& uv python find 3.12 2>$null) ; if (-not $python) { Invoke-Native uv python install 3.12; $python = & uv python find 3.12 }
  $env:PYTHONUTF8 = '1'
  Push-Location (Join-Path $Mcb 'Source\Data')
  try {
    # The Makefile's data.txt recipe, without make.
    Invoke-Native $python -m curation.builders.frequency_builder
    Invoke-Native $python -m curation.compilers.main_compiler --heterophony1 heterophony1.list --heterophony2 heterophony2.list `
      --heterophony3 heterophony3.list --phrase_freq PhraseFreq.txt --bpmf_mappings BPMFMappings.txt --bpmf_base BPMFBase.txt `
      --punctuations BPMFPunctuations.txt --symbols Symbols.txt --macros Macros.txt --output data-raw.txt
    Invoke-Native $python -m curation.compilers.postprocess --input data-raw.txt --directive Postprocess.txt --output data.txt
  } finally { Pop-Location }
  # Python on Windows writes CRLF; the engine reads LF lines.
  $bytes = [IO.File]::ReadAllBytes($Data)
  $text = [Text.Encoding]::UTF8.GetString($bytes).Replace("`r`n", "`n")
  [IO.File]::WriteAllBytes($Data, [Text.UTF8Encoding]::new($false).GetBytes($text))
}

$Engine = Join-Path $Mcb 'Source\Engine'
# Upstream takes string_view iterators for pointers, as libc++'s are; MSVC's are classes.
foreach ($file in 'AssociatedPhrasesV2.cpp', 'ParselessLM.cpp') {
  $path = Join-Path $Engine $file
  $source = [IO.File]::ReadAllText($path)
  $patched = [regex]::Replace($source, 'const auto\* (\w+) = ', 'auto $1 = ')
  if ($patched -ne $source) { [IO.File]::WriteAllText($path, $patched) }
}
$CxxFlags = @('/nologo', '/Brepro', '/std:c++20', '/utf-8', '/EHsc', '/O2', '/MT', '/Zi', '/permissive-',
  '/D_CRT_SECURE_NO_WARNINGS', '/DUNICODE', '/D_UNICODE', '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN',
  "/I$PSScriptRoot\compat", "/I$Engine", "/I$Core", "/I$PSScriptRoot\Sources")

# Same sources, same bytes: Smart App Control judges each new binary, so unchanged ones keep their verdict.
$LinkFlags = @('/nologo', '/DEBUG', '/INCREMENTAL:NO', '/Brepro')

# Compiles sources into $Obj\<name>\ and returns the object files; upstream code passes /W0.
function Compile([string]$name, [string[]]$sources, [string[]]$extra = @('/W3')) {
  $dir = Join-Path $Obj $name
  New-Item -ItemType Directory -Force $dir | Out-Null
  Invoke-Native cl.exe @CxxFlags @extra /c @sources "/Fo$dir\" "/Fd$dir\vc.pdb" | Out-Host
  # Callers wrap the result in @(): a single object would otherwise splat as characters.
  return $sources | ForEach-Object { Join-Path $dir ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj') }
}

$engineSources = @('AssociatedPhrasesV2', 'ByteBlockBackedDictionary', 'McBopomofoLM', 'MemoryMappedFile', 'ParselessLM',
  'ParselessPhraseDB', 'PhraseReplacementMap', 'UTF8Helper', 'UserOverrideModel', 'UserPhrasesLM') |
  ForEach-Object { "$Engine\$_.cpp" }
$engineSources += "$Engine\gramambular2\reading_grid.cpp", "$Engine\Mandarin\Mandarin.cpp"
$engineObjects = @(Compile 'mcbopomofo' $engineSources @('/W0'))
$engineObjects += Compile 'engine' @("$Core\Composer.cpp", "$Core\Bigram.cpp", "$Core\CharModel.cpp", "$Core\Controller.cpp")
Invoke-Native lib.exe /nologo "/OUT:$Obj\engine.lib" @engineObjects

# Tests: the engine tests shared with macOS, and the pipe protocol.
$tests = @(
  @{ Name = 'ComposerTest'; Source = "$Core\Tests\ComposerTest.cpp" },
  @{ Name = 'ControllerTest'; Source = "$Core\Tests\ControllerTest.cpp" },
  @{ Name = 'ProtocolTest'; Source = "$PSScriptRoot\Tests\ProtocolTest.cpp" })
foreach ($test in $tests) {
  $objects = @(Compile $test.Name.ToLower() @($test.Source))
  Invoke-Native link.exe @LinkFlags "/OUT:$Obj\$($test.Name).exe" @objects "$Obj\engine.lib"
}
$commonObjects = @(Compile 'common' @("$PSScriptRoot\Sources\Common\Layout.cpp", "$PSScriptRoot\Sources\Common\Win.cpp"))
$SystemLibs = @('ole32.lib', 'oleaut32.lib', 'uuid.lib', 'advapi32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'dwrite.lib', 'd2d1.lib',
  'dwmapi.lib')

# The engine server.
$serverObjects = @(Compile 'server' @("$PSScriptRoot\Sources\Server\Server.cpp"))
Invoke-Native link.exe @LinkFlags /SUBSYSTEM:WINDOWS "/OUT:$Out\ProjectKeyServer.exe" @serverObjects @commonObjects "$Obj\engine.lib" @SystemLibs

# The text service DLL, with the icon the input switcher shows: ㄅ on the accent color.
$Icon = Join-Path $Obj 'ProjectKey.ico'
if (-not (Test-Path $Icon)) {
  Add-Type -AssemblyName System.Drawing
  $frames = foreach ($size in 16, 20, 24, 32, 48, 64) {
    $bitmap = [Drawing.Bitmap]::new($size, $size)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $r = [Math]::Max(2, $size / 5)
    $path = [Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc(0, 0, 2 * $r, 2 * $r, 180, 90); $path.AddArc($size - 2 * $r - 1, 0, 2 * $r, 2 * $r, 270, 90)
    $path.AddArc($size - 2 * $r - 1, $size - 2 * $r - 1, 2 * $r, 2 * $r, 0, 90); $path.AddArc(0, $size - 2 * $r - 1, 2 * $r, 2 * $r, 90, 90)
    $g.FillPath([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(0x14, 0x80, 0x6f)), $path)
    $font = [Drawing.Font]::new('Microsoft JhengHei UI', $size * 0.62, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $format = [Drawing.StringFormat]::new(); $format.Alignment = 'Center'; $format.LineAlignment = 'Center'
    $g.DrawString([string][char]0x3105, $font, [Drawing.Brushes]::White, [Drawing.RectangleF]::new(0, $size * 0.04, $size, $size), $format)
    $g.Dispose()
    $stream = [IO.MemoryStream]::new()
    $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
    @{ Size = $size; Png = $stream.ToArray() }
  }
  # An .ico of PNG frames: header, directory, images.
  $ico = [IO.MemoryStream]::new()
  $w = [IO.BinaryWriter]::new($ico)
  $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$frames.Count)
  $offset = 6 + 16 * $frames.Count
  foreach ($f in $frames) {
    $w.Write([byte]($f.Size % 256)); $w.Write([byte]($f.Size % 256)); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$f.Png.Length); $w.Write([uint32]$offset)
    $offset += $f.Png.Length
  }
  foreach ($f in $frames) { $w.Write($f.Png) }
  [IO.File]::WriteAllBytes($Icon, $ico.ToArray())
}
Invoke-Native rc.exe /nologo /c65001 /i $Obj "/fo$Obj\ProjectKey.res" "$PSScriptRoot\Sources\TextService\ProjectKey.rc"
$dllSources = 'Dll', 'TextService', 'EngineClient', 'CandidateWindow', 'DisplayAttributes', 'LangBarButton' |
  ForEach-Object { "$PSScriptRoot\Sources\TextService\$_.cpp" }
$dllObjects = @(Compile 'textservice' $dllSources)
Invoke-Native link.exe @LinkFlags /DLL "/DEF:$PSScriptRoot\Sources\TextService\ProjectKey.def" "/OUT:$Out\ProjectKey.dll" `
  @dllObjects @commonObjects "$Obj\ProjectKey.res" @SystemLibs

# The settings window (WinUI 3, .NET), into Settings\ beside the text service.
$dotnet = (Get-Command dotnet -ErrorAction SilentlyContinue).Source
if (-not $dotnet) { $dotnet = "$env:ProgramFiles\dotnet\dotnet.exe" }
if (-not (Test-Path $dotnet)) { throw 'the .NET SDK is required for the settings window' }
# build, not publish: publish leaves out the compiled XAML (.xbf) and resources.pri an unpackaged app loads.
Invoke-Native $dotnet build "$PSScriptRoot\Settings\ProjectKeySettings.csproj" -c Release -nologo -v q -o "$Out\Settings"

# The dictionary, McBopomofo's license, and from tools/bigram-trainer if it was
# trained, the dictionary with the words McBopomofo lacks (replacing McBopomofo's)
# and the context ranking model; the server falls back to unigrams without it.
$Trained = "$PSScriptRoot\..\..\tools\bigram-trainer\.cache"
if (Test-Path "$Trained\data.txt") { Copy-Item "$Trained\data.txt" $Out } else { Copy-Item $Data $Out }
Copy-Item "$Mcb\LICENSE.txt" "$Out\McBopomofo-LICENSE.txt"
Copy-Item "$PSScriptRoot\..\..\LICENSE" "$Out\LICENSE.txt"
if (Test-Path "$Trained\bigram.bin") { Copy-Item "$Trained\bigram.bin" $Out } else { Write-Host "no $Trained\bigram.bin`: building without context ranking" }
if (Test-Path "$Trained\chars.bin") { Copy-Item "$Trained\chars.bin" $Out }

# Smart App Control blocks unsigned programs it has not seen; say so instead of failing the build.
foreach ($test in $tests) {
  try {
    Invoke-Native "$Obj\$($test.Name).exe" $Data
  } catch {
    if ("$_" -notmatch 'Application Control policy') { throw }
    Write-Warning "$($test.Name) was not run: Windows Application Control (Smart App Control) blocks unsigned programs on this computer."
    break
  }
}
