using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
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
    Bind(KeypadPicks, "KeypadPicks", false);
    Bind(RememberChoices, "RememberChoices");
    Bind(CapsLockToggle, "CapsLockToggle", false);
    Bind(ShiftToggle, "ShiftToggle");
    Bind(FullWidthToggle, "FullWidthToggle");
    Bind(EnglishToZhuyin, "EnglishToZhuyin");
    Bind(ZhuyinToEnglish, "ZhuyinToEnglish");
    Bind(Appearance, "Appearance", 1);
    Bind(ProofPreview, "ProofPreview", 1);
    Bind(ChoosingProof, "ChoosingProof", 1);
    Bind(CandidatePosition, "CandidatePosition", 0);
    Bind(GridColumns, "GridColumns", 0);
    // The text service's version (ProjectKey.rc); build.ps1 puts this app in Settings\ beside it.
    Version.Text = FileVersionInfo.GetVersionInfo(Path.Combine(AppContext.BaseDirectory, "..", "ProjectKey.dll")).ProductVersion ?? "";
    CheckForModels();
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

  private record ModelsFile(string Name, long Size, string Digest, string Url);

  // The newest models-YYYY-MM-DD release fetch-models.sh published, offered when newer than the
  // models in use. GitHub gives each file's SHA-256.
  private async void CheckForModels() {
    Models.Content = InstalledModelsText();
    try {
      using var client = new HttpClient();
      client.DefaultRequestHeaders.UserAgent.ParseAdd("ProjectKey");
      using JsonDocument releases = JsonDocument.Parse(
          await client.GetStringAsync("https://api.github.com/repos/KoukeNeko/Project-Key/releases?per_page=30"));
      string newestTag = "";
      List<ModelsFile> newestFiles = [];
      foreach (JsonElement release in releases.RootElement.EnumerateArray()) {
        string tag = release.GetProperty("tag_name").GetString()!;
        if (!tag.StartsWith("models-") || release.GetProperty("draft").GetBoolean() || string.CompareOrdinal(tag, newestTag) <= 0) continue;
        var files = new List<ModelsFile>();
        foreach (JsonElement asset in release.GetProperty("assets").EnumerateArray()) {
          if (asset.TryGetProperty("digest", out JsonElement digest) && digest.ValueKind == JsonValueKind.String) {
            files.Add(new ModelsFile(asset.GetProperty("name").GetString()!, asset.GetProperty("size").GetInt64(), digest.GetString()!,
                                     asset.GetProperty("browser_download_url").GetString()!));
          }
        }
        if (new[] { "data.txt", "bigram.bin", "chars.bin" }.All(name => files.Any(file => file.Name == name))) {
          newestTag = tag;
          newestFiles = files;
        }
      }
      string date = newestTag.Length > 0 ? newestTag["models-".Length..] : "";
      if (string.CompareOrdinal(date, ModelsVersion(ModelsInUse())) <= 0) return;
      var button = new Button { Content = $"下載 {date}（{newestFiles.Sum(file => file.Size) / 1_000_000} MB）" };
      button.Click += (_, _) => DownloadModels(date, newestFiles);
      Models.Content = button;
    } catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or JsonException
                                    or KeyNotFoundException or InvalidOperationException) {
      // The 更新 row already says when GitHub cannot be reached.
    }
  }

  private async void DownloadModels(string date, List<ModelsFile> files) {
    var status = new TextBlock { Text = "下載中 0%" };
    Models.Content = status;
    long total = files.Sum(file => file.Size);
    var progress = new Progress<long>(done => status.Text = $"下載中 {done * 100 / total}%");
    string partial = Path.Combine(DownloadedModels, date + ".partial");
    try {
      Directory.CreateDirectory(DownloadedModels);
      foreach (string left in Directory.GetDirectories(DownloadedModels, "*.partial")) Directory.Delete(left, true);  // cut short before
      // Off the UI thread: a continuation there for every read slows the download to a crawl.
      await Task.Run(() => Download(files, partial, progress));
      // Written last: a folder with it is complete.
      File.WriteAllText(Path.Combine(partial, "models-version.txt"), date + "\n");
      string folder = Path.Combine(DownloadedModels, date);
      if (Directory.Exists(folder)) Directory.Delete(folder, true);
      Directory.Move(partial, folder);
      SendToServer(11);  // Protocol.h: Request::kReloadModels
      Models.Content = InstalledModelsText();
    } catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or IOException
                                    or UnauthorizedAccessException or InvalidDataException) {
      try { Directory.Delete(partial, true); } catch (Exception cleanup) when (cleanup is IOException or UnauthorizedAccessException) { }
      Models.Content = new TextBlock { Text = "無法下載" };
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

  private static void Bind(ToggleSwitch toggle, string name, bool fallback = true) {
    toggle.IsOn = Read(name, fallback ? 1 : 0) != 0;
    toggle.Toggled += (_, _) => Write(name, toggle.IsOn ? 1 : 0);
  }

  private static void Bind(ComboBox box, string name, int fallback) {
    box.SelectedIndex = Math.Clamp(Read(name, fallback), 0, box.Items.Count - 1);
    box.SelectionChanged += (_, _) => Write(name, box.SelectedIndex);
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

  // Downloads and unpacks the release, then runs its install.ps1, which asks Windows for administrator
  // rights, stops this window and the engine server, installs, and starts both again.
  private async void InstallUpdate(ModelsFile zip) {
    var status = new TextBlock { Text = "下載中 0%" };
    Update.Content = status;
    var progress = new Progress<long>(done => status.Text = $"下載中 {done * 100 / zip.Size}%");
    try {
      if (Directory.Exists(UpdateFolder)) Directory.Delete(UpdateFolder, true);
      await Task.Run(() => Download([zip], UpdateFolder, progress));
      string package = Path.Combine(UpdateFolder, "package");
      await Task.Run(() => ZipFile.ExtractToDirectory(Path.Combine(UpdateFolder, zip.Name), package));
      status.Text = "安裝中…";
      var start = new ProcessStartInfo("powershell.exe") { UseShellExecute = false, CreateNoWindow = true };
      foreach (string argument in new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", Path.Combine(package, "install.ps1"),
                                          "-Source", package, "-Update" }) {
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
    string file = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Project Key", "phrases.txt");
    try {
      if (!File.Exists(file)) {
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        File.WriteAllText(file, "# 每行一個詞：詞、空白、注音\n# 注音的音節以 - 連接，中間不留空白\n# 一聲不標；其他聲調寫在音節後，輕聲寫 ˙（ㄉㄜ˙）\n# 例：小鍵 ㄒㄧㄠˇ-ㄐㄧㄢˋ\n# 存檔後，下一次打字就會用到；# 開頭的行不會讀入\n");  // UTF-8 without a BOM, as the engine reads it
      }
      Process.Start(new ProcessStartInfo(file) { UseShellExecute = true });
    } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or System.ComponentModel.Win32Exception) {
      EditPhrases.Content = "無法開啟";
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
