import AppKit
import QuickLookThumbnailing

// MARK: - 缩略图缓存（QuickLook）

final class ThumbnailCache {
    static let shared = ThumbnailCache()

    private var images: [URL: NSImage] = [:]
    private var inFlight: Set<URL> = []
    private var failed: Set<URL> = []
    private let lock = NSLock()

    static func isThumbnailable(_ url: URL) -> Bool {
        switch url.pathExtension.lowercased() {
        case "png", "jpg", "jpeg", "gif", "webp", "heic", "tif", "tiff", "bmp",
             "pdf", "mp4", "mov", "m4v", "mp3", "aiff", "m4a":
            return true
        default:
            return false
        }
    }

    func cached(_ url: URL) -> NSImage? {
        lock.lock(); defer { lock.unlock() }
        return images[url]
    }

    func request(_ url: URL, size: CGFloat, done: @escaping (URL) -> Void) {
        lock.lock()
        if images[url] != nil || inFlight.contains(url) || failed.contains(url) {
            lock.unlock(); return
        }
        inFlight.insert(url)
        lock.unlock()

        let req = QLThumbnailGenerator.Request(fileAt: url,
                                               size: CGSize(width: size, height: size),
                                               scale: 2,
                                               representationTypes: .thumbnail)
        QLThumbnailGenerator.shared.generateBestRepresentation(for: req) { [weak self] rep, _ in
            guard let self else { return }
            self.lock.lock()
            self.inFlight.remove(url)
            if let cg = rep?.cgImage {
                self.images[url] = NSImage(cgImage: cg, size: NSSize(width: size, height: size))
            } else {
                self.failed.insert(url)
            }
            self.lock.unlock()
            DispatchQueue.main.async { done(url) }
        }
    }
}

// MARK: - 图标 / 列表视图

final class IconGridView: NSView, NSDraggingSource {
    enum Mode { case big, list }

    var mode: Mode = .big { didSet { recompute(); needsDisplay = true } }
    var entries: [Entry] = [] {
        didSet {
            selected = selected.filter { url in entries.contains { $0.url == url } }
            recompute()
            needsDisplay = true
        }
    }
    var selected: Set<URL> = [] {
        didSet {
            if selected != oldValue {
                needsDisplay = true
                onSelectionChanged?()
            }
        }
    }

    var onOpen: ((Entry) -> Void)?
    var onOpenFirst: (() -> Void)?
    var onDelete: (() -> Void)?
    var onGoUp: (() -> Void)?
    var onPreview: (() -> Void)?
    var onRefresh: (() -> Void)?
    var onRename: (() -> Void)?
    var onSelectionChanged: (() -> Void)?
    var menuBuilder: (() -> NSMenu)?
    var onContextIndex: ((Int) -> Void)?
    var onDrop: (([URL], URL?) -> Bool)?

    private var cells: [NSRect] = []
    private(set) var contentHeight: CGFloat = 0
    private var lastLayoutWidth: CGFloat = -1
    private var dragStart: NSPoint?
    private var pressedIndex = -1
    private var dropIndex = -1
    private var dropTargetsFolder = false

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    private var cellW: CGFloat { mode == .big ? 94 : 220 }
    private var cellH: CGFloat { mode == .big ? 94 : 19 }

    override init(frame: NSRect) {
        super.init(frame: frame)
        registerForDraggedTypes([.fileURL])
    }
    required init?(coder: NSCoder) { fatalError() }

    func recompute() {
        let w = max(bounds.width, 1)
        let cols = max(1, Int((w - 8) / cellW))
        var rects: [NSRect] = []
        var x: CGFloat = 4
        var y: CGFloat = 4
        for i in 0..<entries.count {
            if i > 0 && i % cols == 0 { x = 4; y += cellH }
            rects.append(NSRect(x: x, y: y, width: cellW, height: cellH))
            x += cellW
        }
        cells = rects
        contentHeight = max(y + cellH + 4, 4)
        lastLayoutWidth = w
    }

    override func layout() {
        super.layout()
        if bounds.width != lastLayoutWidth { recompute(); needsDisplay = true }
    }

    // MARK: 绘制

    override func draw(_ dirtyRect: NSRect) {
        Theme.white.setFill()
        bounds.fill()
        for (i, r) in cells.enumerated() where r.intersects(dirtyRect) {
            drawItem(i, r)
        }
        if dropIndex >= 0, dropIndex < cells.count {
            let r = cells[dropIndex]
            Theme.select.setStroke()
            let p = NSBezierPath(rect: r.insetBy(dx: 1, dy: 1))
            p.lineWidth = 2
            p.stroke()
        }
    }

