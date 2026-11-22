import AppKit

/// 16 色、黑描边的小图标，模仿 Win98 的像素图标观感
enum Icon {
    static func image(_ size: CGFloat, _ draw: @escaping (CGRect) -> Void) -> NSImage {
        let img = NSImage(size: NSSize(width: size, height: size), flipped: true) { rect in
            draw(rect)
            return true
        }
        img.isTemplate = false
        return img
    }

    static func folder(_ s: CGFloat = 16, open: Bool = false) -> NSImage {
        image(s) { r in
            let w = r.width, h = r.height
            let tabH = max(2, h * 0.2)
            let y0 = tabH
            // 黑色描边的整体外框
            let tab = NSBezierPath()
            tab.move(to: NSPoint(x: 0.5, y: y0))
            tab.line(to: NSPoint(x: 0.5, y: 0.5))
            tab.line(to: NSPoint(x: w * 0.40, y: 0.5))
            tab.line(to: NSPoint(x: w * 0.52, y: y0))
            tab.close()
            Theme.folderTop.setFill(); tab.fill()

            let body = NSRect(x: 0.5, y: y0, width: w - 1, height: h - y0 - 0.5)
            let bp = NSBezierPath(rect: body)
            Theme.folderBody.setFill(); bp.fill()

            Theme.dark.setStroke()
            tab.lineWidth = 1; tab.stroke()
            bp.lineWidth = 1; bp.stroke()

            // 底部一行深色，模拟像素阴影
            Theme.folderEdge.setFill()
            NSRect(x: 1, y: h - 2.5, width: w - 2, height: 1).fill()
            if open {
                Theme.white.withAlphaComponent(0.5).setFill()
                NSRect(x: 2, y: y0 + 2, width: w - 4, height: h - y0 - 5).fill()
            }
        }
    }

    static func document(_ s: CGFloat = 16) -> NSImage {
        image(s) { r in
            let w = r.width, h = r.height
            let fold = max(3, w * 0.32)
            let p = NSBezierPath()
            p.move(to: NSPoint(x: 1.5, y: 0.5))
            p.line(to: NSPoint(x: w - fold, y: 0.5))
            p.line(to: NSPoint(x: w - 0.5, y: fold))
            p.line(to: NSPoint(x: w - 0.5, y: h - 0.5))
            p.line(to: NSPoint(x: 1.5, y: h - 0.5))
            p.close()
            Theme.white.setFill(); p.fill()
            Theme.dark.setStroke(); p.lineWidth = 1; p.stroke()

            let c = NSBezierPath()
            c.move(to: NSPoint(x: w - fold, y: 0.5))
            c.line(to: NSPoint(x: w - fold, y: fold))
            c.line(to: NSPoint(x: w - 0.5, y: fold))
            Theme.shadow.setStroke(); c.lineWidth = 1; c.stroke()

            // 文本行
            Theme.shadow.setFill()
            var y = fold + 3
            while y < h - 3 {
                NSRect(x: 3.5, y: y, width: w - fold - 3, height: 1).fill()
                y += 3
            }
        }
    }

    /// 我的电脑：小显示器
    static func computer(_ s: CGFloat = 16) -> NSImage {
        image(s) { r in
            let body = NSRect(x: 1.5, y: 2.5, width: r.width - 3, height: r.height - 5)
            Theme.faceHi.setFill(); body.fill()
            Theme.dark.setStroke(); NSBezierPath(rect: body).stroke()
            Theme.navy.setFill()
            NSRect(x: body.minX + 1.5, y: body.minY + 1.5,
                   width: body.width - 3, height: body.height - 3).fill()
            Theme.dark.setFill()
            NSRect(x: body.minX + 3, y: body.minY + 3, width: 4, height: 1).fill()
            NSRect(x: body.minX + 3, y: body.minY + 5, width: 6, height: 1).fill()
            Theme.faceHi.setFill()
            NSRect(x: r.midX - 3, y: 0.5, width: 6, height: 2).fill()
            Theme.dark.setStroke()
            NSBezierPath(rect: NSRect(x: r.midX - 3, y: 0.5, width: 6, height: 2)).stroke()
        }
    }

