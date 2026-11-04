// The settings content: one grouped form, as in System Settings.
import AppKit
import SwiftUI

struct SettingsForm: View {
  @AppStorage(PKAdjacentKeysKey) private var adjacentKeys = true
  @AppStorage(PKRememberChoicesKey) private var rememberChoices = true
  @AppStorage(PKShiftToggleKey) private var shiftToggle = true
  @AppStorage(PKAppearanceKey) private var appearance = 0
  @AppStorage(PKProofPreviewKey) private var proofPreview = 0
  @State private var cleared = false
  let clearChoices: () -> Void

  var body: some View {
    Form {
      Section("選字") {
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
        Picker("整句預覽", selection: $proofPreview) {
          Text("永遠").tag(0)
          Text("有可疑字時").tag(1)
          Text("關閉").tag(2)
        }
      }
      Section("按鍵") {
        Toggle("單按 ⇧ 切換中英", isOn: $shiftToggle)
      }
    }
    .formStyle(.grouped)
    .scrollDisabled(true)
    .frame(width: 440)
    .fixedSize(horizontal: false, vertical: true)
  }
}

@objc(PKSettingsForm)
final class SettingsFormFactory: NSObject {
  @objc static func makeView(clearChoices: @escaping () -> Void) -> NSView {
    NSHostingView(rootView: SettingsForm(clearChoices: clearChoices))
  }
}
