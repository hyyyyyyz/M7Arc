import AppKit
import Foundation

let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
let sizes = [16, 32, 128, 256, 512]
for size in sizes {
    for scale in [1, 2] {
        let pixels = size * scale
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
        let transform = AffineTransform(scale: CGFloat(pixels) / 1024)
        (transform as NSAffineTransform).concat()
        let tile = NSBezierPath(roundedRect: NSRect(x: 82, y: 82, width: 860, height: 860), xRadius: 194, yRadius: 194)
        let gradient = NSGradient(starting: NSColor(srgbRed: 0.22, green: 0.53, blue: 0.96, alpha: 1), ending: NSColor(srgbRed: 0.10, green: 0.27, blue: 0.65, alpha: 1))!
        gradient.draw(in: tile, angle: -78)
        NSColor.white.withAlphaComponent(0.18).setStroke()
        tile.lineWidth = 3
        tile.stroke()
        let box = NSBezierPath(roundedRect: NSRect(x: 238, y: 233, width: 548, height: 494), xRadius: 60, yRadius: 60)
        NSColor.white.withAlphaComponent(0.96).setStroke()
        box.lineWidth = 30
        box.stroke()
        let seam = NSBezierPath()
        seam.move(to: NSPoint(x: 239, y: 594))
        seam.line(to: NSPoint(x: 785, y: 594))
        seam.lineWidth = 25
        seam.stroke()
        let seven = NSBezierPath()
        seven.move(to: NSPoint(x: 420, y: 507))
        seven.line(to: NSPoint(x: 610, y: 507))
        seven.line(to: NSPoint(x: 470, y: 331))
        seven.lineWidth = 47
        seven.lineCapStyle = .round
        seven.lineJoinStyle = .round
        seven.stroke()
        NSColor(srgbRed: 0.68, green: 0.91, blue: 1.0, alpha: 1).setFill()
        NSBezierPath(roundedRect: NSRect(x: 472, y: 630, width: 80, height: 31), xRadius: 15, yRadius: 15).fill()
        NSGraphicsContext.restoreGraphicsState()
        let suffix = scale == 1 ? "" : "@2x"
        let path = output.appendingPathComponent("icon_\(size)x\(size)\(suffix).png")
        try bitmap.representation(using: .png, properties: [:])!.write(to: path)
    }
}
