// The settings content: a grouped form for each page, as in System Settings.
import AppKit
import CryptoKit
import SwiftUI

struct SettingsForm: View {
  @AppStorage(PKOmitTonesKey) private var omitTones = false
  @AppStorage(PKAbbreviatedKey) private var abbreviated = false
  @AppStorage(PKFuzzySoundsKey) private var fuzzySounds = false
  @AppStorage(PKAssociatedPhrasesKey) private var associatedPhrases = false
  @AppStorage(PKKeypadPicksKey) private var keypadPicks = false
  @AppStorage(PKRememberChoicesKey) private var rememberChoices = true
  @AppStorage(PKShiftToggleKey) private var shiftToggle = true
  @AppStorage(PKFullWidthToggleKey) private var fullWidthToggle = true
  @AppStorage(PKCapsLockToggleKey) private var capsLockToggle = false
  @AppStorage(PKEnglishToZhuyinKey) private var englishToZhuyin = true
  @AppStorage(PKZhuyinToEnglishKey) private var zhuyinToEnglish = true
  @AppStorage(PKDictationKey) private var dictation = false
  @AppStorage(PKSimplifiedKey) private var simplified = false
  @AppStorage(PKSimplifiedToggleKey) private var simplifiedToggle = false
  @AppStorage(PKCheckForUpdatesKey) private var checkForUpdates = true
  @AppStorage(PKAppearanceKey) private var appearance = 3
  @AppStorage(PKProofPreviewKey) private var proofPreview = 1
  @AppStorage(PKChoosingProofKey) private var choosingProof = 1
  @AppStorage(PKCandidatePositionKey) private var candidatePosition = 1
  @AppStorage(PKNextDoubtfulKey) private var nextDoubtful = true
  @AppStorage(PKGridColumnsKey) private var gridColumns = 0
  @AppStorage(PKKeyboardLayoutKey) private var keyboardLayout = 0
  @State private var cleared = false
  @State private var restoring = false
  @State private var removeFailed = false
  @State private var update = UpdateState.checking
  @State private var removingAdvanced = false
  @State private var advancedRemoveFailed = false
  @State private var removingSpeech = false
  @State private var speechRemoveFailed = false
  @ObservedObject var models: ModelsUpdater
  @ObservedObject var advanced: ModelsUpdater
  @ObservedObject var speech: ModelsUpdater
  let clearChoices: () -> Void

  // The sections as pages, switched between above them as on the other platforms.
  private enum Page: String, CaseIterable { case choose = "選字", appearance = "外觀", keys = "按鍵", about = "關於" }
  @State private var page = Page.choose