    /// 工具栏“查看”按钮：四个小方块
    static func views(_ s: CGFloat = 16) -> NSImage {
        image(s) { r in
            let half = r.width / 2 - 1.5
            for (dx, dy) in [(0.5, 0.5), (r.width / 2 + 1, 0.5), (0.5, r.height / 2 + 1), (r.width / 2 + 1, r.height / 2 + 1)] {
                let box = NSRect(x: dx, y: dy, width: half, height: half)
                (dx < 1 ? Theme.select : Theme.folderBody).setFill()
                box.fill()
                Theme.dark.setStroke()
                NSBezierPath(rect: box).stroke()
            }
        }
    }

    /// 按扩展名给出不同类型的文件图标
    static func fileIcon(for url: URL) -> NSImage {
        page(badge: badge(for: url.pathExtension.lowercased()))
    }

    private static func badge(for ext: String) -> Badge {
        switch ext {
        case "png", "jpg", "jpeg", "gif", "webp", "heic", "heif", "tif", "tiff",
             "bmp", "svg", "ico", "raw", "psb":
            return .image
        case "pdf":
            return .pdf
        case "zip", "7z", "rar", "gz", "tar", "xz", "bz2", "tgz":
            return .archive
        case "dmg", "iso", "img", "toast", "sparseimage", "cdr":
            return .disk
        case "mp3", "wav", "aif", "aiff", "m4a", "flac", "ogg", "aac", "opus", "wma":
            return .audio
        case "mp4", "mov", "mkv", "avi", "m4v", "webm", "flv", "wmv":
            return .video
        case "doc", "docx", "rtf", "pages", "odt", "wps":
            return .doc
        case "xls", "xlsx", "csv", "tsv", "numbers", "ods":
            return .sheet
        case "ppt", "pptx", "key", "odp":
            return .slide
        case "txt", "md", "markdown", "log", "nfo", "tex", "me":
            return .text
        case "html", "htm", "xhtml", "webloc", "url", "mht":
            return .html
        case "js", "ts", "jsx", "tsx", "swift", "py", "rb", "go", "rs", "c", "h",
             "cpp", "hpp", "cc", "m", "mm", "java", "kt", "php", "sh", "zsh",
             "bash", "pl", "lua", "css", "scss", "less", "rsrc":
            return .code
        case "json", "xml", "plist", "yml", "yaml", "toml", "ini", "conf", "cfg", "env":
            return .data
        case "ttf", "otf", "woff", "woff2", "ttc":
            return .font
        case "app", "exe", "bin", "dylib", "so", "a", "command":
            return .program
        case "psd", "ai", "sketch", "fig", "xd", "indd", "epub":
            return .design
        default:
            return .plain
        }
    }

    private enum Badge {
        case image, pdf, archive, disk, audio, video
        case doc, sheet, slide, text, html, code, data, font, program, design, plain
    }

    private static let docBlue = NSColor(srgbRed: 0.13, green: 0.29, blue: 0.65, alpha: 1)
    private static let sheetGreen = NSColor(srgbRed: 0.10, green: 0.42, blue: 0.17, alpha: 1)
    private static let slideOrange = NSColor(srgbRed: 0.78, green: 0.40, blue: 0.05, alpha: 1)
    private static let codeTeal = NSColor(srgbRed: 0.05, green: 0.42, blue: 0.45, alpha: 1)
    private static let dataPurple = NSColor(srgbRed: 0.42, green: 0.18, blue: 0.58, alpha: 1)
    private static let designPink = NSColor(srgbRed: 0.70, green: 0.18, blue: 0.52, alpha: 1)

    /// 画一条 1px 线
    private static func line(_ x1: CGFloat, _ y1: CGFloat, _ x2: CGFloat, _ y2: CGFloat) {
        let p = NSBezierPath()
        p.move(to: NSPoint(x: x1, y: y1))
        p.line(to: NSPoint(x: x2, y: y2))
        p.lineWidth = 1
        p.stroke()
    }

