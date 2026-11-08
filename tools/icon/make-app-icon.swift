// Draws the macOS app icon from the input method icon make-icons.ps1 draws (its fill color and ㄅ
// outline in platforms/linux/fcitx-projectkey.svg), on the macOS icon grid: an 824-point rounded
// square centered in 1024 with the system's shadow. Run on a Mac; commit the result.
//   swift tools/icon/make-app-icon.swift   writes platforms/macos/Resources/AppIcon.icns
import AppKit

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../..").standardized
let svg = try String(contentsOf: root.appendingPathComponent("platforms/linux/fcitx-projectkey.svg"), encoding: .utf8)

func attribute(_ name: String) -> String {
  let range = svg.range(of: name + "=\"([^\"]*)\"", options: .regularExpression)!
  return String(svg[range].dropFirst(name.count + 2).dropLast())
}

let hex = Int(attribute("fill").dropFirst(), radix: 16)!
let fill = NSColor(srgbRed: CGFloat(hex >> 16 & 0xff) / 255, green: CGFloat(hex >> 8 & 0xff) / 255, blue: CGFloat(hex & 0xff) / 255, alpha: 1)

// SVG path data of M, L, C and Z commands in the SVG's 64-unit box, flipped to AppKit's y-up.
func glyph(in rect: CGRect) -> NSBezierPath {
  let path = NSBezierPath()
  let scale = rect.width / 64
  let scanner = Scanner(string: attribute("d"))
  scanner.charactersToBeSkipped = CharacterSet(charactersIn: " ,")
  func point() -> CGPoint {
    let x = scanner.scanDouble()!, y = scanner.scanDouble()!
    return CGPoint(x: rect.minX + x * scale, y: rect.maxY - y * scale)
  }
  var command = "M"
  while !scanner.isAtEnd {
    if let next = scanner.scanCharacters(from: CharacterSet(charactersIn: "MLCZ")) {
      if next.hasSuffix("Z") { path.close() }
      command = String(next.last!)
      continue
    }
    switch command {
    case "M": path.move(to: point())
    case "L": path.line(to: point())
    default:
      let c1 = point(), c2 = point()
      path.curve(to: point(), controlPoint1: c1, controlPoint2: c2)
    }
  }
  return path
}

func draw(_ pixels: Int) -> Data {
  let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels, bitsPerSample: 8, samplesPerPixel: 4,
                             hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
  NSGraphicsContext.saveGraphicsState()
  NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
  let unit = CGFloat(pixels) / 1024
  let square = CGRect(x: 100 * unit, y: 100 * unit, width: 824 * unit, height: 824 * unit)
  let shape = NSBezierPath(roundedRect: square, xRadius: 185 * unit, yRadius: 185 * unit)
  NSGraphicsContext.saveGraphicsState()
  let shadow = NSShadow()
  shadow.shadowColor = NSColor.black.withAlphaComponent(0.3)
  shadow.shadowOffset = NSSize(width: 0, height: -10 * unit)
  shadow.shadowBlurRadius = 20 * unit
  shadow.set()
  fill.setFill()
  shape.fill()
  NSGraphicsContext.restoreGraphicsState()
  NSColor.white.setFill()
  glyph(in: square).fill()
  NSGraphicsContext.restoreGraphicsState()
  return rep.representation(using: .png, properties: [:])!
}

let iconset = FileManager.default.temporaryDirectory.appendingPathComponent("AppIcon.iconset")
try? FileManager.default.removeItem(at: iconset)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for size in [16, 32, 128, 256, 512] {
  try draw(size).write(to: iconset.appendingPathComponent("icon_\(size)x\(size).png"))
  try draw(size * 2).write(to: iconset.appendingPathComponent("icon_\(size)x\(size)@2x.png"))
}
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", root.appendingPathComponent("platforms/macos/Resources/AppIcon.icns").path]
try iconutil.run()
iconutil.waitUntilExit()
exit(iconutil.terminationStatus)
