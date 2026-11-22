// The host app: how to turn the keyboard on, its settings, and a field to try it in.
import SwiftUI

@main
struct ProjectKeyApp: App {
  var body: some Scene {
    WindowGroup { ContentView() }
  }
}

// The app group the keyboard reads its settings from, as the keyboard's Info.plist names it.
private let sharedDefaults: UserDefaults? = {
  guard let keyboard = Bundle.main.builtInPlugInsURL?.appendingPathComponent("Keyboard.appex"),
    let group = Bundle(url: keyboard)?.object(forInfoDictionaryKey: "PKAppGroup") as? String
  else { return nil }
  return UserDefaults(suiteName: group)
}()

struct ContentView: View {
  @State private var text = ""
  @State private var cleared = false
  // The names and defaults of the engine's settings (core/engine/Controller.cpp, Settings::Name and
  // DefaultValue), except 簡拼 and 聯想詞, on by default here as the keyboard has them.
  @AppStorage("OmitTones", store: sharedDefaults) private var omitTones = true
  @AppStorage("Abbreviated", store: sharedDefaults) private var abbreviated = true
  @AppStorage("FuzzySounds", store: sharedDefaults) private var fuzzySounds = false
  @AppStorage("AssociatedPhrases", store: sharedDefaults) private var associatedPhrases = true
  @AppStorage("RememberChoices", store: sharedDefaults) private var rememberChoices = true
  @AppStorage("IOS18Style", store: sharedDefaults) private var ios18Style = false  // the keyboard's kIOS18StyleKey
  @AppStorage("Haptics", store: sharedDefaults) private var haptics = 1  // kHapticsKey: 0 off, 1 light, 2 medium, 3 heavy
  @AppStorage("CharacterPreview", store: sharedDefaults) private var characterPreview = true  // kCharacterPreviewKey
  // One item a line, with the keyboard's defaults (kShortcutsKey, kCustomSymbolsKey).
  @AppStorage("Shortcuts", store: sharedDefaults) private var shortcuts = "。\n？\n！\n、\n「\n」\n：\n…"
  @AppStorage("CustomSymbols", store: sharedDefaults) private var customSymbols =
    "(´・ω・`)\n(╯°□°)╯︵ ┻━┻\n¯\\_(ツ)_/¯\n(｡•̀ᴗ-)✧\nಠ_ಠ"

  var body: some View {
    NavigationStack {
      Form {
        Section {
          Text("設定 > 一般 > 鍵盤 > 鍵盤 > 新增鍵盤 > Project Key")
          Text("設定 > 一般 > 鍵盤 > 鍵盤 > Project Key > 允許完整取用")
          Button("打開設定") {
            if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) }
          }
        } header: {
          Text("啟用鍵盤")
        } footer: {
          Text("完整取用只用來讀取下列設定，鍵盤不連網路。")
        }
        Section {
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
            Text("開啟時，送出後列出常接在後面的詞，如「輸入」之後的「法」")
          }
          Toggle("記住選過的字", isOn: $rememberChoices)
          Toggle(isOn: $ios18Style) {
            Text("iOS 18 樣式")
            Text("開啟時縮短候選字下方的空間；iOS 26 起從其他鍵盤切換過來時，上方可能多出一條空白")
          }
          Picker("觸覺回饋", selection: $haptics) {
            Text("關閉").tag(0)
            Text("輕").tag(1)
            Text("中").tag(2)
            Text("重").tag(3)
          }
          Toggle("字元預覽", isOn: $characterPreview)
          NavigationLink {
            ItemListView(title: "快捷列", lines: $shortcuts)
          } label: {
            LabeledContent("快捷列", value: "\(splitLines(shortcuts).count) 項")
          }
          NavigationLink {
            ItemListView(title: "自訂符號", lines: $customSymbols)
          } label: {
            LabeledContent("自訂符號", value: "\(splitLines(customSymbols).count) 項")
          }
          LabeledContent("選字紀錄") {
            Button("清除") {
              sharedDefaults?.set((sharedDefaults?.integer(forKey: "ClearChoicesRequest") ?? 0) + 1, forKey: "ClearChoicesRequest")
              cleared = true
            }
            .disabled(cleared)
          }
        }
        .disabled(sharedDefaults == nil)
        Section("試打") {
          TextField("輸入文字", text: $text, axis: .vertical)
            .lineLimit(3...8)
        }
      }
      .navigationTitle("Project Key")
    }
  }
}

private func splitLines(_ lines: String) -> [String] {
  lines.isEmpty ? [] : lines.components(separatedBy: "\n")
}

// A list the keyboard reads one item a line: added at the bottom, removed and reordered in Edit.
struct ItemListView: View {
  let title: String
  @Binding var lines: String
  @State private var newItem = ""

  var body: some View {
    List {
      ForEach(splitLines(lines), id: \.self) { Text($0) }
        .onDelete { offsets in
          var items = splitLines(lines)
          items.remove(atOffsets: offsets)
          lines = items.joined(separator: "\n")
        }
        .onMove { offsets, destination in
          var items = splitLines(lines)
          items.move(fromOffsets: offsets, toOffset: destination)
          lines = items.joined(separator: "\n")
        }
      TextField("新增項目", text: $newItem)
        .onSubmit {
          let item = newItem.trimmingCharacters(in: .whitespacesAndNewlines)
          newItem = ""
          guard !item.isEmpty, !splitLines(lines).contains(item) else { return }  // an item is its own id
          lines = (splitLines(lines) + [item]).joined(separator: "\n")
        }
    }
    .navigationTitle(title)
    .toolbar { EditButton() }
  }
}
