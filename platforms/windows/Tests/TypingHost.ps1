# A window with one text box for Tests\TypingTest.ps1: WPF (a TSF text store)
# or WinForms (a classic Win32 edit control, reached through TSF's IMM32
# compatibility layer). It writes the box's text to $Out whenever it changes.
param([string]$Out, [ValidateSet('wpf', 'winforms')][string]$Kind = 'wpf', [int]$Top = 200)
Add-Type @'
using System;
using System.Runtime.InteropServices;
namespace PK {
  [ComImport, Guid("71c6e74c-0f28-11d8-a82a-00065b84435c"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface ITfInputProcessorProfileMgr {
    [PreserveSig] int ActivateProfile(uint type, ushort langid, ref Guid clsid, ref Guid profile, IntPtr hkl, uint flags);
  }
  public static class Profiles {
    // Makes Project Key the active keyboard in this process only.
    public static int Activate() {
      var mgr = (ITfInputProcessorProfileMgr)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("33C53A50-F456-4884-B049-85FD643ECFED")));
      Guid clsid = new Guid("A11BBC41-85CF-449F-96B0-F1C5DBDB2213"), profile = new Guid("570A6239-8C24-43D1-A2E9-C7C45C7512DA");
      const uint InputProcessor = 1, ForProcess = 0x10000000, DontCareLanguage = 0x4;
      return mgr.ActivateProfile(InputProcessor, 0x0404, ref clsid, ref profile, IntPtr.Zero, ForProcess | DontCareLanguage);
    }
  }
}
'@
function Activate-ProjectKey {
  $hr = [PK.Profiles]::Activate()
  if ($hr) { [IO.File]::WriteAllText("$Out.error", "ActivateProfile: 0x{0:x8}" -f $hr) }
}
$title = 'Project Key typing test'

if ($Kind -eq 'winforms') {
  Add-Type -AssemblyName System.Windows.Forms
  $form = [Windows.Forms.Form]::new()
  $form.Text = $title
  $form.StartPosition = 'Manual'
  $form.SetBounds(200, $Top, 900, 420)
  $box = [Windows.Forms.TextBox]::new()
  $box.Multiline = $true
  $box.Dock = 'Fill'
  $box.Font = [Drawing.Font]::new('Microsoft JhengHei UI', 14)
  $form.Controls.Add($box)
  $box.Add_TextChanged({ [IO.File]::WriteAllText($Out, $box.Text) })
  $form.Add_Shown({ $form.Activate(); [void]$box.Focus(); Activate-ProjectKey })
  [void]$form.ShowDialog()
  return
}

Add-Type -AssemblyName PresentationFramework
$window = [Windows.Window]::new()
$window.Title = $title
$window.Width = 900
$window.Height = 420
$window.Left = 200
$window.Top = $Top
$box = [Windows.Controls.TextBox]::new()
$box.FontSize = 18
$box.AcceptsReturn = $true
$box.Margin = '24'
$window.Content = $box
$box.Add_TextChanged({ [IO.File]::WriteAllText($Out, $box.Text) })
$window.Add_ContentRendered({
    $window.Activate()
    [void]$box.Focus()
    Activate-ProjectKey
  })
[void]$window.ShowDialog()