  var body: some View {
    VStack(spacing: 0) {
      Picker("", selection: $page) {
        ForEach(Page.allCases, id: \.self) { Text($0.rawValue) }
      }
      .pickerStyle(.segmented)
      .labelsHidden()
      .fixedSize()
      .padding(.top, 12)
      Form {
        switch page {
        case .choose:
          Section {
            Picker("選字位置", selection: $candidatePosition) {
              Text("游標後（Windows）").tag(0)
              Text("游標前（macOS）").tag(1)
            }
            Picker("每列欄數", selection: $gridColumns) {
              ForEach(0..<4) { Text("\($0 + 6)").tag($0) }
            }
            Toggle("選完跳到下一個可疑處", isOn: $nextDoubtful)
            Toggle(isOn: $omitTones) {
              Text("省略聲調")
              Text("開啟時拼完整音節可直接打下一個，ㄕㄉㄜ 為「是的」；關閉時按到已有注音的位置會換掉它，ㄋㄧ 再按 ㄨ 為 ㄋㄨ")
            }
            Toggle(isOn: $abbreviated) {
              Text("簡拼")
              Text("開啟時 ㄨㄇ 為「我們」，可省略聲調連打；關閉時同一般注音")
            }
            Toggle(isOn: $fuzzySounds) {
              Text("近似音")
              Text("開啟時，ㄓ/ㄗ、ㄔ/ㄘ、ㄕ/ㄙ、ㄣ/ㄥ、ㄢ/ㄤ 的字也會列出，排在原音之後")
            }
            Toggle(isOn: $associatedPhrases) {
              Text("聯想詞")
              Text("開啟時，送出後列出常接在後面的詞，如「輸入」之後的「法」，按 ⇧ 與數字鍵選取")
            }
            Toggle(isOn: $keypadPicks) {
              Text("數字鍵盤選字")
              Text("開啟時數字鍵盤直接選字；主鍵盤的數字仍是注音")
            }
            Toggle("記住選過的字", isOn: $rememberChoices)
            Toggle("簡體中文", isOn: $simplified)
            LabeledContent("選字紀錄") {
              Button("清除") {
                clearChoices()
                cleared = true
              }
              .disabled(cleared)
            }
            LabeledContent {
              Button("編輯…") { PKEditUserPhrases() }
            } label: {
              Text("自訂詞")
              Text("每行一個詞和注音，如 小鍵 ㄒㄧㄠˇ-ㄐㄧㄢˋ")
            }
            LabeledContent {
              Button("編輯…") { PKEditExcludedPhrases() }
            } label: {
              Text("排除詞")
              Text("不再出現的詞，格式同自訂詞；選字時按 ⌦ 也會加入")
            }
            LabeledContent {
              Button("顯示") { PKShowDataFolder() }
            } label: {
              Text("資料夾")
              Text("自訂詞、排除詞與學到的字串，複製檔案即可備份")
            }
          }
        case .appearance:
          Section {
            Picker("候選視窗", selection: $appearance) {
              Text("自動").tag(3)
              Text("淺色").tag(2)
              Text("深色").tag(0)
              if PKGlassAvailable() { Text("Liquid Glass").tag(1) }
            }
            Picker("輸入時的整句預覽", selection: $proofPreview) {
              Text("永遠").tag(0)
              Text("有可疑字時").tag(1)
              Text("關閉").tag(2)
            }
            Picker("選字時的整句預覽", selection: $choosingProof) {
              Text("永遠").tag(0)
              Text("有可疑字時").tag(1)
              Text("關閉").tag(2)
            }
          }
        case .keys:
          Section {
            Picker("鍵盤排列", selection: $keyboardLayout) {
              Text("大千").tag(0)
              Text("倚天").tag(1)
              Text("許氏").tag(2)
              Text("倚天 26 鍵").tag(3)
              Text("IBM").tag(4)
            }
            Toggle(isOn: $capsLockToggle) {
              Text("⇪ 切換中英")
              Text("開啟時大寫用 ⇧；關閉時 ⇪ 鎖定大寫")
            }
            Toggle("單按 ⇧ 切換中英", isOn: $shiftToggle)
            Toggle("⇧ Space 切換全形", isOn: $fullWidthToggle)
            Toggle("⇥ 英文轉注音", isOn: $englishToZhuyin)
            Toggle("⇥ 注音轉英文", isOn: $zhuyinToEnglish)
            Toggle("⌃⇧F 切換繁簡", isOn: $simplifiedToggle)
            if speech.supported {
              Toggle(isOn: $dictation) {
                Text("語音輸入（Alpha）")
                Text("按住右 ⌥ 說話，放開後輸入；需下載「關於」的語音模型")
              }
            }
          }
        case .about:
          Section {
            LabeledContent("版本", value: currentVersion)
            LabeledContent("更新") {
              switch update {
              case .checking: Text("檢查中…")
              case .current: Text("已是最新版本")
              case .available(let version, let page): Link("下載 \(version)", destination: page)
              case .installable(let version, let file):
                Button("更新到 \(version)") {
                  update = .installing(0)
                  Task { update = await installUpdate(file, progress: { update = .installing($0) }) ? .current : .installFailed }
                }
              case .installing(let fraction): Text(fraction < 1 ? "下載中 \(Int(fraction * 100))%" : "安裝中…")
              case .installFailed: Text("無法更新")
              case .failed: Text("無法檢查")
              }
            }
            Toggle("自動檢查更新", isOn: $checkForUpdates)
            LabeledContent("選字模型") { ModelsState(updater: models) }
            if advanced.supported {
              LabeledContent("進階模型") {
                HStack {
                  ModelsState(updater: advanced, current: advancedRemoveFailed ? "無法移除" : nil)
                  if advanced.removable { Button("移除") { removingAdvanced = true } }
                }
              }
              .alert("移除進階模型？", isPresented: $removingAdvanced) {
                Button("移除", role: .destructive) { Task { advancedRemoveFailed = !(await advanced.remove()) } }
                Button("取消", role: .cancel) {}
              } message: {
                Text("打字停頓時，不再用進階模型重選整句。")
              }
            }
            if speech.supported {
              LabeledContent("語音模型") {
                HStack {
                  ModelsState(updater: speech, current: speechRemoveFailed ? "無法移除" : nil)
                  if speech.removable { Button("移除") { removingSpeech = true } }
                }
              }
              .alert("移除語音模型？", isPresented: $removingSpeech) {
                Button("移除", role: .destructive) { Task { speechRemoveFailed = !(await speech.remove()) } }
                Button("取消", role: .cancel) {}
              } message: {
                Text("按住右 ⌥ 不再錄音輸入。")
              }
            }
            LabeledContent("歡迎視窗") {
              Button("開啟") { WelcomeWindow.show(models: models, advanced: advanced) }
            }
            LabeledContent("授權", value: "MIT")
            LabeledContent("引擎與詞庫") {
              Link("McBopomofo", destination: URL(string: "https://github.com/openvanilla/McBopomofo")!)
            }
          }
          Section("重設") {
            LabeledContent("恢復原廠設定") {
              if removeFailed {
                Text("無法刪除資料")
              } else {
                Button("恢復…") { restoring = true }
              }
            }
            .alert("恢復原廠設定？", isPresented: $restoring) {
              Button("恢復設定") { PKRestoreSettings() }
              Button("一併刪除資料", role: .destructive) {
                PKRestoreSettings()
                removeFailed = !PKRemoveUserWords()
                clearChoices()  // personal.txt, and the choices the input method holds in memory
                cleared = true
              }
              Button("取消", role: .cancel) {}
            } message: {
              Text("設定將改回預設值。「一併刪除資料」另刪除自訂詞、排除詞與選字紀錄，無法復原；下載的選字模型保留。")
            }
          }
        }
      }
      .formStyle(.grouped)
      .scrollDisabled(true)
    }
    .frame(width: 440)
    .fixedSize(horizontal: false, vertical: true)
    .task { update = await checkForUpdate() }
    .task { await models.check() }
    .task { if advanced.supported { await advanced.check() } }
    .task { if speech.supported { await speech.check() } }
  }
}

