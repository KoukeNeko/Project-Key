// The host app: how to turn the keyboard on, and a field to try it in.
import SwiftUI

@main
struct ProjectKeyApp: App {
  var body: some Scene {
    WindowGroup { ContentView() }
  }
}

struct ContentView: View {
  @State private var text = ""

  var body: some View {
    NavigationStack {
      Form {
        Section("啟用鍵盤") {
          Text("設定 > 一般 > 鍵盤 > 鍵盤 > 新增鍵盤 > Project Key")
          Button("打開設定") {
            if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) }
          }
        }
        Section("試打") {
          TextField("輸入文字", text: $text, axis: .vertical)
            .lineLimit(3...8)
        }
      }
      .navigationTitle("Project Key")
    }
  }
}