    private static func page(badge: Badge) -> NSImage {
        image(16) { r in
            let w = r.width, h = r.height
            let fold: CGFloat = 5
            let outline = NSBezierPath()
            outline.move(to: NSPoint(x: 1.5, y: 0.5))
            outline.line(to: NSPoint(x: w - fold, y: 0.5))
            outline.line(to: NSPoint(x: w - 0.5, y: fold))
            outline.line(to: NSPoint(x: w - 0.5, y: h - 0.5))
            outline.line(to: NSPoint(x: 1.5, y: h - 0.5))
            outline.close()
            Theme.white.setFill(); outline.fill()
            Theme.dark.setStroke(); outline.lineWidth = 1; outline.stroke()
            let f = NSBezierPath()
            f.move(to: NSPoint(x: w - fold, y: 0.5))
            f.line(to: NSPoint(x: w - fold, y: fold))
            f.line(to: NSPoint(x: w - 0.5, y: fold))
            Theme.shadow.setStroke(); f.lineWidth = 1; f.stroke()

            let area = NSRect(x: 3, y: fold + 1, width: w - 8, height: h - fold - 4)
            switch badge {
            case .image:
                Theme.green.setFill()
                NSRect(x: area.minX, y: area.maxY - 4, width: area.width, height: 4).fill()
                (NSColor(srgbRed: 0.2, green: 0.4, blue: 0.85, alpha: 1)).setFill()
                NSRect(x: area.minX, y: area.minY, width: area.width, height: area.height - 4).fill()
                Theme.folderTop.setFill()
                NSRect(x: area.minX + 1, y: area.maxY - 6, width: 2, height: 2).fill()
            case .pdf:
                (NSColor(srgbRed: 0.75, green: 0.10, blue: 0.10, alpha: 1)).setFill()
                NSRect(x: area.minX, y: area.minY + area.height / 2 - 2, width: area.width, height: 4).fill()
                Theme.white.setFill()
                NSRect(x: area.minX + 1, y: area.minY + area.height / 2 - 1, width: area.width - 2, height: 1).fill()
            case .archive:
                Theme.folderBody.setFill()
                area.fill()
                Theme.dark.setStroke()
                NSBezierPath(rect: area).stroke()
                Theme.shadow.setFill()
                var y = area.minY + 1
                while y < area.maxY - 1 {
                    NSRect(x: area.midX - 1, y: y, width: 2, height: 1).fill()
                    y += 2
                }
            case .audio:
                Theme.dark.setFill()
                NSRect(x: area.midX, y: area.minY + 1, width: 1, height: area.height - 3).fill()
                NSRect(x: area.midX + 1, y: area.maxY - 3, width: 3, height: 2).fill()
                NSBezierPath(ovalIn: NSRect(x: area.midX - 2, y: area.minY, width: 3, height: 2.5)).fill()
            case .video:
                Theme.dark.setFill()
                NSRect(x: area.minX, y: area.minY, width: area.width, height: 4).fill()
                for i in 0..<3 {
                    NSRect(x: area.minX + CGFloat(i) * 3, y: area.minY + 1, width: 1, height: 2).fill()
                }
            case .disk:
                Theme.face.setFill()
                NSBezierPath(ovalIn: area.insetBy(dx: 0.5, dy: 1)).fill()
                Theme.dark.setStroke()
                NSBezierPath(ovalIn: area.insetBy(dx: 0.5, dy: 1)).stroke()
                Theme.shadow.setFill()
                NSRect(x: area.midX - 1, y: area.midY - 1, width: 2, height: 2).fill()
            case .doc:
                docBlue.setFill()
                NSRect(x: area.minX, y: area.minY, width: area.width, height: 3).fill()
                Theme.shadow.setStroke()
                for i in 0..<3 {
                    let y = area.minY + 4.5 + CGFloat(i) * 2
                    line(area.minX + 0.5, y, area.maxX - (i == 2 ? 3 : 0.5), y)
                }
            case .sheet:
                sheetGreen.setFill()
                area.fill()
                Theme.white.setFill()
                NSRect(x: area.minX, y: area.midY - 0.5, width: area.width, height: 1).fill()
                NSRect(x: area.midX - 0.5, y: area.minY, width: 1, height: area.height).fill()
                NSRect(x: area.minX, y: area.minY + 1.5, width: area.width, height: 1).fill()
            case .slide:
                slideOrange.setFill()
                area.fill()
                Theme.white.setFill()
                NSRect(x: area.minX + 1, y: area.minY + 1.5, width: area.width - 2, height: area.height - 3.5).fill()
                Theme.dark.setFill()
                NSRect(x: area.minX + 1, y: area.minY + 2.5, width: area.width - 2, height: 1).fill()
            case .text:
                Theme.shadow.setStroke()
                for i in 0..<4 {
                    let y = area.minY + 0.5 + CGFloat(i) * 1.8
                    line(area.minX + 0.5, y, area.maxX - (i % 2 == 1 ? 2 : 0.5), y)
                }
            case .html:
                (NSColor(srgbRed: 0.20, green: 0.35, blue: 0.80, alpha: 1)).setFill()
                area.fill()
                Theme.white.setStroke()
                line(area.midX - 1.5, area.midY - 2, area.midX - 3, area.midY)
                line(area.midX - 3, area.midY, area.midX - 1.5, area.midY + 2)
                line(area.midX + 1.5, area.midY - 2, area.midX + 3, area.midY)
                line(area.midX + 3, area.midY, area.midX + 1.5, area.midY + 2)
            case .code:
                codeTeal.setFill()
                area.fill()
                Theme.white.setStroke()
                line(area.midX - 1, area.midY - 2.5, area.midX - 2.5, area.midY)
                line(area.midX - 2.5, area.midY, area.midX - 1, area.midY + 2.5)
                line(area.midX + 1, area.midY - 2.5, area.midX + 2.5, area.midY)
                line(area.midX + 2.5, area.midY, area.midX + 1, area.midY + 2.5)
            case .data:
                dataPurple.setFill()
                area.fill()
                Theme.white.setStroke()
                line(area.midX - 2.5, area.midY - 2.5, area.midX - 3.5, area.midY - 1)
                line(area.midX - 3.5, area.midY - 1, area.midX - 3.5, area.midY + 1)
                line(area.midX - 3.5, area.midY + 1, area.midX - 2.5, area.midY + 2.5)
                line(area.midX + 2.5, area.midY - 2.5, area.midX + 3.5, area.midY - 1)
                line(area.midX + 3.5, area.midY - 1, area.midX + 3.5, area.midY + 1)
                line(area.midX + 3.5, area.midY + 1, area.midX + 2.5, area.midY + 2.5)
            case .font:
                Theme.dark.setFill()
                NSBezierPath(rect: NSRect(x: area.minX, y: area.minY, width: area.width, height: area.height)).stroke()
                let a = NSAttributedString(string: "A", attributes: [
                    .font: Theme.font(8, bold: true),
                    .foregroundColor: Theme.dark
                ])
                let sz = a.size()
                a.draw(at: NSPoint(x: area.midX - sz.width / 2, y: area.midY - sz.height / 2))
            case .program:
                Theme.shadow.setFill()
                let d = min(area.width, area.height) - 1
                let rect = NSRect(x: area.midX - d / 2, y: area.midY - d / 2, width: d, height: d)
                NSBezierPath(ovalIn: rect).fill()
                Theme.face.setFill()
                NSBezierPath(ovalIn: rect.insetBy(dx: 1.5, dy: 1.5)).fill()
                Theme.dark.setFill()
                for (dx, dy) in [(-1.0, -1.0), (1.0, -1.0), (-1.0, 1.0), (1.0, 1.0)] {
                    NSRect(x: area.midX + CGFloat(dx) * 2 - 0.5, y: area.midY + CGFloat(dy) * 2 - 0.5,
                           width: 1, height: 1).fill()
                }
            case .design:
                designPink.setFill()
                NSBezierPath(ovalIn: area.insetBy(dx: 0.5, dy: 0.5)).fill()
                Theme.white.setFill()
                NSRect(x: area.midX - 1, y: area.midY - 2, width: 2, height: 2).fill()
                NSRect(x: area.midX - 2, y: area.midY + 0.5, width: 3, height: 2).fill()
            case .plain:
                Theme.faceHi.setFill()
                NSRect(x: area.minX, y: area.maxY - 3, width: area.width, height: 3).fill()
            }
        }
    }