private let currentVersion = PKInputMethodBundle()?.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""

private enum UpdateState {
  case checking, current, failed
  case available(String, URL)                  // only its page: the release has no macOS zip GitHub gives a SHA-256 for
  case installable(String, ModelsUpdater.File)  // its macOS zip
  case installing(Double)                       // the fraction downloaded; 1 while it is put in place
  case installFailed
}

// The newest GitHub release, which brew and scoop install from. A newer one is offered as an update.
private func checkForUpdate() async -> UpdateState {
  struct Release: Decodable {
    let tag_name: String
    let html_url: URL
    let assets: [ModelsUpdater.File]
  }
  let api = URL(string: "https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest")!
  guard let (data, response) = try? await URLSession.shared.data(from: api),
        (response as? HTTPURLResponse)?.statusCode == 200,
        let release = try? JSONDecoder().decode(Release.self, from: data) else {
    return .failed
  }
  let latest = release.tag_name.hasPrefix("v") ? String(release.tag_name.dropFirst()) : release.tag_name
  guard latest.compare(currentVersion, options: .numeric) == .orderedDescending else { return .current }
  if let zip = release.assets.first(where: { $0.name == "ProjectKey-\(latest)-macos.zip" && $0.digest != nil }) {
    return .installable(latest, zip)
  }
  return .available(latest, release.html_url)
}