    private func drawItem(_ i: Int, _ r: NSRect) {
        let e = entries[i]
        let sel = selected.contains(e.url)

        if mode == .list {
            if sel {
                Theme.select.setFill()
                r.fill()
            } else if i == dropIndex && dropTargetsFolder {
                Theme.faceHi.setFill(); r.fill()
            }
            let iconRect = NSRect(x: r.minX + 2, y: r.minY + 2, width: 16, height: 16)
            let img = e.isDir ? Icon.folder(16) : Icon.fileIcon(for: e.url)
            img.draw(in: iconRect)
            drawLabel(e.name, in: NSRect(x: r.minX + 22, y: r.minY + 2,
                                         width: r.width - 26, height: 15),
                      color: sel ? .white : Theme.text, alignment: .left, wrap: false)
            return
        }

        // 大图标
        let iconBox = NSRect(x: r.midX - 24, y: r.minY + 4, width: 48, height: 48)
        if sel {
            Theme.faceHi.setFill()
            NSRect(x: r.minX + 6, y: r.minY + 2, width: r.width - 12, height: r.height - 4).fill()
        }
        if !e.isDir, ThumbnailCache.isThumbnailable(e.url) {
            if let thumb = ThumbnailCache.shared.cached(e.url) {
                thumb.draw(in: iconBox)
            } else {
                ThumbnailCache.shared.request(e.url, size: 64) { [weak self] _ in
                    self?.needsDisplay = true
                }
                Icon.fileIcon(for: e.url).draw(in: iconBox.insetBy(dx: 8, dy: 8))
            }
        } else if e.isDir {
            Icon.folder(48).draw(in: iconBox)
        } else {
            Icon.fileIcon(for: e.url).draw(in: iconBox.insetBy(dx: 8, dy: 8))
        }

        let labelRect = NSRect(x: r.minX + 2, y: r.minY + 54, width: r.width - 4, height: 36)
        if sel {
            Theme.select.setFill()
            labelRect.fill()
        }
        drawLabel(e.name, in: labelRect.insetBy(dx: 2, dy: 1),
                  color: sel ? .white : Theme.text, alignment: .center, wrap: true)
    }

    private func drawLabel(_ text: String, in rect: NSRect, color: NSColor,
                           alignment: NSTextAlignment, wrap: Bool) {
        let para = NSMutableParagraphStyle()
        para.alignment = alignment
        para.lineBreakMode = wrap ? .byCharWrapping : .byTruncatingTail
        let attrs: [NSAttributedString.Key: Any] = [
            .font: Theme.font(11), .foregroundColor: color, .paragraphStyle: para
        ]
        (text as NSString).draw(in: rect, withAttributes: attrs)
    }

    // MARK: 命中与选择

    private func index(at p: NSPoint) -> Int {
        cells.firstIndex { $0.contains(p) } ?? -1
    }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        let p = convert(event.locationInWindow, from: nil)
        let i = index(at: p)
        pressedIndex = i
        dragStart = p