    /// 回收站：灰色垃圾桶 + 绿色回收三角
    static func trash(_ s: CGFloat = 16) -> NSImage {
        image(s) { r in
            let w = r.width, h = r.height
            // 提手
            let handle = NSRect(x: w * 0.35, y: 0.5, width: w * 0.30, height: 1.5)
            Theme.faceHi.setFill(); handle.fill()
            Theme.dark.setStroke()
            NSBezierPath(rect: handle).stroke()
            // 桶盖
            let lid = NSRect(x: 1, y: 2, width: w - 2, height: 2.5)
            Theme.faceHi.setFill(); lid.fill()
            NSBezierPath(rect: lid).stroke()
            // 桶身：上宽下窄
            let body = NSBezierPath()
            body.move(to: NSPoint(x: 2.2, y: 4.5))
            body.line(to: NSPoint(x: w - 2.2, y: 4.5))
            body.line(to: NSPoint(x: w - 3.4, y: h - 1))
            body.line(to: NSPoint(x: 3.4, y: h - 1))
            body.close()
            Theme.face.setFill(); body.fill()
            body.lineWidth = 1
            body.stroke()
            // 绿色回收三角
            Theme.green.setStroke()
            let tri = NSBezierPath()
            tri.move(to: NSPoint(x: w * 0.5, y: 6.5))
            tri.line(to: NSPoint(x: w * 0.68, y: 10.5))
            tri.line(to: NSPoint(x: w * 0.32, y: 10.5))
            tri.close()
            tri.lineWidth = 1
            tri.stroke()
        }
    }