// Downloads the release's macOS zip, checks it against GitHub's SHA-256, puts its ProjectKey.app in
// place of the installed one, as the Homebrew cask installs it, and registers it. Then the running
// input method quits and the new one opens; false when it could not be updated, and nothing was replaced.
// shortcut: written without a Mac to run it on; check it on one before relying on it.
@MainActor
private func installUpdate(_ zip: ModelsUpdater.File, progress: @escaping @MainActor (Double) -> Void) async -> Bool {
  let fileManager = FileManager.default
  guard let bundle = PKInputMethodBundle()?.bundleURL else { return false }
  do {
    // On the bundle's volume, so the new one moves into place rather than being copied.
    let work = try fileManager.url(for: .itemReplacementDirectory, in: .userDomainMask, appropriateFor: bundle, create: true)
    defer { try? fileManager.removeItem(at: work) }
    let archive = work.appendingPathComponent(zip.name)
    let response = try await fetch(zip.browser_download_url, to: archive) { received in
      progress(min(0.99, Double(received) / Double(max(zip.size, 1))))
    }
    guard (response as? HTTPURLResponse)?.statusCode == 200,
          try await Task.detached(operation: { try sha256(of: archive) }).value == zip.digest else {
      throw URLError(.badServerResponse)
    }
    progress(1)
    let app = work.appendingPathComponent("ProjectKey.app")
    try await run("/usr/bin/ditto", ["-x", "-k", archive.path, work.path])
    // Ad-hoc signed and not notarized: Gatekeeper refuses it while quarantined (as the cask's postflight says).
    try? await run("/usr/bin/xattr", ["-dr", "com.apple.quarantine", app.path])
    _ = try fileManager.replaceItemAt(bundle, withItemAt: app)
    try await run(bundle.appendingPathComponent("Contents/MacOS/ProjectKey").path, ["--register"])
  } catch {
    return false
  }
  // Opened again once this process has quit, as the cask's postflight opens it after installing.
  let reopen = Process()
  reopen.executableURL = URL(fileURLWithPath: "/bin/sh")
  reopen.arguments = ["-c", "sleep 1; /usr/bin/open -g \"$0\"", bundle.path]
  try? reopen.run()
  if Bundle.main.bundleURL == bundle { exit(0) }
  // The settings app: the input method quits as install.sh stops it, and this window stays.
  for app in NSRunningApplication.runningApplications(withBundleIdentifier: PKInputMethodBundleID) { app.terminate() }
  return true
}

// Runs a program to its end; throws when it fails.
private func run(_ program: String, _ arguments: [String]) async throws {
  try await Task.detached {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: program)
    process.arguments = arguments
    try process.run()
    process.waitUntilExit()
    guard process.terminationStatus == 0 else { throw CocoaError(.executableLoad) }
  }.value
}

// Models (tools/bigram-trainer) newer than the ones in use, from the GitHub releases
// fetch-models.sh publishes as models-YYYY-MM-DD, downloaded when the user asks or by the welcome
// window; the advanced model (core/engine/AdvancedModel.h) from advanced-YYYY-MM-DD releases and the
// speech model (core/engine/SpeechModel.h) from speech-YYYY-MM-DD ones, which can also be removed.
// GitHub gives each file's SHA-256. One of each for all settings and welcome windows, so a download
// outlives the window.
@MainActor
final class ModelsUpdater: ObservableObject {
  struct Release {
    let date: String
    let files: [File]
    var megabytes: Int64 { files.reduce(0) { $0 + $1.size } / 1_000_000 }
  }
  struct File: Decodable {
    let name: String
    let size: Int64
    let digest: String?  // sha256:<hex>
    let browser_download_url: URL
  }
  enum State {
    case current, available(Release), downloading(Double), failed
  }

