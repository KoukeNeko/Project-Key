// The window after installing: adds Project Key to the input sources, waits for the user to
// switch to it, and offers a box to try it in. It opens at launch while the input source is not
// enabled, so it never comes back once it has been added.
import AppKit
import Carbon
import SwiftUI

// Whether Project Key is an enabled input source and the one in use, as the system reports it.
@MainActor
final class InputSourceState: ObservableObject {
  @Published private(set) var enabled = false
  @Published private(set) var selected = false
  private let bundleID = PKInputMethodBundleID

  init() {
    refresh()
    for name in [kTISNotifyEnabledKeyboardInputSourcesChanged!, kTISNotifySelectedKeyboardInputSourceChanged!] {
      DistributedNotificationCenter.default().addObserver(forName: Notification.Name(name as String), object: nil, queue: .main) { [weak self] _ in
        MainActor.assumeIsolated { self?.refresh() }
      }
    }
  }

  // A fixed state, for `ProjectKey --preview`.
  init(enabled: Bool, selected: Bool) {
    self.enabled = enabled
    self.selected = selected
  }

  // The input method and its Bopomofo input mode.
  private var sources: [TISInputSource] {
    let filter = [kTISPropertyBundleID as String: bundleID] as CFDictionary
    return TISCreateInputSourceList(filter, true)?.takeRetainedValue() as? [TISInputSource] ?? []
  }

  func refresh() {
    enabled = sources.contains { Property(of: $0, kTISPropertyInputSourceIsEnabled) == true }
    selected = Property(of: TISCopyCurrentKeyboardInputSource().takeRetainedValue(), kTISPropertyBundleID) == bundleID
  }

  func enable() {
    for source in sources { TISEnableInputSource(source) }
    refresh()
  }
}

private func Property<T>(of source: TISInputSource, _ key: CFString) -> T? {
  guard let raw = TISGetInputSourceProperty(source, key) else { return nil }
  return Unmanaged<AnyObject>.fromOpaque(raw).takeUnretainedValue() as? T
}

struct WelcomeForm: View {
  @ObservedObject var inputSource: InputSourceState
  @State var tried = ""
  @FocusState private var trying: Bool
  let showSettings: () -> Void
  let close: () -> Void

  private enum Step: Int { case add, switchTo, tryIt, done }

  private var step: Step {
    if !inputSource.enabled { return .add }
    if !inputSource.selected && tried.isEmpty { return .switchTo }
    return tried.isEmpty ? .tryIt : .done
  }

  var body: some View {
    VStack(alignment: .leading, spacing: 20) {
      HStack(spacing: 12) {
        Logo().frame(width: 48, height: 48)
        Text("Project Key").font(.system(size: 22, weight: .semibold))
      }

      VStack(spacing: 0) {
        StepRow(mark: mark(.add), title: "加入輸入方式") {
          if step == .add {
            Button("加入") { withAnimation(.easeOut(duration: 0.2)) { inputSource.enable() } }
              .buttonStyle(.borderedProminent)
              .keyboardShortcut(.defaultAction)
          } else {
            Text("已加入").foregroundStyle(.secondary)
          }
        }
        Divider().padding(.leading, 46)
        StepRow(mark: mark(.switchTo), title: "切換到 Project Key") {
          if step.rawValue > Step.switchTo.rawValue {
            Text("使用中").foregroundStyle(.secondary)
          } else {
            HStack(spacing: 4) { Keycap("⌃"); Keycap("Space") }
              .opacity(step == .switchTo ? 1 : 0.55)
          }
        }
        Divider().padding(.leading, 46)
        VStack(alignment: .leading, spacing: 10) {
          StepRow(mark: mark(.tryIt), title: "試打") { EmptyView() }
            .padding(.bottom, -14)
          HStack(spacing: 14) {
            Hint(key: "Space", label: "選字")
            Hint(key: "Enter", label: "送出")
            Hint(key: "⇧", label: "單按切換中英")
          }
          .padding(.leading, 46)
          TextEditor(text: $tried)
            .font(.system(size: 15))
            .scrollContentBackground(.hidden)
            .padding(6)
            .background(RoundedRectangle(cornerRadius: 7).fill(Color(nsColor: .textBackgroundColor)))
            .overlay(RoundedRectangle(cornerRadius: 7)
              .strokeBorder(trying ? Color.accentColor : Color(nsColor: .separatorColor), lineWidth: trying ? 2 : 1))
            .frame(height: 76)
            .focused($trying)
            .padding(.leading, 46)
            .disabled(step == .add)
        }
        .opacity(step.rawValue >= Step.tryIt.rawValue ? 1 : 0.5)
        .padding(.trailing, 14)
        .padding(.bottom, 14)
      }
      .background(RoundedRectangle(cornerRadius: 10).fill(Color.primary.opacity(0.04)))
      .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(Color(nsColor: .separatorColor).opacity(0.6)))

      HStack {
        Button("設定…", action: showSettings).buttonStyle(.link)
        Spacer()
        if step == .done {
          Button("完成", action: close).buttonStyle(.borderedProminent).keyboardShortcut(.defaultAction)
        } else {
          Button("完成", action: close).buttonStyle(.bordered)
        }
      }
    }
    .padding(24)
    .frame(width: 480)
    .animation(.easeOut(duration: 0.2), value: step)
    .onChange(of: step) { step in
      if step == .tryIt { trying = true }  // the switch landed: type here next
    }
  }

  // 0 not yet, 1 the step to do now, 2 done.
  private func mark(_ row: Step) -> Int {
    step.rawValue > row.rawValue ? 2 : step == row ? 1 : 0
  }
}

