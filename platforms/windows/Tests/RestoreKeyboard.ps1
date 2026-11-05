# Makes US English the active keyboard for the whole session again, after a
# test activated Project Key. Run with -STA.
Add-Type @'
using System;
using System.Runtime.InteropServices;
namespace PKRestore {
  [ComImport, Guid("71c6e74c-0f28-11d8-a82a-00065b84435c"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface ITfInputProcessorProfileMgr {
    [PreserveSig] int ActivateProfile(uint type, ushort langid, ref Guid clsid, ref Guid profile, IntPtr hkl, uint flags);
  }
  public static class Profiles {
    public static int ActivateUsEnglish() {
      var mgr = (ITfInputProcessorProfileMgr)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("33C53A50-F456-4884-B049-85FD643ECFED")));
      Guid none = Guid.Empty, none2 = Guid.Empty;
      const uint KeyboardLayout = 2, ForSession = 0x20000000;
      return mgr.ActivateProfile(KeyboardLayout, 0x0409, ref none, ref none2, new IntPtr(0x04090409), ForSession);
    }
  }
}
'@
$hr = [PKRestore.Profiles]::ActivateUsEnglish()
if ($hr) { throw ('ActivateProfile failed: 0x{0:x8}' -f $hr) }
'US English is the active keyboard'
