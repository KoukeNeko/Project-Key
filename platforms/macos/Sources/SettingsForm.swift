// The settings content: one grouped form, as in System Settings.
import AppKit
import CryptoKit
import SwiftUI

struct SettingsForm: View {
  @AppStorage(PKAbbreviatedKey) private var abbreviated = false
  @AppStorage(PKKeypadPicksKey) private var keypadPicks = false
  @AppStorage(PKRememberChoicesKey) private var rememberChoices = true
  @AppStorage(PKShiftToggleKey) private var shiftToggle = true
  @AppStorage(PKCapsLockToggleKey) private var capsLockToggle = false
  @AppStorage(PKEnglishToZhuyinKey) private var englishToZhuyin = true
  @AppStorage(PKZhuyinToEnglishKey) private var zhuyinToEnglish = true
  @AppStorage(PKAppearanceKey) private var appearance = 1
  @AppStorage(PKProofPreviewKey) private var proofPreview = 1
  @AppStorage(PKChoosingProofKey) private var choosingProof = 1
  @AppStorage(PKCandidatePositionKey) private var candidatePosition = 1
  @AppStorage(PKNextDoubtfulKey) private var nextDoubtful = true
  @State private var cleared = false
  @State private var update = UpdateState.checking
  @ObservedObject var models: ModelsUpdater
  let clearChoices: () -> Void

  var body: some View {
    Form {
      Section("選字") {
        Picker("選字位置", selection: $candidatePosition) {
          Text("游標後（Windows）").tag(0)
          Text("游標前（macOS）").tag(1)
        }
        Toggle("選完跳到下一個可疑處", isOn: $nextDoubtful)
        Toggle(isOn: $abbreviated) {
          Text("簡拼")
          Text("開啟時 ㄨㄇ 為「我們」，可省略聲調連打；關閉時同一般注音")
        }
        Toggle(isOn: $keypadPicks) {
          Text("數字鍵盤選字")
          Text("開啟時數字鍵盤的 1–6 直接選字；主鍵盤的 1–6 仍是注音")
        }
        Toggle("記住選過的字", isOn: $rememberChoices)
        LabeledContent("選字紀錄") {
          Button("清除") {
            clearChoices()
            cleared = true
          }
          .disabled(cleared)
        }
      }
      Section("外觀") {
        Picker("候選視窗", selection: $appearance) {
          Text("深色").tag(0)
          Text("Liquid Glass").tag(1)
        }
        .disabled(!PKGlassAvailable())
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
      Section("按鍵") {
        Toggle(isOn: $capsLockToggle) {
          Text("⇪ 切換中英")
          Text("開啟時大寫用 ⇧；關閉時 ⇪ 鎖定大寫")
        }
        Toggle("單按 ⇧ 切換中英", isOn: $shiftToggle)
        Toggle("⇥ 英文轉注音", isOn: $englishToZhuyin)
        Toggle("⇥ 注音轉英文", isOn: $zhuyinToEnglish)
      }
      Section("關於") {
        LabeledContent("版本", value: currentVersion)
        LabeledContent("更新") {
          switch update {
          case .checking: Text("檢查中…")
          case .current: Text("已是最新版本")
          case .available(let version, let page): Link("下載 \(version)", destination: page)
          case .failed: Text("無法檢查")
          }
        }
        LabeledContent("選字模型") {
          switch models.state {
          case .current: Text(models.installed)
          case .available(let release): Button("下載 \(release.date)（\(release.megabytes) MB）") { models.download(release) }
          case .downloading(let fraction): Text("下載中 \(Int(fraction * 100))%")
          case .failed: Text("無法下載")
          }
        }
        LabeledContent("歡迎視窗") {
          Button("開啟") { WelcomeWindow.show() }
        }
        LabeledContent("授權", value: "MIT")
        LabeledContent("引擎與詞庫") {
          Link("McBopomofo", destination: URL(string: "https://github.com/openvanilla/McBopomofo")!)
        }
      }
    }
    .formStyle(.grouped)
    .scrollDisabled(true)
    .frame(width: 440)
    .fixedSize(horizontal: false, vertical: true)
    .task { update = await checkForUpdate() }
    .task { await models.check() }
  }
}

private let currentVersion = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""

private enum UpdateState {
  case checking, current, available(String, URL), failed
}

// The newest GitHub release, which brew and scoop install from.
private func checkForUpdate() async -> UpdateState {
  struct Release: Decodable {
    let tag_name: String
    let html_url: URL
  }
  let api = URL(string: "https://api.github.com/repos/KoukeNeko/Project-Key/releases/latest")!
  guard let (data, response) = try? await URLSession.shared.data(from: api),
        (response as? HTTPURLResponse)?.statusCode == 200,
        let release = try? JSONDecoder().decode(Release.self, from: data) else {
    return .failed
  }
  let latest = release.tag_name.hasPrefix("v") ? String(release.tag_name.dropFirst()) : release.tag_name
  return latest.compare(currentVersion, options: .numeric) == .orderedDescending
      ? .available(latest, release.html_url) : .current
}

// Models (tools/bigram-trainer) newer than the ones in use, from the GitHub releases
// fetch-models.sh publishes as models-YYYY-MM-DD, downloaded when the user asks. GitHub gives
// each file's SHA-256. One for all settings forms, so a download outlives the window.
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

  static var shared: ModelsUpdater?
  @Published private(set) var state = State.current
  private let models: PKModels

  init(models: PKModels) { self.models = models }

  // What the row says while nothing newer is offered.
  var installed: String { models.loaded ? (models.version.isEmpty ? "已安裝" : models.version) : "未安裝" }

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
      release.tag_name.hasPrefix("models-") && !release.draft
          && ["data.txt", "bigram.bin", "chars.bin"].allSatisfy { name in release.assets.contains { $0.name == name && $0.digest != nil } }
    }
    guard let newest = complete.max(by: { $0.tag_name < $1.tag_name }) else { return }
    let date = String(newest.tag_name.dropFirst("models-".count))
    state = date > models.version ? .available(Release(date: date, files: newest.assets)) : .current
  }

  func download(_ release: Release) {
    state = .downloading(0)
    Task {
      let fileManager = FileManager.default
      let partial = models.downloads.appendingPathComponent(release.date + ".partial", isDirectory: true)
      do {
        for left in (try? fileManager.contentsOfDirectory(at: models.downloads, includingPropertiesForKeys: nil)) ?? []
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
        let target = models.downloads.appendingPathComponent(release.date, isDirectory: true)
        try? fileManager.removeItem(at: target)
        try fileManager.moveItem(at: partial, to: target)
        state = models.reload() ? .current : .failed
      } catch {
        try? fileManager.removeItem(at: partial)
        state = .failed
      }
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
    let updater = ModelsUpdater.shared ?? ModelsUpdater(models: models)
    ModelsUpdater.shared = updater
    return NSHostingView(rootView: SettingsForm(models: updater, clearChoices: clearChoices))
  }
}
