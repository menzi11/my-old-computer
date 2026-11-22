import AppKit

/// Windows 98 经典配色：16 色灰阶 + 海军蓝。
/// 全部用 1px 硬线画立体边，不使用阴影、模糊或位图素材。
enum Theme {
    static let face    = NSColor(srgbRed: 0.753, green: 0.753, blue: 0.753, alpha: 1) // C0C0C0
    static let faceHi  = NSColor(srgbRed: 0.875, green: 0.875, blue: 0.875, alpha: 1) // DFDFDF
    static let white   = NSColor.white
    static let shadow  = NSColor(srgbRed: 0.502, green: 0.502, blue: 0.502, alpha: 1) // 808080
    static let dark    = NSColor.black
    static let navy    = NSColor(srgbRed: 0.000, green: 0.000, blue: 0.502, alpha: 1) // 000080
    static let navyHi  = NSColor(srgbRed: 0.063, green: 0.518, blue: 0.816, alpha: 1) // 1084D0
    static let text    = NSColor.black
    static let dim     = NSColor(srgbRed: 0.502, green: 0.502, blue: 0.502, alpha: 1)
    static let select  = NSColor(srgbRed: 0.000, green: 0.000, blue: 0.502, alpha: 1)
    static let green   = NSColor(srgbRed: 0.000, green: 0.502, blue: 0.000, alpha: 1)
    static let folderTop  = NSColor(srgbRed: 1.000, green: 0.980, blue: 0.400, alpha: 1)
    static let folderBody = NSColor(srgbRed: 0.980, green: 0.850, blue: 0.100, alpha: 1)
    static let folderEdge = NSColor(srgbRed: 0.600, green: 0.500, blue: 0.000, alpha: 1)

    /// MS Sans Serif 不可用，Tahoma 是最接近的系统字体
    static func font(_ size: CGFloat = 11, bold: Bool = false) -> NSFont {
        let name = bold ? "Tahoma-Bold" : "Tahoma"
        return NSFont(name: name, size: size)
            ?? (bold ? NSFont.boldSystemFont(ofSize: size) : NSFont.systemFont(ofSize: size))
    }
}

enum M {
    static let edge: CGFloat = 3
    static let titleH: CGFloat = 22
    static let menuH: CGFloat = 20
    static let toolH: CGFloat = 32
    static let addrH: CGFloat = 26
    static let statusH: CGFloat = 20
    static let sidebarW: CGFloat = 200
    static let rowH: CGFloat = 19
    static let headerH: CGFloat = 20
    static let scrollerW: CGFloat = 16
}

/// 三维边框：1px 白/深灰，凸起与凹陷两种
enum Bevel {
    static func raised(_ r: NSRect) {
        Theme.white.setFill()
        NSRect(x: r.minX, y: r.minY, width: r.width, height: 1).fill()
        NSRect(x: r.minX, y: r.minY, width: 1, height: r.height).fill()
        Theme.shadow.setFill()
        NSRect(x: r.minX, y: r.maxY - 1, width: r.width, height: 1).fill()
        NSRect(x: r.maxX - 1, y: r.minY, width: 1, height: r.height).fill()
    }

    static func sunken(_ r: NSRect) {
        Theme.shadow.setFill()
        NSRect(x: r.minX, y: r.minY, width: r.width, height: 1).fill()
        NSRect(x: r.minX, y: r.minY, width: 1, height: r.height).fill()
        Theme.white.setFill()
        NSRect(x: r.minX, y: r.maxY - 1, width: r.width, height: 1).fill()
        NSRect(x: r.maxX - 1, y: r.minY, width: 1, height: r.height).fill()
    }

    /// 输入框：外层深灰 + 内层黑的 2px 凹陷
    static func field(_ r: NSRect) {
        Theme.shadow.setFill()
        NSRect(x: r.minX, y: r.minY, width: r.width, height: 1).fill()
        NSRect(x: r.minX, y: r.minY, width: 1, height: r.height).fill()
        Theme.dark.setFill()
        NSRect(x: r.minX + 1, y: r.minY + 1, width: r.width - 1, height: 1).fill()
        NSRect(x: r.minX + 1, y: r.minY + 1, width: 1, height: r.height - 1).fill()
        Theme.white.setFill()
        NSRect(x: r.minX, y: r.maxY - 1, width: r.width, height: 1).fill()
        NSRect(x: r.maxX - 1, y: r.minY, width: 1, height: r.height).fill()
    }
}
