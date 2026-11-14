using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Net.Http;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text.Json;
using System.Threading;
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
    AppWindow.SetIcon(System.IO.Path.Combine(AppContext.BaseDirectory, "ProjectKey.ico"));
    ExtendsContentIntoTitleBar = true;
    SetTitleBar(TitleBar);
    if (AppWindow.Presenter is OverlappedPresenter presenter) {
      presenter.IsResizable = false;
      presenter.IsMaximizable = false;
    }

    Bind(NextDoubtful, "NextDoubtful");
    Bind(OmitTones, "OmitTones", false);
    Bind(Abbreviated, "Abbreviated", false);
    Bind(FuzzySounds, "FuzzySounds", false);
    Bind(AssociatedPhrases, "AssociatedPhrases", false);
    Bind(KeypadPicks, "KeypadPicks", false);
    Bind(RememberChoices, "RememberChoices");
    Bind(CapsLockToggle, "CapsLockToggle", false);
    Bind(ShiftToggle, "ShiftToggle");
    Bind(FullWidthToggle, "FullWidthToggle");
    Bind(EnglishToZhuyin, "EnglishToZhuyin");
    Bind(ZhuyinToEnglish, "ZhuyinToEnglish");
    // 自動, 淺色, 深色, Acrylic, Mica, Mica Alt: 淺色 and 自動 came after the others, which keep their values.
    Bind(Appearance, "Appearance", 5, [5, 4, 0, 1, 2, 3]);
    Bind(ProofPreview, "ProofPreview", 1);
    Bind(ChoosingProof, "ChoosingProof", 1);
    Bind(CandidatePosition, "CandidatePosition", 0);
    Bind(GridColumns, "GridColumns", 0);
    Bind(KeyboardLayout, "KeyboardLayout", 0);
    // The text service's version (ProjectKey.rc); build.ps1 puts this app in Settings\ beside it.
    Version.Text = FileVersionInfo.GetVersionInfo(Path.Combine(AppContext.BaseDirectory, "..", "ProjectKey.dll")).ProductVersion ?? "";
    CheckForReleases(ModelsReleases);
#if ADVANCED_MODEL
    Advanced.Visibility = Visibility.Visible;
    CheckForReleases(AdvancedReleases);
