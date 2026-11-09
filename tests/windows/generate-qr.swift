import Foundation
import CoreImage
import AppKit

// Maintenance-only fixture generator; Windows tests use the checked-in PNGs.
let fixtures: [(String, Data)] = [
    ("qr-utf8.png", Data([0xe6, 0x97, 0xa5, 0xe6, 0x9c, 0xac, 0xe8, 0xaa, 0x9e,
                         0x20, 0x63, 0x61, 0x66, 0xc3, 0xa9])),
    ("qr-sjis.png", Data([0x93, 0xfa, 0x96, 0x7b, 0x8c, 0xea]))
]
let directory = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    .appendingPathComponent("resources")
try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
let context = CIContext()
for (name, bytes) in fixtures {
    let filter = CIFilter(name: "CIQRCodeGenerator")!
    filter.setValue(bytes, forKey: "inputMessage")
    filter.setValue("M", forKey: "inputCorrectionLevel")
    let image = filter.outputImage!.transformed(by: CGAffineTransform(scaleX: 8, y: 8))
    let bounds = image.extent.insetBy(dx: -32, dy: -32)
    let background = CIImage(color: CIColor.white).cropped(to: bounds)
    let bordered = image.composited(over: background)
    let bitmap = NSBitmapImageRep(cgImage: context.createCGImage(bordered, from: bounds)!)
    try bitmap.representation(using: .png, properties: [:])!.write(
        to: directory.appendingPathComponent(name))
}