  enum Kind { case models, advanced, speech }

  private static var shared: (ModelsUpdater, ModelsUpdater)?
  private static var sharedSpeech: ModelsUpdater?

  static func shared(for models: PKModels) -> (ModelsUpdater, ModelsUpdater) {
    if let shared { return shared }
    let made = (ModelsUpdater(models: models), ModelsUpdater(models: models, kind: .advanced))
    shared = made
    return made
  }
  static func speech(for models: PKModels) -> ModelsUpdater {
    if let sharedSpeech { return sharedSpeech }
    let made = ModelsUpdater(models: models, kind: .speech)
    sharedSpeech = made
    return made
  }
  @Published private(set) var state = State.current
  private let models: PKModels
  private let kind: Kind

  init(models: PKModels, kind: Kind = .models) {
    self.models = models
    self.kind = kind
  }

  private var prefix: String {
    switch kind {
    case .models: "models-"
    case .advanced: "advanced-"
    case .speech: "speech-"
    }
  }
  private var required: [String] {
    switch kind {
    case .models: ["data.txt", "bigram.bin", "chars.bin"]
    case .advanced: ["advanced.gguf"]
    case .speech: ["speech.gguf", "speech-encoder.gguf"]
    }
  }
  private var version: String {
    switch kind {
    case .models: models.version
    case .advanced: models.advancedVersion
    case .speech: models.speechVersion
    }
  }
  private var downloads: URL {
    switch kind {
    case .models: models.downloads
    case .advanced: models.advancedDownloads
    case .speech: models.speechDownloads
    }
  }
  // The advanced and speech models' rows are shown only where the input method can run what they download.
  var supported: Bool { kind == .models || models.advancedSupported }
  var removable: Bool {
    if case .downloading(_) = state { return false }
    return kind != .models && !version.isEmpty
  }

  // What the row says while nothing newer is offered.
  var installed: String {
    if kind != .models { return version.isEmpty ? "未安裝" : version }
    return models.loaded ? (models.version.isEmpty ? "已安裝" : models.version) : "未安裝"
  }

  func check() async {
    switch state {
    case .current, .failed: break
    case .available, .downloading: return
    }
    struct GitHubRelease: Decodable {
      let tag_name: String
      let draft: Bool
      let assets: [File]
    }
    let api = URL(string: "https://api.github.com/repos/KoukeNeko/Project-Key/releases?per_page=30")!
    guard let (data, response) = try? await URLSession.shared.data(from: api),
          (response as? HTTPURLResponse)?.statusCode == 200,
          let releases = try? JSONDecoder().decode([GitHubRelease].self, from: data) else {
      return  // the 更新 row already says when GitHub cannot be reached
    }
    let complete = releases.filter { release in
      release.tag_name.hasPrefix(prefix) && !release.draft
          && required.allSatisfy { name in release.assets.contains { $0.name == name && $0.digest != nil } }
    }
    guard let newest = complete.max(by: { $0.tag_name < $1.tag_name }) else { return }
    let date = String(newest.tag_name.dropFirst(prefix.count))
    state = date > version ? .available(Release(date: date, files: newest.assets)) : .current
  }

  // Removes every advanced or speech model downloaded and has the input method drop it, then offers the
  // newest again; false when one could not be removed.
  func remove() async -> Bool {
    let fileManager = FileManager.default
    let removed = (try? fileManager.removeItem(at: downloads)) != nil || !fileManager.fileExists(atPath: downloads.path)
    models.reloadAdvanced()
    objectWillChange.send()  // removable and installed read the folder
    state = .current
    await check()
    return removed
  }