#endif
    CheckForUpdate();

    WindowFit.Follow(this, Root, Width);
  }

  private static int Read(string name, int fallback) =>
      Registry.CurrentUser.OpenSubKey(Key)?.GetValue(name) is int value ? value : fallback;

  private static void Write(string name, int value) {
    using RegistryKey key = Registry.CurrentUser.CreateSubKey(Key);
    key.SetValue(name, value, RegistryValueKind.DWord);
  }

  // The models build.ps1 installed beside the text service, and newer releases this window downloads
  // beside personal.txt; the engine uses the newest, as core/engine/Models.h chooses.
  private static readonly string InstalledModels = Path.Combine(AppContext.BaseDirectory, "..");
  private static readonly string DownloadedModels =
      Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Project Key", "models");

  // models-version.txt, from fetch-models.sh or train.sh; empty without it.
  private static string ModelsVersion(string dir) {
    string file = Path.Combine(dir, "models-version.txt");
    return File.Exists(file) ? File.ReadAllText(file).Trim() : "";
  }

  // ModelsDirectory in Models.h: a downloaded folder counts once it is named after its version.
  private static string ModelsInUse() {
    string newest = InstalledModels;
    foreach (string dir in Directory.Exists(DownloadedModels) ? Directory.GetDirectories(DownloadedModels) : []) {
      string version = ModelsVersion(dir);
      if (version != "" && version == Path.GetFileName(dir) && string.CompareOrdinal(version, ModelsVersion(newest)) > 0) newest = dir;
    }
    return newest;
  }

  private static TextBlock InstalledModelsText() {
    string dir = ModelsInUse();
    string version = ModelsVersion(dir);
    bool installed = File.Exists(Path.Combine(dir, "bigram.bin")) && File.Exists(Path.Combine(dir, "chars.bin"));
    return new TextBlock { Text = !installed ? "未安裝" : version != "" ? version : "已安裝" };
  }

  // The advanced model (core/engine/AdvancedModel.h), which nothing installs: the newest release this
  // window downloaded, chosen as for the models; empty when there is none.
  private static readonly string DownloadedAdvanced =
      Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Project Key", "advanced");

  private static string AdvancedVersion() {
    string newest = "";
    foreach (string dir in Directory.Exists(DownloadedAdvanced) ? Directory.GetDirectories(DownloadedAdvanced) : []) {
      string version = ModelsVersion(dir);
      if (version != "" && version == Path.GetFileName(dir) && string.CompareOrdinal(version, newest) > 0) newest = version;
    }
    return newest;
  }

  private record ModelsFile(string Name, long Size, string Digest, string Url);

  // A card offering downloads from GitHub releases named <prefix>YYYY-MM-DD with all the required files:
  // the models, or the advanced model. Around puts what the card shows beside the rest of it.
  private sealed record Releases(CommunityToolkit.WinUI.Controls.SettingsCard Card, string Prefix, string[] Required, string Folder, Func<string> InUse,
                                 Func<UIElement> Installed, Func<UIElement, UIElement> Around);

  private Releases ModelsReleases => new(Models, "models-", ["data.txt", "bigram.bin", "chars.bin"], DownloadedModels,
                                         () => ModelsVersion(ModelsInUse()), InstalledModelsText, shown => shown);

  // Its version or the newer one offered, and 移除 once one is downloaded.
  private Releases AdvancedReleases => new(Advanced, "advanced-", ["advanced.gguf"], DownloadedAdvanced, AdvancedVersion,
                                           () => new TextBlock { Text = AdvancedVersion() is "" ? "未安裝" : AdvancedVersion() }, WithRemove);

  private UIElement WithRemove(UIElement shown) {
    if (AdvancedVersion() == "") return shown;
    var remove = new Button { Content = "移除" };
    remove.Click += (_, _) => RemoveAdvanced();
    var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
    row.Children.Add(shown);
    row.Children.Add(remove);
    return row;
  }

  // Removes every advanced model downloaded and has the server drop it, then offers the newest again.
  private async void RemoveAdvanced() {
    var dialog = new ContentDialog {
      XamlRoot = Content.XamlRoot,
      Title = "移除進階模型？",
      Content = "打字停頓時，不再用進階模型重選整句。",
      PrimaryButtonText = "移除",
      CloseButtonText = "取消",
      DefaultButton = ContentDialogButton.Close,
    };
    if (await dialog.ShowAsync() != ContentDialogResult.Primary) return;
    try {
      // The server keeps the model's file open. Without its version file a folder is no longer one to
      // load (core/engine/Models.h), so the server lets go of it on Request::kReloadModels, and then it can go.
      foreach (string dir in Directory.Exists(DownloadedAdvanced) ? Directory.GetDirectories(DownloadedAdvanced) : []) {
        File.Delete(Path.Combine(dir, "models-version.txt"));
      }
      SendToServer(11);  // Protocol.h: Request::kReloadModels, which reloads the advanced model too
      for (int attempt = 0; ; attempt++) {
        try {
          if (Directory.Exists(DownloadedAdvanced)) Directory.Delete(DownloadedAdvanced, true);
          break;
        } catch (Exception ex) when ((ex is IOException or UnauthorizedAccessException) && attempt < 20) {
          await Task.Delay(250);  // the server unloads it on a thread of its own
        }
      }
    } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) {
      Advanced.Content = new TextBlock { Text = "無法移除" };
      return;
    }
    CheckForReleases(AdvancedReleases);
  }

  // The newest such release, offered when newer than the one in use. GitHub gives each file's SHA-256.
  private async void CheckForReleases(Releases releasesOf) {
    releasesOf.Card.Content = releasesOf.Around(releasesOf.Installed());
    try {
      using var client = new HttpClient();
      client.DefaultRequestHeaders.UserAgent.ParseAdd("ProjectKey");
      using JsonDocument releases = JsonDocument.Parse(
          await client.GetStringAsync("https://api.github.com/repos/KoukeNeko/Project-Key/releases?per_page=30"));
      string newestTag = "";
      List<ModelsFile> newestFiles = [];
      foreach (JsonElement release in releases.RootElement.EnumerateArray()) {
        string tag = release.GetProperty("tag_name").GetString()!;
        if (!tag.StartsWith(releasesOf.Prefix) || release.GetProperty("draft").GetBoolean() || string.CompareOrdinal(tag, newestTag) <= 0) continue;
        var files = new List<ModelsFile>();
        foreach (JsonElement asset in release.GetProperty("assets").EnumerateArray()) {
          if (asset.TryGetProperty("digest", out JsonElement digest) && digest.ValueKind == JsonValueKind.String) {
            files.Add(new ModelsFile(asset.GetProperty("name").GetString()!, asset.GetProperty("size").GetInt64(), digest.GetString()!,
                                     asset.GetProperty("browser_download_url").GetString()!));
          }
        }
        if (releasesOf.Required.All(name => files.Any(file => file.Name == name))) {
          newestTag = tag;
          newestFiles = files;
        }
      }
      string date = newestTag.Length > 0 ? newestTag[releasesOf.Prefix.Length..] : "";
      if (string.CompareOrdinal(date, releasesOf.InUse()) <= 0) return;
      var button = new Button { Content = $"下載 {date}（{newestFiles.Sum(file => file.Size) / 1_000_000} MB）" };
      button.Click += (_, _) => DownloadRelease(releasesOf, date, newestFiles);
      releasesOf.Card.Content = releasesOf.Around(button);
    } catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or JsonException
                                    or KeyNotFoundException or InvalidOperationException) {
      // The 更新 row already says when GitHub cannot be reached.
    }
  }

  private async void DownloadRelease(Releases releasesOf, string date, List<ModelsFile> files) {
    var status = new TextBlock { Text = "下載中 0%" };
    releasesOf.Card.Content = status;
    long total = files.Sum(file => file.Size);
    var progress = new Progress<long>(done => status.Text = $"下載中 {done * 100 / total}%");
    string partial = Path.Combine(releasesOf.Folder, date + ".partial");
    try {
      Directory.CreateDirectory(releasesOf.Folder);
      foreach (string left in Directory.GetDirectories(releasesOf.Folder, "*.partial")) Directory.Delete(left, true);  // cut short before
      // Off the UI thread: a continuation there for every read slows the download to a crawl.
      await Task.Run(() => Download(files, partial, progress));
      // Written last: a folder with it is complete.
      File.WriteAllText(Path.Combine(partial, "models-version.txt"), date + "\n");
      string folder = Path.Combine(releasesOf.Folder, date);
      if (Directory.Exists(folder)) Directory.Delete(folder, true);
      Directory.Move(partial, folder);
      SendToServer(11);  // Protocol.h: Request::kReloadModels
      releasesOf.Card.Content = releasesOf.Around(releasesOf.Installed());
    } catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or IOException
                                    or UnauthorizedAccessException or InvalidDataException) {
      try { Directory.Delete(partial, true); } catch (Exception cleanup) when (cleanup is IOException or UnauthorizedAccessException) { }
      releasesOf.Card.Content = new TextBlock { Text = "無法下載" };
    }
  }

  // Each file into partial, checked against GitHub's SHA-256, reporting the bytes so far at each whole percent.
  private static async Task Download(List<ModelsFile> files, string partial, IProgress<long> progress) {
    Directory.CreateDirectory(partial);
    using var client = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
    long total = files.Sum(file => file.Size), done = 0;
    var buffer = new byte[1 << 20];
    foreach (ModelsFile file in files) {
      using HttpResponseMessage response = await client.GetAsync(file.Url, HttpCompletionOption.ResponseHeadersRead);
      response.EnsureSuccessStatusCode();
      using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
      await using (Stream source = await response.Content.ReadAsStreamAsync())
      await using (FileStream target = File.Create(Path.Combine(partial, file.Name))) {
        int read;
        while ((read = await source.ReadAsync(buffer)) > 0) {
          await target.WriteAsync(buffer.AsMemory(0, read));
          hash.AppendData(buffer, 0, read);
          long before = done * 100 / total;
          done += read;
          if (done * 100 / total != before) progress.Report(done);
        }
      }
      if ("sha256:" + Convert.ToHexStringLower(hash.GetHashAndReset()) != file.Digest) throw new InvalidDataException(file.Name);
    }
  }

  // Puts each bound control back to its default once the settings are gone, without writing it.
  private readonly List<Action> showDefaults = [];
  private bool restoring;

  private void Bind(ToggleSwitch toggle, string name, bool fallback = true) {
    toggle.IsOn = Read(name, fallback ? 1 : 0) != 0;
    toggle.Toggled += (_, _) => {
      if (!restoring) Write(name, toggle.IsOn ? 1 : 0);
    };
    showDefaults.Add(() => toggle.IsOn = fallback);
  }

  // values: what each item stores, in the list's order; without them, its index. A value the
  // list does not have shows as its first item.
  private void Bind(ComboBox box, string name, int fallback, int[]? values = null) {
    int IndexOf(int value) => values is null ? Math.Clamp(value, 0, box.Items.Count - 1) : Math.Max(0, Array.IndexOf(values, value));
    box.SelectedIndex = IndexOf(Read(name, fallback));
    box.SelectionChanged += (_, _) => {
      if (!restoring) Write(name, values is null ? box.SelectedIndex : values[box.SelectedIndex]);
    };
    showDefaults.Add(() => box.SelectedIndex = IndexOf(fallback));
  }

  // 恢復原廠設定: the settings go, so the engine server reads its defaults at the next composition;
  // 一併刪除資料 also removes the user's files and the choices the server holds. The downloaded
  // models stay.
  private async void OnRestore(object sender, RoutedEventArgs e) {
    var dialog = new ContentDialog {
      XamlRoot = Content.XamlRoot,
      Title = "恢復原廠設定？",
      Content = "設定將改回預設值。「一併刪除資料」另刪除自訂詞、排除詞與選字紀錄，無法復原；下載的選字模型保留。",
      PrimaryButtonText = "恢復設定",
      SecondaryButtonText = "一併刪除資料",
      CloseButtonText = "取消",
      DefaultButton = ContentDialogButton.Close,
    };
    ContentDialogResult result = await dialog.ShowAsync();
    if (result == ContentDialogResult.None) return;
    Registry.CurrentUser.DeleteSubKeyTree(Key, throwOnMissingSubKey: false);
    restoring = true;
    foreach (Action showDefault in showDefaults) showDefault();
    restoring = false;
    if (result != ContentDialogResult.Secondary) return;
    try {
      foreach (string name in new[] { "phrases.txt", "exclude-phrases.txt", "personal.txt" }) {
        // File.Delete does nothing for a file that is not there, but throws for a folder that is not.
        if (Directory.Exists(UserDataFolder())) File.Delete(Path.Combine(UserDataFolder(), name));
      }
    } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) {
      Restore.Content = "無法刪除資料";
    }
    SendToServer(7);  // Protocol.h: Request::kClearChoices
    ClearChoices.IsEnabled = false;
  }

  // The newest GitHub release, which brew and scoop install from. A newer one is offered as an
  // update: its Windows zip, checked against the SHA-256 GitHub lists, installed by its own install.ps1.
  private async void CheckForUpdate() {
    try {
      using var client = new HttpClient();
      client.DefaultRequestHeaders.UserAgent.ParseAdd("ProjectKey");  // GitHub rejects requests without one
      using JsonDocument release = JsonDocument.Parse(
          await client.GetStringAsync("https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest"));
      string latest = release.RootElement.GetProperty("tag_name").GetString()!.TrimStart('v');
      // Version is the TextBlock here, hence System.Version.
      if (System.Version.Parse(latest) <= System.Version.Parse(Version.Text)) {
        Update.Content = new TextBlock { Text = "已是最新版本" };
        return;
      }
      string name = $"ProjectKey-{latest}-windows-x64.zip";
      ModelsFile? zip = null;
      foreach (JsonElement asset in release.RootElement.GetProperty("assets").EnumerateArray()) {
        if (asset.GetProperty("name").GetString() == name && asset.TryGetProperty("digest", out JsonElement digest) &&
            digest.ValueKind == JsonValueKind.String) {
          zip = new ModelsFile(name, asset.GetProperty("size").GetInt64(), digest.GetString()!, asset.GetProperty("browser_download_url").GetString()!);
        }
      }
      if (zip is null) {  // a release without the zip, or without GitHub's digest for it: the page to download from
        Update.Content = new HyperlinkButton {
          Content = $"下載 {latest}",
          NavigateUri = new Uri(release.RootElement.GetProperty("html_url").GetString()!),
        };
        return;
      }
      var button = new Button { Content = $"更新到 {latest}" };
      button.Click += (_, _) => InstallUpdate(zip);
      Update.Content = button;
    } catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or JsonException
                                    or KeyNotFoundException or ArgumentException or FormatException) {
      Update.Content = new TextBlock { Text = "無法檢查" };
    }
  }

  private static readonly string UpdateFolder = Path.Combine(Path.GetTempPath(), "ProjectKey-update");

  // Downloads the release, then runs the installed install.ps1 on its zip: elevated, that copies the
  // zip where only administrators can write, checks its SHA-256 again and installs from there, so
  // nothing in the user's writable %TEMP% runs elevated. It stops this window and the engine server,
  // installs, and starts both again.
  private async void InstallUpdate(ModelsFile zip) {
    var status = new TextBlock { Text = "下載中 0%" };
    Update.Content = status;
    var progress = new Progress<long>(done => status.Text = $"下載中 {done * 100 / zip.Size}%");
    try {
      if (Directory.Exists(UpdateFolder)) Directory.Delete(UpdateFolder, true);
      await Task.Run(() => Download([zip], UpdateFolder, progress));
      status.Text = "安裝中…";
      // Program Files, which build.ps1 and install.ps1 put this app under; only administrators change it.
      string installer = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "install.ps1"));
      var start = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "WindowsPowerShell", "v1.0", "powershell.exe")) {
        UseShellExecute = false, CreateNoWindow = true,
      };
      foreach (string argument in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", installer,
                                          "-Zip", Path.Combine(UpdateFolder, zip.Name), "-Digest", zip.Digest, "-Update" }) {
        start.ArgumentList.Add(argument);
      }
      using Process install = Process.Start(start)!;
      await install.WaitForExitAsync();
      // Still running: install.ps1 stopped before it replaced anything, as when the administrator prompt is declined.
      Update.Content = new TextBlock { Text = "無法更新" };
    } catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or IOException or UnauthorizedAccessException
                                    or InvalidDataException or System.ComponentModel.Win32Exception) {
      Update.Content = new TextBlock { Text = "無法更新" };
    }
  }

  // Asks the engine server to forget the choices; it keeps them in memory only.
  private void OnClearChoices(object sender, RoutedEventArgs e) {
    SendToServer(7);  // Protocol.h: Request::kClearChoices
    ClearChoices.IsEnabled = false;
  }

  // Opens the user's words (core/engine Lexicon::loadUserPhrases) in their text editor, made first
  // when missing, beside personal.txt; the engine reads them again on the next composition after a save.
  private void OnEditPhrases(object sender, RoutedEventArgs e) {
    OpenUserFile(EditPhrases, "phrases.txt", "# 每行一個詞：詞、空白、注音\n# 注音的音節以 - 連接，中間不留空白\n# 一聲不標；其他聲調寫在音節後，輕聲寫 ˙（ㄉㄜ˙）\n# 例：小鍵 ㄒㄧㄠˇ-ㄐㄧㄢˋ\n# 存檔後，下一次打字就會用到；# 開頭的行不會讀入\n");
  }

  // The same for the words that are never offered (exclude-phrases.txt).
  private void OnEditExcluded(object sender, RoutedEventArgs e) {
    OpenUserFile(EditExcluded, "exclude-phrases.txt", "# 每行一個不要出現的詞：詞、空白、注音，格式同自訂詞\n# 選字時按 Delete 也會加到這裡\n# 存檔後，下一次打字就會用到；# 開頭的行不會讀入\n");
  }

  private static string UserDataFolder() =>
      Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Project Key");

  private static void OpenUserFile(Button button, string name, string header) {
    string file = Path.Combine(UserDataFolder(), name);
    try {
      if (!File.Exists(file)) {
        Directory.CreateDirectory(UserDataFolder());
        File.WriteAllText(file, header);  // UTF-8 without a BOM, as the engine reads it
      }
      Process.Start(new ProcessStartInfo(file) { UseShellExecute = true });
    } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception) {
      button.Content = "無法開啟";
    }
  }

  // Shows the folder of the user's files (phrases.txt, exclude-phrases.txt, personal.txt) in Explorer.
  private void OnShowFolder(object sender, RoutedEventArgs e) {
    try {
      Directory.CreateDirectory(UserDataFolder());
      Process.Start(new ProcessStartInfo(UserDataFolder()) { UseShellExecute = true });
    } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception) {
      ShowFolder.Content = "無法開啟";
    }
  }

  // One request to the engine server. Without a server running there is nothing to tell it:
  // it starts with the choices cleared and the newest models.
  private static void SendToServer(byte request) {
    try {
      string sid = WindowsIdentity.GetCurrent().User!.Value;
      using var pipe = new NamedPipeClientStream(".", "ProjectKey-" + sid, PipeDirection.InOut);
      pipe.Connect(1000);
      pipe.ReadMode = PipeTransmissionMode.Message;
      // Protocol.h: kVersion (3), the request, an empty key event, no context. A server of another
      // version drops the message, so this follows kVersion.
      var message = new MemoryStream();
      var writer = new BinaryWriter(message);
      writer.Write(3u);
      writer.Write(request);
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
    }
  }
}
