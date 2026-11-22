// 生成 Win98 风格 “我的电脑” 图标（16/32 两套像素图，最近邻放大），输出 AppIcon.icns
import AppKit

typealias P = SIMD4<UInt8>
let clear = P(0, 0, 0, 0)
let dark  = P(0, 0, 0, 255)
let face  = P(192, 192, 192, 255)   // #C0C0C0
let hi    = P(223, 223, 223, 255)   // #DFDFDF
let mid   = P(212, 208, 200, 255)   // #D4D0C8 机箱米色
let sh    = P(128, 128, 128, 255)   // #808080
let screen = P(0, 0, 128, 255)      // #000080 深蓝
let bar    = P(16, 132, 208, 255)   // #1084D0 标题栏蓝
let white  = P(255, 255, 255, 255)
let green  = P(0, 160, 0, 255)

struct Pix {
    var w: Int, h: Int
    var px: [P]
    init(_ w: Int, _ h: Int) { self.w = w; self.h = h; px = Array(repeating: clear, count: w * h) }
    mutating func rect(_ x: Int, _ y: Int, _ ww: Int, _ hh: Int, _ c: P) {
        guard ww > 0, hh > 0 else { return }
        for yy in max(0, y)..<min(h, y + hh) {
            for xx in max(0, x)..<min(w, x + ww) { px[yy * w + xx] = c }
        }
    }
    func scaled(_ n: Int) -> Pix {
        var out = Pix(n, n)
        for y in 0..<n { for x in 0..<n {
            let sx = min(w - 1, x * w / n), sy = min(h - 1, y * h / n)
            out.px[y * n + x] = px[sy * w + sx]
        } }
        return out
    }
}

// 32x32：显示器 + 底座
func art32() -> Pix {
    var p = Pix(32, 32)
    p.rect(2, 1, 28, 21, dark)          // 外框
    p.rect(3, 2, 26, 19, mid)           // 机身
    p.rect(3, 18, 26, 1, sh)            // 底部阴影
    p.rect(27, 3, 1, 15, sh)            // 右侧阴影
    p.rect(6, 5, 20, 13, dark)          // 屏幕外框
    p.rect(7, 6, 18, 11, screen)        // 屏幕
    p.rect(7, 6, 18, 2, bar)            // 标题栏
    p.rect(9, 9, 3, 1, white)           // 高光
    p.rect(9, 10, 1, 1, white)
    p.rect(24, 18, 1, 1, green)         // 电源灯
    p.rect(11, 22, 10, 1, dark)         // 支架上沿
    p.rect(12, 23, 8, 1, mid)
    p.rect(11, 24, 10, 1, dark)
    p.rect(3, 25, 26, 5, dark)          // 底座
    p.rect(4, 26, 24, 3, mid)
    p.rect(5, 29, 22, 1, sh)
    p.rect(7, 27, 8, 1, sh)             // 软驱口
    p.rect(25, 27, 1, 1, green)         // 指示灯
    return p
}

// 16x16：同一造型的简化版
func art16() -> Pix {
    var p = Pix(16, 16)
    p.rect(0, 0, 16, 11, dark)          // 显示器外框
    p.rect(1, 1, 14, 9, mid)            // 机身
    p.rect(1, 9, 14, 1, sh)             // 机身底部阴影
    p.rect(3, 2, 10, 7, dark)           // 屏幕框
    p.rect(4, 3, 8, 5, screen)          // 屏幕
    p.rect(4, 3, 8, 1, bar)             // 标题栏
    p.rect(1, 11, 14, 4, dark)          // 底座（紧贴显示器）
    p.rect(2, 12, 12, 2, mid)
    p.rect(3, 12, 4, 1, sh)             // 软驱口
    p.rect(12, 12, 1, 1, green)         // 指示灯
    return p
}

func png(_ p: Pix, _ url: URL) {
    guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: p.w, pixelsHigh: p.h,
                                     bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                     colorSpaceName: .deviceRGB, bytesPerRow: p.w * 4, bitsPerPixel: 32) else { fatalError() }
    let d = rep.bitmapData!
    for i in 0..<(p.w * p.h) {
        let c = p.px[i]
        d[i * 4 + 0] = c.x; d[i * 4 + 1] = c.y; d[i * 4 + 2] = c.z; d[i * 4 + 3] = c.w
    }
    try! rep.representation(using: .png, properties: [:])!.write(to: url)
}

let root = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
let iconset = root.appendingPathComponent("build/AppIcon.iconset")
try? FileManager.default.removeItem(at: iconset)
try! FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
let a32 = art32(), a16 = art16()
let jobs: [(String, Pix)] = [
    ("icon_16x16.png", a16),
    ("icon_16x16@2x.png", a32),
    ("icon_32x32.png", a32),
    ("icon_32x32@2x.png", a32.scaled(64)),
    ("icon_128x128.png", a32.scaled(128)),
    ("icon_128x128@2x.png", a32.scaled(256)),
    ("icon_256x256.png", a32.scaled(256)),
    ("icon_256x256@2x.png", a32.scaled(512)),
    ("icon_512x512.png", a32.scaled(512)),
    ("icon_512x512@2x.png", a32.scaled(1024)),
]
for (name, img) in jobs { png(img, iconset.appendingPathComponent(name)) }
let task = Process()
task.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
task.arguments = ["-c", "icns", iconset.path, "-o", root.appendingPathComponent("Resources/AppIcon.icns").path]
try! task.run(); task.waitUntilExit()
print("icns:", task.terminationStatus == 0 ? "ok" : "failed")