    static func drive(_ s: CGFloat = 16) -> NSImage {
        image(s) { r in
            let w = r.width, h = r.height
            let b = NSRect(x: 0.5, y: h * 0.22, width: w - 1, height: h * 0.56)
            let p = NSBezierPath(rect: b)
            Theme.face.setFill(); p.fill()
            Theme.dark.setStroke(); p.lineWidth = 1; p.stroke()
            Theme.white.setFill()
            NSRect(x: b.minX + 1, y: b.minY + 1, width: b.width - 2, height: 1).fill()
            Theme.shadow.setFill()
            NSRect(x: b.minX + 1, y: b.minY + 2, width: b.width - 2, height: 2).fill()
            Theme.green.setFill()
            NSRect(x: b.minX + 2, y: b.maxY - 4, width: 2, height: 2).fill()
        }
    }

    static func arrowLeft(_ s: CGFloat = 20) -> NSImage { arrow(s, dir: .left) }
    static func arrowRight(_ s: CGFloat = 20) -> NSImage { arrow(s, dir: .right) }
    static func arrowUp(_ s: CGFloat = 20) -> NSImage { arrow(s, dir: .up) }

    private enum Dir { case left, right, up }

    private static func arrow(_ s: CGFloat, dir: Dir) -> NSImage {
        image(s) { r in
            let w = r.width, h = r.height
            let p = NSBezierPath()
            switch dir {
            case .left:
                p.move(to: NSPoint(x: 1, y: h / 2))
                p.line(to: NSPoint(x: w * 0.55, y: 2))
                p.line(to: NSPoint(x: w * 0.55, y: h * 0.34))
                p.line(to: NSPoint(x: w - 1, y: h * 0.34))
                p.line(to: NSPoint(x: w - 1, y: h * 0.66))
                p.line(to: NSPoint(x: w * 0.55, y: h * 0.66))
                p.line(to: NSPoint(x: w * 0.55, y: h - 2))
            case .right:
                p.move(to: NSPoint(x: w - 1, y: h / 2))
                p.line(to: NSPoint(x: w * 0.45, y: 2))
                p.line(to: NSPoint(x: w * 0.45, y: h * 0.34))
                p.line(to: NSPoint(x: 1, y: h * 0.34))
                p.line(to: NSPoint(x: 1, y: h * 0.66))
                p.line(to: NSPoint(x: w * 0.45, y: h * 0.66))
                p.line(to: NSPoint(x: w * 0.45, y: h - 2))
            case .up:
                p.move(to: NSPoint(x: w / 2, y: 1))
                p.line(to: NSPoint(x: w - 2, y: h * 0.5))
                p.line(to: NSPoint(x: w * 0.66, y: h * 0.5))
                p.line(to: NSPoint(x: w * 0.66, y: h - 1))
                p.line(to: NSPoint(x: w * 0.34, y: h - 1))
                p.line(to: NSPoint(x: w * 0.34, y: h * 0.5))
                p.line(to: NSPoint(x: 2, y: h * 0.5))
            }
            p.close()
            Theme.green.setFill(); p.fill()
            Theme.dark.setStroke(); p.lineWidth = 1; p.stroke()
        }
    }

