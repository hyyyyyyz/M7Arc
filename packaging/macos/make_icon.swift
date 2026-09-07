import AppKit
import Foundation

let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

func color(_ hex: UInt32, alpha: CGFloat = 1) -> NSColor {
    NSColor(srgbRed: CGFloat((hex >> 16) & 255) / 255,
            green: CGFloat((hex >> 8) & 255) / 255,
            blue: CGFloat(hex & 255) / 255, alpha: alpha)
}

// The brand mark uses a 256-point SVG canvas with its origin at the top left.
func point(_ x: CGFloat, _ y: CGFloat) -> NSPoint {
    NSPoint(x: 128 + x * 3, y: 896 - y * 3)
}

func fill(_ path: NSBezierPath, colors: [NSColor], locations: [CGFloat],
          from: NSPoint, to: NSPoint) {
    NSGraphicsContext.saveGraphicsState()
    path.addClip()
    let gradient = NSGradient(colors: colors, atLocations: locations, colorSpace: .sRGB)!
    gradient.draw(from: from, to: to, options: [.drawsBeforeStartingLocation, .drawsAfterEndingLocation])
    NSGraphicsContext.restoreGraphicsState()
}

let stem = NSBezierPath()
stem.move(to: point(162, 73))
stem.line(to: point(232, 79))
stem.curve(to: point(226, 101), controlPoint1: point(235, 87), controlPoint2: point(232, 93))
stem.line(to: point(128, 224))
stem.curve(to: point(109, 233), controlPoint1: point(123, 230), controlPoint2: point(117, 233))
stem.line(to: point(55, 233))
stem.curve(to: point(47, 214), controlPoint1: point(43, 233), controlPoint2: point(39, 224))
stem.close()

let head = NSBezierPath()
head.move(to: point(54, 26))
head.line(to: point(188, 26))
head.curve(to: point(210, 38), controlPoint1: point(198, 26), controlPoint2: point(205, 30))
head.line(to: point(231, 73))
head.curve(to: point(231, 89), controlPoint1: point(234, 78), controlPoint2: point(235, 84))
head.curve(to: point(207, 80), controlPoint1: point(225, 82), controlPoint2: point(218, 80))
head.line(to: point(54, 80))
head.curve(to: point(54, 26), controlPoint1: point(18, 80), controlPoint2: point(18, 26))
head.close()

let fold = NSBezierPath()
fold.move(to: point(156, 81))
fold.line(to: point(229, 82))
fold.curve(to: point(230, 94), controlPoint1: point(233, 85), controlPoint2: point(233, 89))
fold.line(to: point(212, 118))
fold.line(to: point(127, 119))
fold.close()

let crease = NSBezierPath()
crease.move(to: point(157, 80))
crease.line(to: point(207, 80))
crease.curve(to: point(231, 89), controlPoint1: point(218, 80), controlPoint2: point(225, 82))
crease.lineWidth = 7.5
crease.lineCapStyle = .round

let silhouette = NSBezierPath()
silhouette.append(stem)
silhouette.append(head)

let sizes = [16, 32, 128, 256, 512]
for size in sizes {
    for scale in [1, 2] {
        let pixels = size * scale
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
        NSGraphicsContext.current!.imageInterpolation = .high
        let pixelScale = CGFloat(pixels) / 1024
        let transform = AffineTransform(scale: pixelScale)
        (transform as NSAffineTransform).concat()

        let tile = NSBezierPath(roundedRect: NSRect(x: 82, y: 82, width: 860, height: 860),
                                xRadius: 194, yRadius: 194)
        NSGraphicsContext.saveGraphicsState()
        let tileShadow = NSShadow()
        tileShadow.shadowColor = color(0x5E4127, alpha: 0.17)
        tileShadow.shadowBlurRadius = 22 * pixelScale
        tileShadow.shadowOffset = NSSize(width: 0, height: -10 * pixelScale)
        tileShadow.set()
        color(0xF7F1E6).setFill()
        tile.fill()
        NSGraphicsContext.restoreGraphicsState()
        fill(tile, colors: [color(0xFFFDF8), color(0xF7F1E6), color(0xEEE5D5)],
             locations: [0, 0.58, 1], from: NSPoint(x: 270, y: 940), to: NSPoint(x: 750, y: 82))
        color(0xFFFFFF, alpha: 0.75).setStroke()
        tile.lineWidth = 2
        tile.stroke()

        NSGraphicsContext.saveGraphicsState()
        let markShadow = NSShadow()
        markShadow.shadowColor = color(0xAA480C, alpha: 0.15)
        markShadow.shadowBlurRadius = 14 * pixelScale
        markShadow.shadowOffset = NSSize(width: 0, height: -10 * pixelScale)
        markShadow.set()
        color(0xFF7018).setFill()
        silhouette.fill()
        NSGraphicsContext.restoreGraphicsState()

        fill(stem, colors: [color(0xE64A08), color(0xFF7018), color(0xFF5F0F)],
             locations: [0, 0.38, 1], from: point(205, 77), to: point(88, 233))
        fill(fold, colors: [color(0xAD3305, alpha: 0.64), color(0xAD3305, alpha: 0)],
             locations: [0, 1], from: point(199, 81), to: point(183, 110))
        fill(head, colors: [color(0xFF8C32), color(0xFF6512)],
             locations: [0, 1], from: point(95, 26), to: point(158, 89))
        color(0xFFB66E).setStroke()
        crease.stroke()

        NSGraphicsContext.restoreGraphicsState()
        let suffix = scale == 1 ? "" : "@2x"
        let path = output.appendingPathComponent("icon_\(size)x\(size)\(suffix).png")
        try bitmap.representation(using: .png, properties: [:])!.write(to: path)
    }
}
