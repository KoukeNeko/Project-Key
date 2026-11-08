using System;
using System.Linq;
using System.Runtime.InteropServices;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.Win32;

namespace ProjectKey.Settings;

// The window after installing (install.ps1 opens it with --welcome): adds Project Key as a
// keyboard, waits for the user to switch to it, and offers a box to try it in.
public sealed partial class WelcomeWindow : Window {
  // Globals.h's kClsid and kProfile under Chinese (Taiwan), as the user's language list names it.
  private const string Tip = "0404:{A11BBC41-85CF-449F-96B0-F1C5DBDB2213}{570A6239-8C24-43D1-A2E9-C7C45C7512DA}";
  private static readonly Guid Clsid = new("A11BBC41-85CF-449F-96B0-F1C5DBDB2213");
  private static readonly Guid KeyboardCategory = new("34745C63-B2F0-4784-8B67-5E12C8701A31");  // GUID_TFCAT_TIP_KEYBOARD
  private readonly DispatcherTimer poll = new() { Interval = TimeSpan.FromMilliseconds(500) };
  private readonly IProfileManager profiles = (IProfileManager)new ProfileManager();
  private int step = -1;

  public WelcomeWindow() {
    InitializeComponent();
    ExtendsContentIntoTitleBar = true;
    SetTitleBar(TitleBar);
    if (AppWindow.Presenter is OverlappedPresenter presenter) {
      presenter.IsResizable = false;
      presenter.IsMaximizable = false;
    }
    WindowFit.Follow(this, Root, 560);  // as wide as the settings window, so the cards keep their content beside the header
    // Neither the language list nor the keyboard in use tells anyone when it changes.
    poll.Tick += (_, _) => Refresh();
    poll.Start();
    Closed += (_, _) => poll.Stop();
    Refresh();
  }

  // Whether the user's language list (Settings > Time & language) has Project Key.
  public static bool IsAdded() {
    using RegistryKey? languages = Registry.CurrentUser.OpenSubKey(@"Control Panel\International\User Profile");
    if (languages == null) return false;
    foreach (string language in languages.GetSubKeyNames()) {
      using RegistryKey? keyboards = languages.OpenSubKey(language);
      if (keyboards?.GetValueNames().Any(name => string.Equals(name, Tip, StringComparison.OrdinalIgnoreCase)) == true) return true;
    }
    return false;
  }

  // Whether this window types with Project Key now.
  private bool IsInUse() {
    Guid keyboard = KeyboardCategory;
    return profiles.GetActiveProfile(ref keyboard, out Profile profile) == 0 && profile.Clsid == Clsid;
  }

  private void Refresh() {
    bool added = IsAdded(), tried = Try.Text.Length > 0;
    int now = !added ? 0 : !tried && !IsInUse() ? 1 : !tried ? 2 : 3;
    if (now == step) return;
    step = now;
    Add.Visibility = step == 0 ? Visibility.Visible : Visibility.Collapsed;
    Added.Visibility = step == 0 ? Visibility.Collapsed : Visibility.Visible;
    Shortcut.Visibility = step <= 1 ? Visibility.Visible : Visibility.Collapsed;
    InUse.Visibility = step <= 1 ? Visibility.Collapsed : Visibility.Visible;
    SwitchCard.IsEnabled = step >= 1;
    AddCard.HeaderIcon = Mark(0);
    SwitchCard.HeaderIcon = Mark(1);
    FontIcon mark = Mark(2);
    TryMark.Glyph = mark.Glyph;
    TryMark.Foreground = mark.Foreground;
    Hints.Opacity = Try.Opacity = step >= 2 ? 1 : 0.5;
    Try.IsEnabled = step >= 1;
    Done.Style = step == 3 ? (Style)Application.Current.Resources["AccentButtonStyle"] : null;
    if (step == 2) Try.Focus(FocusState.Programmatic);  // the switch landed: type here next
  }

  // Done, the step to do now, or not yet.
  private FontIcon Mark(int row) {
    string brush = step > row ? "SystemFillColorSuccessBrush" : step == row ? "AccentFillColorDefaultBrush" : "TextFillColorTertiaryBrush";
    return new FontIcon {
      Glyph = step > row ? "" : step == row ? "" : "",  // CompletedSolid, RadioBtnOn, RadioBtnOff
      Foreground = (Brush)Application.Current.Resources[brush],
    };
  }

  private void OnAdd(object sender, RoutedEventArgs e) {
    if (!InstallLayoutOrTip(Tip, 0)) AddCard.Description = "無法加入，請到「設定 > 時間與語言 > 語言與地區」新增鍵盤";
    Refresh();
  }

  private void OnTyped(object sender, TextChangedEventArgs e) => Refresh();

  private void OnSettings(object sender, RoutedEventArgs e) => new MainWindow().Activate();

  private void OnDone(object sender, RoutedEventArgs e) => Close();

  [DllImport("input.dll", CharSet = CharSet.Unicode)]
  private static extern bool InstallLayoutOrTip(string profile, uint flags);

  [StructLayout(LayoutKind.Sequential)]
  private struct Profile {
    public uint ProfileType;
    public ushort LangId;
    public Guid Clsid;
    public Guid GuidProfile;
    public Guid CategoryId;
    public IntPtr HklSubstitute;
    public uint Caps;
    public IntPtr Hkl;
    public uint Flags;
  }

  [ComImport, Guid("33C53A50-F456-4884-B049-85FD643ECFED")]  // CLSID_TF_InputProcessorProfiles
  private class ProfileManager {
  }

  // ITfInputProcessorProfileMgr, in vtable order up to the one method used.
  [ComImport, Guid("71C6E74C-0F28-11D8-A82A-00065B84435C"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  private interface IProfileManager {
    void ActivateProfile();
    void DeactivateProfile();
    void GetProfile();
    void EnumProfiles();
    void ReleaseInputProcessor();
    void RegisterProfile();
    void UnregisterProfile();
    [PreserveSig] int GetActiveProfile(ref Guid category, out Profile profile);
  }
}