    static func search(_ s: CGFloat = 16) -> NSImage {
        image(s) { r in
            let w = r.width, h = r.height
            let d = w * 0.55
            let ring = NSBezierPath(ovalIn: NSRect(x: 1, y: 1, width: d, height: d))
            Theme.white.setFill(); ring.fill()
            Theme.dark.setStroke(); ring.lineWidth = 1.4; ring.stroke()
            let handle = NSBezierPath()
            handle.lineWidth = 2
            handle.lineCapStyle = .square
            handle.move(to: NSPoint(x: d + 0.5, y: d + 0.5))
            handle.line(to: NSPoint(x: w - 1, y: h - 1))
            Theme.dark.setStroke(); handle.stroke()
        }
    }

    static func refresh(_ s: CGFloat = 16) -> NSImage {
        image(s) { r in
            let w = r.width, h = r.height
            let c = NSPoint(x: w / 2, y: h / 2)
            let rad = w * 0.33
            let arc = NSBezierPath()
            arc.appendArc(withCenter: c, radius: rad, startAngle: 50, endAngle: 330, clockwise: false)
            arc.lineWidth = 2
            Theme.green.setStroke(); arc.stroke()
            // 箭头
            let tip = NSPoint(x: c.x + rad * cos(CGFloat.pi * 50 / 180),
                              y: c.y - rad * sin(CGFloat.pi * 50 / 180))
            let t = NSBezierPath()
            t.move(to: NSPoint(x: tip.x - 1, y: tip.y - 5))
            t.line(to: NSPoint(x: tip.x + 3, y: tip.y + 1))
            t.line(to: NSPoint(x: tip.x - 3, y: tip.y + 3))
            t.close()
            Theme.dark.setFill(); t.fill()
        }
    }

    static func chevron(_ s: CGFloat = 7, expanded: Bool) -> NSImage {
        image(s) { r in
            let w = r.width, h = r.height
            let p = NSBezierPath()
            if expanded {
                p.move(to: NSPoint(x: 1, y: h * 0.3))
                p.line(to: NSPoint(x: w / 2, y: h * 0.7))
                p.line(to: NSPoint(x: w - 1, y: h * 0.3))
                p.line(to: NSPoint(x: w - 1, y: h * 0.3))
            } else {
                p.move(to: NSPoint(x: w * 0.3, y: 1))
                p.line(to: NSPoint(x: w * 0.7, y: h / 2))
                p.line(to: NSPoint(x: w * 0.3, y: h - 1))
            }
            p.lineWidth = 1.5
            p.lineJoinStyle = .miter
            Theme.dark.setStroke(); p.stroke()
        }
    }

    static func sortArrow(_ s: CGFloat = 8, up: Bool) -> NSImage {
        image(s) { r in
            let w = r.width, h = r.height
            let p = NSBezierPath()
            p.move(to: NSPoint(x: 1, y: up ? h - 2 : 2))
            p.line(to: NSPoint(x: w / 2, y: up ? 2 : h - 2))
            p.line(to: NSPoint(x: w - 1, y: up ? h - 2 : 2))
            p.close()
            Theme.dark.setFill(); p.fill()
        }
    }
}