// The input method's icon, from the SVG tools/icon/make-icons.ps1 draws (build.sh copies it in):
// its fill color and the ㄅ outline, on corners a fifth of its size.
private struct Logo: View {
  private static let svg = Bundle.main.url(forResource: "fcitx-projectkey", withExtension: "svg")
    .flatMap { try? String(contentsOf: $0, encoding: .utf8) } ?? ""

  private static func attribute(_ name: String) -> String {
    guard let range = svg.range(of: name + "=\"([^\"]*)\"", options: .regularExpression) else { return "" }
    return String(svg[range].dropFirst(name.count + 2).dropLast())
  }

  private static let fill: Color = {
    let hex = Int(attribute("fill").dropFirst(), radix: 16) ?? 0
    return Color(red: Double(hex >> 16 & 0xff) / 255, green: Double(hex >> 8 & 0xff) / 255, blue: Double(hex & 0xff) / 255)
  }()

  var body: some View {
    GeometryReader { box in
      RoundedRectangle(cornerRadius: box.size.width / 5)
        .fill(Logo.fill)
        .overlay(Glyph(outline: Logo.attribute("d")).fill(.white))
    }
  }

  // SVG path data of M, L, C and Z commands in the SVG's 64-unit box.
  private struct Glyph: Shape {
    let outline: String

    func path(in rect: CGRect) -> Path {
      var path = Path()
      let scale = rect.width / 64
      let scanner = Scanner(string: outline)
      scanner.charactersToBeSkipped = CharacterSet(charactersIn: " ,")
      func point() -> CGPoint? {
        guard let x = scanner.scanDouble(), let y = scanner.scanDouble() else { return nil }
        return CGPoint(x: x * scale, y: y * scale)
      }
      var command = "M"
      while !scanner.isAtEnd {
        if let next = scanner.scanCharacters(from: CharacterSet(charactersIn: "MLCZ")) {
          if next.hasSuffix("Z") { path.closeSubpath() }
          command = String(next.last!)
          continue
        }
        switch command {
        case "M": guard let p = point() else { return path }; path.move(to: p)
        case "L": guard let p = point() else { return path }; path.addLine(to: p)
        case "C":
          guard let c1 = point(), let c2 = point(), let p = point() else { return path }
          path.addCurve(to: p, control1: c1, control2: c2)
        default: return path
        }
      }
      return path
    }
  }
}

private struct StepRow<Trailing: View>: View {
  let mark: Int
  let title: String
  @ViewBuilder let trailing: () -> Trailing

