using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Net.Http;
using System.Security.Principal;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Win32;
using Windows.Foundation;
using Windows.Graphics;

namespace ProjectKey.Settings;

// Edits the settings the engine server reads: DWORDs under
// HKCU\Software\ProjectKey, named and defaulted as in core/engine/Controller.cpp.
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

    Bind(NextDoubtful, "NextDoubtful");
    Bind(AdjacentKeys, "AdjacentKeys");
    Bind(RememberChoices, "RememberChoices");
    Bind(CapsLockToggle, "CapsLockToggle", false);
    Bind(ShiftToggle, "ShiftToggle");
    Bind(EnglishToZhuyin, "EnglishToZhuyin");
    Bind(ZhuyinToEnglish, "ZhuyinToEnglish");
    Bind(Appearance, "Appearance", 1);
    Bind(ProofPreview, "ProofPreview", 1);
    Bind(ChoosingProof, "ChoosingProof", 1);
    Bind(CandidatePosition, "CandidatePosition", 0);
    // The text service's version (ProjectKey.rc); build.ps1 puts this app in Settings\ beside it.
    Version.Text = FileVersionInfo.GetVersionInfo(Path.Combine(AppContext.BaseDirectory, "..", "ProjectKey.dll")).ProductVersion ?? "";
    CheckForUpdate();

    Root.SizeChanged += (_, _) => FitToContent();
    Root.Loaded += (_, _) => Root.XamlRoot.Changed += (_, _) => FitToContent();
  }

  private static int Read(string name, int fallback) =>
      Registry.CurrentUser.OpenSubKey(Key)?.GetValue(name) is int value ? value : fallback;

  private static void Write(string name, int value) {
    using RegistryKey key = Registry.CurrentUser.CreateSubKey(Key);
    key.SetValue(name, value, RegistryValueKind.DWord);
  }

  private static void Bind(ToggleSwitch toggle, string name, bool fallback = true) {
    toggle.IsOn = Read(name, fallback ? 1 : 0) != 0;
    toggle.Toggled += (_, _) => Write(name, toggle.IsOn ? 1 : 0);
  }

  private static void Bind(ComboBox box, string name, int fallback) {
    box.SelectedIndex = Math.Clamp(Read(name, fallback), 0, box.Items.Count - 1);
    box.SelectionChanged += (_, _) => Write(name, box.SelectedIndex);
  }

  // As tall as the laid-out content, at the fixed width, centered. Resizing
  // inside a layout event is overridden by that layout, so it waits for it to end.
  private void FitToContent() {
    DispatcherQueue.TryEnqueue(() => {
      double scale = Root.XamlRoot.RasterizationScale;
      // The XAML area is the content plus what is left over below it. The
      // outer size is set: ResizeClient lands 30px taller with the title bar extended.
      int spare = (int)Math.Round((Root.XamlRoot.Size.Height - Root.ActualHeight) * scale);
      int widthChange = (int)Math.Ceiling(Width * scale) - AppWindow.ClientSize.Width;
      if (spare == 0 && widthChange == 0) return;
      AppWindow.Resize(new SizeInt32(AppWindow.Size.Width + widthChange, AppWindow.Size.Height - spare));
      DisplayArea area = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary);
      AppWindow.Move(new PointInt32(area.WorkArea.X + (area.WorkArea.Width - AppWindow.Size.Width) / 2,
                                    area.WorkArea.Y + (area.WorkArea.Height - AppWindow.Size.Height) / 2));
    });
  }

  // The newest GitHub release, which brew and scoop install from.
  private async void CheckForUpdate() {
    try {
      using var client = new HttpClient();
      client.DefaultRequestHeaders.UserAgent.ParseAdd("ProjectKey");  // GitHub rejects requests without one
      using JsonDocument release = JsonDocument.Parse(
          await client.GetStringAsync("https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest"));
      string latest = release.RootElement.GetProperty("tag_name").GetString()!.TrimStart('v');
      // Version is the TextBlock here, hence System.Version.
      Update.Content = System.Version.Parse(latest) > System.Version.Parse(Version.Text)
          ? new HyperlinkButton {
              Content = $"下載 {latest}",
              NavigateUri = new Uri(release.RootElement.GetProperty("html_url").GetString()!),
            }
          : new TextBlock { Text = "已是最新版本" };
    } catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or JsonException
                                    or KeyNotFoundException or ArgumentException or FormatException) {
      Update.Content = new TextBlock { Text = "無法檢查" };
    }
  }

  // Asks the engine server to forget the choices; it keeps them in memory only.
  private void OnClearChoices(object sender, RoutedEventArgs e) {
    try {
      string sid = WindowsIdentity.GetCurrent().User!.Value;
      using var pipe = new NamedPipeClientStream(".", "ProjectKey-" + sid, PipeDirection.InOut);
      pipe.Connect(1000);
      pipe.ReadMode = PipeTransmissionMode.Message;
      // Protocol.h: kVersion (2), Request::kClearChoices, an empty key event, no context.
      var message = new MemoryStream();
      var writer = new BinaryWriter(message);
      writer.Write(2u);
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