        if i < 0 {
            if !event.modifierFlags.contains(.command) { selected = [] }
            return
        }
        let e = entries[i]
        if event.modifierFlags.contains(.command) {
            if selected.contains(e.url) { selected.remove(e.url) } else { selected.insert(e.url) }
        } else if !selected.contains(e.url) {
            selected = [e.url]
        }
        if event.clickCount == 2 { onOpen?(e) }
    }

    override func mouseDragged(with event: NSEvent) {
        guard let start = dragStart, pressedIndex >= 0, pressedIndex < entries.count else { return }
        let p = convert(event.locationInWindow, from: nil)
        guard hypot(p.x - start.x, p.y - start.y) > 4 else { return }

        let urls = selected.isEmpty ? [entries[pressedIndex].url] : entries.filter { selected.contains($0.url) }.map { $0.url }
        var items: [NSDraggingItem] = []
        for url in urls {
            let item = NSPasteboardItem()
            item.setString(url.absoluteString, forType: .fileURL)
            let di = NSDraggingItem(pasteboardWriter: item)
            let img = entries.first(where: { $0.url == url }).map { $0.isDir ? Icon.folder(32) : Icon.fileIcon(for: $0.url) } ?? Icon.document(32)
            di.setDraggingFrame(NSRect(x: p.x - 16, y: p.y - 16, width: 32, height: 32), contents: img)
            items.append(di)
        }
        beginDraggingSession(with: items, event: event, source: self)
        dragStart = nil
    }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 36, 76: onOpenFirst?()
        case 51: onGoUp?()            // 退格 = 上一级
        case 49: onPreview?()         // 空格 = 快速预览
        case 96: onRefresh?()         // F5 = 刷新
        case 120: onRename?()         // F2 = 重命名
        case 117: onDelete?()
        case 126 where event.modifierFlags.contains(.command): onGoUp?()
        default: super.keyDown(with: event)
        }
    }

    override func mouseUp(with event: NSEvent) {
        dragStart = nil
        pressedIndex = -1
    }

    override func rightMouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let i = index(at: p)
        if i >= 0, !selected.contains(entries[i].url) {
            selected = [entries[i].url]
        } else if i < 0 {
            selected = []
        }
        onContextIndex?(i)
        if let menu = menuBuilder?() {
            NSMenu.popUpContextMenu(menu, with: event, for: self)
        }
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func menu(for event: NSEvent) -> NSMenu? {
        let p = convert(event.locationInWindow, from: nil)
        let i = index(at: p)
        if i >= 0, !selected.contains(entries[i].url) {
            selected = [entries[i].url]
        } else if i < 0 {
            selected = []
        }
        onContextIndex?(i)
        return menuBuilder?()
    }

    // MARK: 拖出

    func draggingSession(_ session: NSDraggingSession,
                         sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation {
        .every
    }

    // MARK: 拖入

    private func draggedURLs(_ info: NSDraggingInfo) -> [URL] {
        let urls = info.draggingPasteboard.readObjects(forClasses: [NSURL.self],
                                                       options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        return urls
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        updateDropTarget(sender)
        return .copy
    }

    override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        updateDropTarget(sender)
        return .copy
    }

    override func draggingExited(_ sender: NSDraggingInfo?) {
        dropIndex = -1
        needsDisplay = true
    }

    private func updateDropTarget(_ info: NSDraggingInfo) {
        let p = convert(info.draggingLocation, from: nil)
        let i = index(at: p)
        if i != dropIndex {
            dropIndex = i
            needsDisplay = true
        }
        dropTargetsFolder = i >= 0 && entries[i].isDir
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        let urls = draggedURLs(sender)
        guard !urls.isEmpty else { return false }
        let i = index(at: convert(sender.draggingLocation, from: nil))
        let target = (i >= 0 && entries[i].isDir) ? entries[i].url : nil
        dropIndex = -1
        needsDisplay = true
        return onDrop?(urls, target) ?? false
    }
}

// MARK: - 视图容器（滚动条外壳）

final class GridContainer: NSView {
    let grid = IconGridView()
    private let scroll = NSScrollView()
    private let vbar = Win98ScrollBar()

    override var isFlipped: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        scroll.documentView = grid
        scroll.hasVerticalScroller = false
        scroll.hasHorizontalScroller = false
        scroll.autohidesScrollers = false
        scroll.drawsBackground = true
        scroll.backgroundColor = .white
        scroll.borderType = .noBorder
        addSubview(scroll)
        vbar.scrollView = scroll
        addSubview(vbar)
    }
    required init?(coder: NSCoder) { fatalError() }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()
        let area = NSRect(x: 1, y: 1, width: max(0, bounds.width - 2 - M.scrollerW - 2),
                          height: max(0, bounds.height - 3))
        Theme.white.setFill(); area.fill()
        Bevel.sunken(area)
    }

    override func layout() {
        super.layout()
        let barW = M.scrollerW
        let viewport = NSRect(x: 2, y: 2, width: max(0, bounds.width - 4 - barW), height: max(0, bounds.height - 4))
        scroll.frame = viewport
        vbar.frame = NSRect(x: bounds.width - 2 - barW, y: 2, width: barW, height: max(0, bounds.height - 4))

        grid.setFrameSize(NSSize(width: viewport.width, height: grid.contentHeight))
        if grid.contentHeight < viewport.height {
            grid.setFrameSize(NSSize(width: viewport.width, height: viewport.height))
        }
        vbar.refresh()
    }

    func reload() {
        grid.recompute()
        needsLayout = true
        layoutSubtreeIfNeeded()
        grid.needsDisplay = true
        vbar.refresh()
    }
}
