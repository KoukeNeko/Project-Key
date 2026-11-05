using System;
using System.IO;
using System.IO.Pipes;
using System.Security.Principal;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Win32;
using Windows.Foundation;
using Windows.Graphics;

namespace ProjectKey.Settings;

// Edits the settings the engine server reads: DWORDs under
// HKCU\Software\ProjectKey, named and defaulted as in Engine/Controller.cpp.
// Changes are written at once, as on macOS; there is no Apply button.
public sealed partial class MainWindow : Window {
  private const string Key = @"Software\ProjectKey";
  private const double Width = 560;

  public MainWindow() {
    InitializeComponent();
    ExtendsContentIntoTitleBar = true;
    SetTitleBar(TitleBar);
    if (AppWindow.Presenter is OverlappedPresenter presenter) {
      presenter.IsResizable = false;
      presenter.IsMaximizable = false;
    }

    Bind(AdjacentKeys, "AdjacentKeys");
    Bind(RememberChoices, "RememberChoices");
    Bind(CapsLockToggle, "CapsLockToggle");
    Bind(ShiftToggle, "ShiftToggle");
    Bind(EnglishToZhuyin, "EnglishToZhuyin");
    Bind(ZhuyinToEnglish, "ZhuyinToEnglish");
    Bind(Appearance, "Appearance", 0);
    Bind(ProofPreview, "ProofPreview", 1);
    Bind(ChoosingProof, "ChoosingProof", 1);
    // The Shift conversions only happen when a lone Shift switches.
    ShiftToggle.Toggled += (_, _) => UpdateShiftDependents();
    UpdateShiftDependents();

    Root.SizeChanged += (_, _) => FitToContent();
  }

  private static int Read(string name, int fallback) =>
      Registry.CurrentUser.OpenSubKey(Key)?.GetValue(name) is int value ? value : fallback;

  private static void Write(string name, int value) {
    using RegistryKey key = Registry.CurrentUser.CreateSubKey(Key);
    key.SetValue(name, value, RegistryValueKind.DWord);
  }

  private static void Bind(ToggleSwitch toggle, string name) {
    toggle.IsOn = Read(name, 1) != 0;
    toggle.Toggled += (_, _) => Write(name, toggle.IsOn ? 1 : 0);
  }

  private static void Bind(ComboBox box, string name, int fallback) {
    box.SelectedIndex = Math.Clamp(Read(name, fallback), 0, box.Items.Count - 1);
    box.SelectionChanged += (_, _) => Write(name, box.SelectedIndex);
  }

  private void UpdateShiftDependents() {
    EnglishToZhuyinCard.IsEnabled = ShiftToggle.IsOn;
    ZhuyinToEnglishCard.IsEnabled = ShiftToggle.IsOn;
  }

  // As tall as the laid-out content, at the fixed width; centered the first time.
  private bool centered_;

  private void FitToContent() {
    double scale = Root.XamlRoot.RasterizationScale;
    var size = new SizeInt32((int)Math.Ceiling(Width * scale), (int)Math.Ceiling(Root.ActualHeight * scale));
    if (AppWindow.ClientSize.Width != size.Width || AppWindow.ClientSize.Height != size.Height) AppWindow.ResizeClient(size);
    if (centered_) return;
    centered_ = true;
    DisplayArea area = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary);
    AppWindow.Move(new PointInt32(area.WorkArea.X + (area.WorkArea.Width - AppWindow.Size.Width) / 2,
                                  area.WorkArea.Y + (area.WorkArea.Height - AppWindow.Size.Height) / 2));
  }

  // Asks the engine server to forget the choices; it keeps them in memory only.
  private void OnClearChoices(object sender, RoutedEventArgs e) {
    try {
      string sid = WindowsIdentity.GetCurrent().User!.Value;
      using var pipe = new NamedPipeClientStream(".", "ProjectKey-" + sid, PipeDirection.InOut);
      pipe.Connect(1000);
      pipe.ReadMode = PipeTransmissionMode.Message;
      // Protocol.h: version 1, Request::kClearChoices, an empty key event, no context.
      var message = new MemoryStream();
      var writer = new BinaryWriter(message);
      writer.Write(1u);
      writer.Write((byte)7);
      writer.Write((byte)0);
      writer.Write(0u);
      writer.Write((byte)0);
      writer.Write((byte)0);
      writer.Write(0u);
      byte[] bytes = message.ToArray();
      pipe.Write(bytes, 0, bytes.Length);
      // The reply carries nothing the window needs; read it to complete the request.
      var reply = new byte[4096];
      while (pipe.Read(reply, 0, reply.Length) > 0 && !pipe.IsMessageComplete) {
      }
    } catch (Exception ex) when (ex is IOException or TimeoutException or UnauthorizedAccessException) {
      // No server running: there are no choices in memory to clear.
    }
    ClearChoices.IsEnabled = false;
  }
}