  var body: some View {
    HStack(spacing: 12) {
      ZStack {
        switch mark {
        case 2:
          Image(systemName: "checkmark.circle.fill")
            .font(.system(size: 18))
            .foregroundStyle(.white, .green)
            .transition(.scale(scale: 0.6).combined(with: .opacity))
        case 1:
          Circle().strokeBorder(Color.accentColor, lineWidth: 2).frame(width: 18, height: 18)
          Circle().fill(Color.accentColor).frame(width: 8, height: 8)
        default:
          Circle().strokeBorder(Color.secondary.opacity(0.5), lineWidth: 1.5).frame(width: 18, height: 18)
        }
      }
      .frame(width: 20, height: 20)
      Text(title).font(.system(size: 14)).foregroundStyle(mark == 0 ? .secondary : .primary)
      Spacer()
      trailing()
    }
    .padding(.horizontal, 14)
    .frame(minHeight: 50)
  }
}

private struct Keycap: View {
  let label: String
  init(_ label: String) { self.label = label }

  var body: some View {
    Text(label)
      .font(.system(size: 11, weight: .medium, design: .rounded))
      .padding(.horizontal, 6)
      .frame(minWidth: 22, minHeight: 20)
      .background(RoundedRectangle(cornerRadius: 5).fill(Color(nsColor: .controlBackgroundColor)))
      .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(Color(nsColor: .separatorColor)))
  }
}

private struct Hint: View {
  let key: String
  let label: String

  var body: some View {
    HStack(spacing: 5) {
      Keycap(key)
      Text(label).font(.system(size: 12)).foregroundStyle(.secondary)
    }
  }
}

@objc(PKWelcome)
final class WelcomeWindow: NSObject {
  @MainActor private static var window: NSWindow?

  // After installing (Homebrew opens the app), while Project Key is not an enabled input source.
  @MainActor @objc static func showIfNeeded() {
    if !InputSourceState().enabled { show() }
  }

  // From the settings window's 關於, whatever the state.
  @MainActor @objc static func show() {
    if let window {
      NSApp.activate(ignoringOtherApps: true)
      window.makeKeyAndOrderFront(nil)
      return
    }
    let inputSource = InputSourceState()
    let window = NSWindow(contentRect: .zero, styleMask: [.titled, .closable, .fullSizeContentView], backing: .buffered, defer: true)
    window.titlebarAppearsTransparent = true
    window.titleVisibility = .hidden
    window.title = "Project Key"
    window.isReleasedWhenClosed = false
    window.contentView = NSHostingView(rootView: WelcomeForm(inputSource: inputSource, showSettings: { PKShowSettings() },
                                                            close: { WelcomeWindow.window?.close(); WelcomeWindow.window = nil }))
    window.setContentSize(window.contentView!.fittingSize)
    window.center()
    self.window = window
    // The input method is a background app; it has to come forward for the window to take keys.
    NSApp.activate(ignoringOtherApps: true)
    window.makeKeyAndOrderFront(nil)
  }

  // The window's states as PNGs, for `ProjectKey --preview`.
  @MainActor @objc static func preview(into dir: String) {
    let states: [(String, Bool, Bool, String)] = [
      ("welcome-add", false, false, ""), ("welcome-switch", true, false, ""),
      ("welcome-try", true, true, ""), ("welcome-done", true, true, "我們把舊的程式重構成模組"),
    ]
    for (name, enabled, selected, tried) in states {
      let form = WelcomeForm(inputSource: InputSourceState(enabled: enabled, selected: selected), tried: tried, showSettings: {}, close: {})
      let view = NSHostingView(rootView: form.background(Color(nsColor: .windowBackgroundColor)).environment(\.controlActiveState, .key))
      let window = NSWindow(contentRect: .zero, styleMask: .borderless, backing: .buffered, defer: false)
      window.contentView = view
      window.setContentSize(view.fittingSize)
      view.layoutSubtreeIfNeeded()
      RunLoop.current.run(until: Date().addingTimeInterval(0.3))
      guard let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { continue }
      view.cacheDisplay(in: view.bounds, to: rep)
      try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: dir).appendingPathComponent(name + ".png"))
    }
  }
}