  func download(_ release: Release) {
    state = .downloading(0)
    Task {
      let fileManager = FileManager.default
      let partial = downloads.appendingPathComponent(release.date + ".partial", isDirectory: true)
      do {
        for left in (try? fileManager.contentsOfDirectory(at: downloads, includingPropertiesForKeys: nil)) ?? []
        where left.pathExtension == "partial" {
          try fileManager.removeItem(at: left)  // a download cut short
        }
        try fileManager.createDirectory(at: partial, withIntermediateDirectories: true)
        let total = Double(release.files.reduce(0) { $0 + $1.size })
        var done: Int64 = 0
        for file in release.files {
          let target = partial.appendingPathComponent(file.name)
          let base = done
          let response = try await fetch(file.browser_download_url, to: target) { received in
            self.state = .downloading(Double(base + received) / total)
          }
          guard (response as? HTTPURLResponse)?.statusCode == 200,
                try await Task.detached(operation: { try sha256(of: target) }).value == file.digest else {
            throw URLError(.badServerResponse)
          }
          done += file.size
        }
        // Written last: a folder with it is complete (core/engine/Models.h).
        try Data((release.date + "\n").utf8).write(to: partial.appendingPathComponent("models-version.txt"))
        let target = downloads.appendingPathComponent(release.date, isDirectory: true)
        try? fileManager.removeItem(at: target)
        try fileManager.moveItem(at: partial, to: target)
        if kind != .models {
          models.reloadAdvanced()  // it loads in the background; a file it cannot load leaves the walk's choices
          state = .current
        } else {
          state = models.reload() ? .current : .failed
        }
      } catch {
        try? fileManager.removeItem(at: partial)
        state = .failed
      }
    }
  }
}

// What a models row shows: the version in use (or current), a button that downloads a newer one,
// or the download's progress.
struct ModelsState: View {
  @ObservedObject var updater: ModelsUpdater
  var current: String? = nil

  var body: some View {
    switch updater.state {
    case .current: Text(current ?? updater.installed)
    case .available(let release): Button("下載 \(release.date)（\(release.megabytes) MB）") { updater.download(release) }
    case .downloading(let fraction): Text("下載中 \(Int(fraction * 100))%")
    case .failed: Text("無法下載")
    }
  }
}

// Downloads url into target, reporting the bytes received so far every quarter second.
private func fetch(_ url: URL, to target: URL, received: @escaping @MainActor (Int64) -> Void) async throws -> URLResponse {
  final class Download: @unchecked Sendable {
    var task: URLSessionDownloadTask?
  }
  let download = Download()
  let poll = Task { @MainActor in
    while !Task.isCancelled {
      if let task = download.task { received(task.countOfBytesReceived) }
      try? await Task.sleep(nanoseconds: 250_000_000)
    }
  }
  defer { poll.cancel() }
  return try await withCheckedThrowingContinuation { continuation in
    let task = URLSession.shared.downloadTask(with: url) { temporary, response, error in
      // The temporary file is gone once this returns.
      guard let temporary, let response else { return continuation.resume(throwing: error ?? URLError(.unknown)) }
      do {
        try FileManager.default.moveItem(at: temporary, to: target)
        continuation.resume(returning: response)
      } catch {
        continuation.resume(throwing: error)
      }
    }
    download.task = task
    task.resume()
  }
}

private func sha256(of file: URL) throws -> String {
  let handle = try FileHandle(forReadingFrom: file)
  defer { try? handle.close() }
  var hash = SHA256()
  while let chunk = try handle.read(upToCount: 1 << 20), !chunk.isEmpty {
    hash.update(data: chunk)
  }
  return "sha256:" + hash.finalize().map { String(format: "%02x", $0) }.joined()
}

@objc(PKSettingsForm)
final class SettingsFormFactory: NSObject {
  @MainActor @objc static func makeView(clearChoices: @escaping () -> Void, models: PKModels) -> NSView {
    let (updater, advanced) = ModelsUpdater.shared(for: models)
    return NSHostingView(rootView: SettingsForm(models: updater, advanced: advanced, speech: ModelsUpdater.speech(for: models),
                                                clearChoices: clearChoices).defaultAppStorage(PKDefaults()))
  }
}
