// The settings content: one grouped form, as in System Settings.
import AppKit
import SwiftUI

struct SettingsForm: View {
  @AppStorage(PKAdjacentKeysKey) private var adjacentKeys = true
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
  @AppStorage(PKKeyboardLayoutKey) private var keyboardLayout = 0
  @State private var cleared = false
  @State private var update = UpdateState.checking
  let clearChoices: () -> Void

  var body: some View {
    Form {
      Section("選字") {
        Picker("選字位置", selection: $candidatePosition) {
          Text("游標後（Windows）").tag(0)
          Text("游標前（macOS）").tag(1)
        }
        Toggle("選完跳到下一個可疑處", isOn: $nextDoubtful)
        Toggle("鄰鍵修正", isOn: $adjacentKeys)
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
        Picker("輸入法", selection: $keyboardLayout) {
          Text("大千注音").tag(0)
          Text("漢語拼音").tag(1)
        }
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

@objc(PKSettingsForm)
final class SettingsFormFactory: NSObject {
  @objc static func makeView(clearChoices: @escaping () -> Void) -> NSView {
    NSHostingView(rootView: SettingsForm(clearChoices: clearChoices))
  }
}
