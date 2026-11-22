import AppKit

// MARK: - 窗口

final class XPWindow: NSWindow {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { true }

    init(contentRect: NSRect) {
        super.init(contentRect: contentRect, styleMask: [.borderless, .resizable],
                   backing: .buffered, defer: false)
        isOpaque = true
        backgroundColor = Theme.face
        hasShadow = false          // Win98 没有现代投影
        minSize = NSSize(width: 700, height: 440)
        isReleasedWhenClosed = false
    }
}

// MARK: - 根视图

final class RootView: NSView {
    var titleBar: TitleBarView!
    var menuBar: MenuBarView!
    var toolBar: ToolbarView!
    var addrBar: AddressBarView!
    var sidebar: TreePaneView!
    var listView: FileListView!
    var gridView: GridContainer!
    var statusBar: StatusBarView!

    /// 当前视图模式（详细信息 / 大图标 / 列表）
    var mode: ViewMode = .details

    /// 侧栏宽度（可拖拽调节）
    var sidebarWidth: CGFloat = 200 { didSet { needsLayout = true } }
    let splitter = SplitterView()

    var leftEdge: ResizeEdge!
    var rightEdge: ResizeEdge!
    var bottomEdge: ResizeEdge!

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()
        // 1px 黑外框 + 1px 立体边，Win98 窗口的典型轮廓
        let border = NSRect(x: 0.5, y: 0.5, width: bounds.width - 1, height: bounds.height - 1)
        Theme.dark.setStroke()
        let p = NSBezierPath(rect: border)
        p.lineWidth = 1
        p.stroke()
        Bevel.raised(NSRect(x: 1.5, y: 1.5, width: bounds.width - 3, height: bounds.height - 3))
    }

    override func layout() {
        super.layout()
        let e = M.edge
        let w = bounds.width - e * 2
        let h = bounds.height
        titleBar.frame = NSRect(x: e, y: e, width: w, height: M.titleH)
        menuBar.frame = NSRect(x: e, y: e + M.titleH, width: w, height: M.menuH)
        let toolY = e + M.titleH + M.menuH
        toolBar.frame = NSRect(x: e, y: toolY, width: w, height: M.toolH)
        let addrY = toolY + M.toolH
        addrBar.frame = NSRect(x: e, y: addrY, width: w, height: M.addrH)
        let mainY = addrY + M.addrH
        let mainH = max(0, h - mainY - M.statusH - e)
        let contentRect: NSRect
        if sidebar.isHidden {
            splitter.isHidden = true
            contentRect = NSRect(x: e, y: mainY, width: w, height: mainH)
        } else {
            splitter.isHidden = false
            let sw = min(max(120, sidebarWidth), max(120, w - 260))
            sidebar.frame = NSRect(x: e, y: mainY, width: sw, height: mainH)
            splitter.frame = NSRect(x: e + sw, y: mainY, width: 5, height: mainH)
            contentRect = NSRect(x: e + sw + 5, y: mainY, width: w - sw - 5, height: mainH)
        }
        listView.frame = contentRect
        gridView.frame = contentRect
        statusBar.frame = NSRect(x: e, y: h - e - M.statusH, width: w, height: M.statusH)

        leftEdge.frame = NSRect(x: 0, y: e, width: 3, height: max(0, h - e - 3))
        rightEdge.frame = NSRect(x: bounds.width - 3, y: e, width: 3, height: max(0, h - e - 3))
        bottomEdge.frame = NSRect(x: 0, y: h - 3, width: bounds.width, height: 3)
    }
}

// MARK: - 侧栏分隔条

final class SplitterView: NSView {
    var currentWidth: (() -> CGFloat)?
    var onResize: ((CGFloat) -> Void)?
    var onEnd: ((CGFloat) -> Void)?
    private var startWidth: CGFloat = 0
    private var startX: CGFloat = 0

    override var isFlipped: Bool { true }

    override func resetCursorRects() {
        addCursorRect(bounds, cursor: .resizeLeftRight)
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()
        Theme.shadow.setFill()
        NSRect(x: 1, y: 0, width: 1, height: bounds.height).fill()
        Theme.white.setFill()
        NSRect(x: 2, y: 0, width: 1, height: bounds.height).fill()
    }

    override func mouseDown(with event: NSEvent) {
        startWidth = currentWidth?() ?? 200
        startX = event.locationInWindow.x
    }

    override func mouseDragged(with event: NSEvent) {
        let w = startWidth + (event.locationInWindow.x - startX)
        onResize?(w)
    }

    override func mouseUp(with event: NSEvent) {
        onEnd?(startWidth + (event.locationInWindow.x - startX))
    }
}

// MARK: - 边缘拖拽

final class ResizeEdge: NSView {
    enum Side { case left, right, bottom }
    let side: Side
    private var startFrame: NSRect = .zero
    private var startMouse: NSPoint = .zero

    init(side: Side) {
        self.side = side
        super.init(frame: .zero)
    }
    required init?(coder: NSCoder) { fatalError() }

    override var isFlipped: Bool { true }

    override func resetCursorRects() {
        addCursorRect(bounds, cursor: side == .bottom ? .resizeUpDown : .resizeLeftRight)
    }

    override func mouseDown(with event: NSEvent) {
        guard let win = window else { return }
        startFrame = win.frame
        startMouse = NSEvent.mouseLocation
    }

    override func mouseDragged(with event: NSEvent) {
        guard let win = window else { return }
        let p = NSEvent.mouseLocation
        let dx = p.x - startMouse.x
        let dy = p.y - startMouse.y
        var f = startFrame
        switch side {
        case .left:
            f.size.width = max(win.minSize.width, startFrame.width - dx)
            f.origin.x = startFrame.origin.x + (startFrame.width - f.size.width)
        case .right:
            f.size.width = max(win.minSize.width, startFrame.width + dx)
        case .bottom:
            f.size.height = max(win.minSize.height, startFrame.height - dy)
            f.origin.y = startFrame.origin.y + (startFrame.height - f.size.height)
        }
        win.setFrame(f, display: true)
    }
}

// MARK: - 标题栏

final class TitleBarView: NSView {
    var title: String = "" { didSet { needsDisplay = true } }
    var onClose: (() -> Void)?
    var onMinimize: (() -> Void)?
    var onZoom: (() -> Void)?
    private var hovered: Int = -1
    private var pressed: Int = -1

    override var isFlipped: Bool { true }

    private func buttonRect(_ i: Int) -> NSRect {
        let w: CGFloat = 16, h: CGFloat = 14
        let gap: CGFloat = 2
        let right = bounds.width - 2
        let x = right - CGFloat(3 - i) * (w + gap)
        return NSRect(x: x, y: 4, width: w, height: h)
    }

    override func draw(_ dirtyRect: NSRect) {
        // Win98 标题栏是 海军蓝 → 亮蓝 的水平渐变
        if let g = NSGradient(starting: Theme.navy, ending: Theme.navyHi) {
            g.draw(in: bounds, angle: 0)
        } else {
            Theme.navy.setFill(); bounds.fill()
        }

        Icon.folder(16).draw(in: NSRect(x: 3, y: 3, width: 16, height: 16))
        let attrs: [NSAttributedString.Key: Any] = [
            .font: Theme.font(11, bold: true),
            .foregroundColor: NSColor.white
        ]
        (title as NSString).draw(at: NSPoint(x: 23, y: 4), withAttributes: attrs)

        for i in 0..<3 {
            let r = buttonRect(i)
            Theme.face.setFill(); r.fill()
            if pressed == i {
                Bevel.sunken(r)
            } else {
                Bevel.raised(r)
            }
            let c = NSPoint(x: r.midX, y: r.midY)
            let path = NSBezierPath()
            path.lineWidth = 1.4
            Theme.dark.setStroke()
            switch i {
            case 0:
                path.move(to: NSPoint(x: c.x - 4, y: c.y + 3))
                path.line(to: NSPoint(x: c.x + 4, y: c.y + 3))
            case 1:
                path.appendRect(NSRect(x: c.x - 4, y: c.y - 4, width: 8, height: 8))
                Theme.dark.setFill()
                NSRect(x: c.x - 4, y: c.y - 1, width: 8, height: 1).fill()
            default:
                path.move(to: NSPoint(x: c.x - 3.5, y: c.y - 3.5))
                path.line(to: NSPoint(x: c.x + 3.5, y: c.y + 3.5))
                path.move(to: NSPoint(x: c.x + 3.5, y: c.y - 3.5))
                path.line(to: NSPoint(x: c.x - 3.5, y: c.y + 3.5))
            }
            path.stroke()
        }
        // 标题栏与菜单栏之间 1px 分隔
        Theme.dark.setFill()
        NSRect(x: 0, y: bounds.height - 1, width: bounds.width, height: 1).fill()
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        for i in 0..<3 where buttonRect(i).contains(p) {
            pressed = i
            needsDisplay = true
            return
        }
        window?.performDrag(with: event)
    }

    override func mouseUp(with event: NSEvent) {
        guard pressed >= 0 else { return }
        let i = pressed
        pressed = -1
        needsDisplay = true
        let p = convert(event.locationInWindow, from: nil)
        guard buttonRect(i).contains(p) else { return }
        switch i {
        case 0: onMinimize?()
        case 1: onZoom?()
        default: onClose?()
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: bounds,
                                       options: [.mouseMoved, .mouseEnteredAndExited, .activeInActiveApp],
                                       owner: self, userInfo: nil))
    }

    override func mouseMoved(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        var h = -1
        for i in 0..<3 where buttonRect(i).contains(p) { h = i }
        if h != hovered { hovered = h; needsDisplay = true }
    }
}

// MARK: - 菜单栏

final class MenuBarView: NSView {
    struct Item { let title: String; let menu: () -> NSMenu }
    var items: [Item] = [] { didSet { needsDisplay = true } }
    private var activeIndex: Int = -1
    private var hoveredIndex: Int = -1
    private var itemRects: [NSRect] = []

    override var isFlipped: Bool { true }

    private func measure() {
        itemRects.removeAll()
        var x: CGFloat = 2
        for it in items {
            let w = (it.title as NSString).size(withAttributes: [.font: Theme.font(11)]).width + 16
            itemRects.append(NSRect(x: x, y: 1, width: w, height: M.menuH - 2))
            x += w
        }
    }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()
        measure()
        for (i, it) in items.enumerated() {
            let r = itemRects[i]
            if i == activeIndex {
                Bevel.sunken(r)
            } else if i == hoveredIndex {
                Bevel.raised(r)
            }
            let attrs: [NSAttributedString.Key: Any] = [.font: Theme.font(11), .foregroundColor: Theme.text]
            let sz = (it.title as NSString).size(withAttributes: attrs)
            (it.title as NSString).draw(at: NSPoint(x: r.midX - sz.width / 2, y: (r.height - sz.height) / 2 + 1),
                                        withAttributes: attrs)
        }
        // 底部分隔线
        Theme.shadow.setFill()
        NSRect(x: 0, y: bounds.height - 1, width: bounds.width, height: 1).fill()
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        measure()
        for (i, _) in items.enumerated() where itemRects[i].contains(p) {
            activeIndex = i
            needsDisplay = true
            items[i].menu().popUp(positioning: nil,
                                  at: NSPoint(x: itemRects[i].minX, y: itemRects[i].maxY),
                                  in: self)
            activeIndex = -1
            needsDisplay = true
            return
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: bounds,
                                       options: [.mouseMoved, .mouseEnteredAndExited, .activeInActiveApp],
                                       owner: self, userInfo: nil))
    }

    override func mouseMoved(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        var h = -1
        measure()
        for (i, _) in items.enumerated() where itemRects[i].contains(p) { h = i }
        if h != hoveredIndex { hoveredIndex = h; needsDisplay = true }
    }

    override func mouseExited(with event: NSEvent) {
        if hoveredIndex != -1 { hoveredIndex = -1; needsDisplay = true }
    }
}

// MARK: - 工具栏（图标在左、文字在右，Win98 排列）

final class ToolbarView: NSView {
    struct Item {
        let id: String
        let title: String
        let icon: NSImage
        var enabled: Bool = true
    }
    var items: [Item] = [] { didSet { needsDisplay = true } }
    var onAction: ((String) -> Void)?

    private var hovered: String?
    private var pressed: String?

    override var isFlipped: Bool { true }

    private func itemRect(_ i: Int) -> NSRect {
        var x: CGFloat = 3
        for (j, it) in items.enumerated() {
            let tw = (it.title as NSString).size(withAttributes: [.font: Theme.font(11)]).width
            let w = 6 + 16 + 4 + tw + 6
            let r = NSRect(x: x, y: 2, width: w, height: M.toolH - 4)
            if j == i { return r }
            x += w + 1
        }
        return .zero
    }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()
        for i in 0..<items.count {
            let it = items[i]
            let r = itemRect(i)
            if it.enabled {
                if pressed == it.id {
                    Bevel.sunken(r)
                } else if hovered == it.id {
                    Bevel.raised(r)
                }
            }
            let iconRect = NSRect(x: r.minX + 6, y: r.minY + (r.height - 16) / 2, width: 16, height: 16)
            it.icon.draw(in: iconRect)
            let attrs: [NSAttributedString.Key: Any] = [
                .font: Theme.font(11),
                .foregroundColor: it.enabled ? Theme.text : Theme.dim
            ]
            let sz = (it.title as NSString).size(withAttributes: attrs)
            (it.title as NSString).draw(at: NSPoint(x: iconRect.maxX + 4, y: r.midY - sz.height / 2 + 0.5),
                                        withAttributes: attrs)
        }
        // 底部 1px 分隔
        Theme.shadow.setFill()
        NSRect(x: 0, y: bounds.height - 1, width: bounds.width, height: 1).fill()
        Theme.white.setFill()
        NSRect(x: 0, y: bounds.height - 2, width: bounds.width, height: 1).fill()
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        for i in 0..<items.count where itemRect(i).contains(p) && items[i].enabled {
            pressed = items[i].id
            needsDisplay = true
            return
        }
    }

    override func mouseUp(with event: NSEvent) {
        defer { pressed = nil; needsDisplay = true }
        guard let id = pressed else { return }
        let p = convert(event.locationInWindow, from: nil)
        for i in 0..<items.count where items[i].id == id && itemRect(i).contains(p) {
            onAction?(id)
            return
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: bounds,
                                       options: [.mouseMoved, .mouseEnteredAndExited, .activeInActiveApp],
                                       owner: self, userInfo: nil))
    }

    override func mouseMoved(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        var h: String?
        for i in 0..<items.count where itemRect(i).contains(p) { h = items[i].id }
        if h != hovered { hovered = h; needsDisplay = true }
    }

    override func mouseExited(with event: NSEvent) {
        if hovered != nil { hovered = nil; needsDisplay = true }
    }
}

// MARK: - 地址栏

/// 单击进入编辑时默认全选内容，跟浏览器/资源管理器一致
final class SelectAllField: NSTextField {
    override func becomeFirstResponder() -> Bool {
        let ok = super.becomeFirstResponder()
        if ok { currentEditor()?.selectAll(nil) }
        return ok
    }

    override func mouseDown(with event: NSEvent) {
        let wasEditing = window?.firstResponder === currentEditor()
        super.mouseDown(with: event)
        if !wasEditing { currentEditor()?.selectAll(nil) }
    }
}

final class AddressBarView: NSView {
    var onGo: ((String) -> Void)?
    var onPick: ((String) -> Void)?
    private let field = SelectAllField()
    private let goButton = GoButton()
    private let arrow = DropArrow()

    override var isFlipped: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        field.font = Theme.font(11)
        field.isBordered = false
        field.drawsBackground = false
        field.focusRingType = .none
        field.textColor = Theme.text
        field.target = self
        field.action = #selector(go)
        addSubview(field)

        goButton.title = "转到"
        goButton.onClick = { [weak self] in self?.go() }
        addSubview(goButton)

        arrow.onClick = { [weak self] in self?.showPlaces() }
        addSubview(arrow)
    }
    required init?(coder: NSCoder) { fatalError() }

    var path: String {
        get { field.stringValue }
        set { field.stringValue = newValue }
    }

    override func layout() {
        super.layout()
        let labelW: CGFloat = 52
        let goW: CGFloat = 56
        field.frame = NSRect(x: labelW + 6, y: 6, width: bounds.width - labelW - goW - 18 - 17, height: 15)
        arrow.frame = NSRect(x: bounds.width - goW - 5 - 17, y: 5, width: 15, height: 16)
        goButton.frame = NSRect(x: bounds.width - goW - 5, y: 3, width: goW, height: 20)
    }

    @objc private func go() {
        onGo?(field.stringValue)
    }

    private func showPlaces() {
        let m = NSMenu()
        let home = FileManager.default.homeDirectoryForCurrentUser
        var items: [(String, String)] = [("我的电脑", "/"), ("个人文件夹", home.path)]
        for (t, d) in [("桌面", "Desktop"), ("我的文档", "Documents"), ("下载", "Downloads"),
                       ("我的图片", "Pictures"), ("我的音乐", "Music"), ("我的影片", "Movies")] {
            items.append((t, home.appendingPathComponent(d).path))
        }
        for (t, p) in items {
            let it = NSMenuItem(title: t, action: #selector(pickPlace(_:)), keyEquivalent: "")
            it.target = self
            it.representedObject = p
            m.addItem(it)
        }
        m.popUp(positioning: nil, at: NSPoint(x: arrow.frame.minX, y: 26), in: self)
    }

    @objc private func pickPlace(_ sender: NSMenuItem) {
        guard let p = sender.representedObject as? String else { return }
        path = (p as NSString).abbreviatingWithTildeInPath
        onPick?(p)
    }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()
        let attrs: [NSAttributedString.Key: Any] = [.font: Theme.font(11), .foregroundColor: Theme.text]
        ("地址(D)" as NSString).draw(at: NSPoint(x: 4, y: 6), withAttributes: attrs)

        let fr = NSRect(x: 52, y: 3, width: bounds.width - 52 - 61 - 17, height: 20)
        Theme.white.setFill(); fr.fill()
        Bevel.field(fr)
    }
}

/// 地址栏右侧的小下拉箭头
final class DropArrow: NSView {
    var onClick: (() -> Void)?

    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill(); bounds.fill()
        Bevel.raised(bounds)
        let c = NSPoint(x: bounds.midX, y: bounds.midY)
        let t = NSBezierPath()
        t.move(to: NSPoint(x: c.x - 3.5, y: c.y - 1.5))
        t.line(to: NSPoint(x: c.x + 3.5, y: c.y - 1.5))
        t.line(to: NSPoint(x: c.x, y: c.y + 2.5))
        t.close()
        Theme.dark.setFill(); t.fill()
    }

    override func mouseDown(with event: NSEvent) {
        onClick?()
    }
}

/// 普通 3D 按钮
final class GoButton: NSView {
    var title: String = "" { didSet { needsDisplay = true } }
    var onClick: (() -> Void)?
    private var pressed = false
    private var hovered = false

    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()
        if pressed { Bevel.sunken(bounds) } else { Bevel.raised(bounds) }
        let attrs: [NSAttributedString.Key: Any] = [.font: Theme.font(11), .foregroundColor: Theme.text]
        let sz = (title as NSString).size(withAttributes: attrs)
        let off: CGFloat = pressed ? 1 : 0
        (title as NSString).draw(at: NSPoint(x: bounds.midX - sz.width / 2 + off,
                                             y: bounds.midY - sz.height / 2 + off),
                                 withAttributes: attrs)
    }

    override func mouseDown(with event: NSEvent) {
        pressed = true; needsDisplay = true
    }

    override func mouseUp(with event: NSEvent) {
        pressed = false; needsDisplay = true
        let p = convert(event.locationInWindow, from: nil)
        if bounds.contains(p) { onClick?() }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: bounds,
                                       options: [.mouseEnteredAndExited, .activeInActiveApp],
                                       owner: self, userInfo: nil))
    }
}

// MARK: - 状态栏

final class StatusBarView: NSView {
    var leftText: String = "" { didSet { needsDisplay = true } }
    var rightText: String = "" { didSet { needsDisplay = true } }

    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()

        let attrs: [NSAttributedString.Key: Any] = [.font: Theme.font(11), .foregroundColor: Theme.text]

        // 两个凹陷面板
        let p1 = NSRect(x: 1, y: 2, width: 240, height: bounds.height - 3)
        let p2 = NSRect(x: 243, y: 2, width: max(20, bounds.width - 244), height: bounds.height - 3)
        for p in [p1, p2] {
            Theme.face.setFill(); p.fill()
            Bevel.sunken(p)
        }
        NSGraphicsContext.saveGraphicsState()
        NSBezierPath(rect: p1.insetBy(dx: 2, dy: 1)).addClip()
        (leftText as NSString).draw(at: NSPoint(x: p1.minX + 3, y: p1.minY + 2), withAttributes: attrs)
        NSGraphicsContext.restoreGraphicsState()
        let sz = (rightText as NSString).size(withAttributes: attrs)
        (rightText as NSString).draw(at: NSPoint(x: p2.maxX - sz.width - 4, y: p2.minY + 2), withAttributes: attrs)
    }
}

// MARK: - 滚动条

final class Win98Scroller: NSScroller {
    override class var isCompatibleWithOverlayScrollers: Bool { false }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()

        let dec = rect(for: .decrementLine)
        let inc = rect(for: .incrementLine)
        let slot = rect(for: .knobSlot)

        if !slot.isEmpty {
            Theme.faceHi.setFill(); slot.fill()
            Bevel.sunken(slot)
        }
        for (r, up) in [(dec, true), (inc, false)] where !r.isEmpty {
            Theme.face.setFill(); r.fill()
            Bevel.raised(r)
            let c = NSPoint(x: r.midX, y: r.midY)
            let t = NSBezierPath()
            if up {
                t.move(to: NSPoint(x: c.x - 3.5, y: c.y + 2))
                t.line(to: NSPoint(x: c.x + 3.5, y: c.y + 2))
                t.line(to: NSPoint(x: c.x, y: c.y - 2.5))
            } else {
                t.move(to: NSPoint(x: c.x - 3.5, y: c.y - 2))
                t.line(to: NSPoint(x: c.x + 3.5, y: c.y - 2))
                t.line(to: NSPoint(x: c.x, y: c.y + 2.5))
            }
            t.close()
            Theme.dark.setFill(); t.fill()
        }

        let knob = rect(for: .knob)
        if !knob.isEmpty {
            Theme.face.setFill(); knob.fill()
            Bevel.raised(knob)
        }
    }
}

// MARK: - 自绘 Win98 滚动条

final class Win98ScrollBar: NSView {
    weak var scrollView: NSScrollView? {
        didSet { attach() }
    }
    private var dragStartY: CGFloat = 0
    private var dragStartScroll: CGFloat = 0
    private let arrowH: CGFloat = 16

    override var isFlipped: Bool { true }

    private var docHeight: CGFloat { scrollView?.documentView?.bounds.height ?? 0 }
    private var viewHeight: CGFloat { max(1, scrollView?.contentView.bounds.height ?? 1) }
    private var scrollY: CGFloat { scrollView?.contentView.bounds.origin.y ?? 0 }

    private var trackRect: NSRect {
        NSRect(x: 0, y: arrowH, width: bounds.width, height: max(0, bounds.height - arrowH * 2))
    }

    private func knobRect() -> NSRect {
        let track = trackRect
        guard docHeight > viewHeight, track.height > 0 else { return .zero }
        let ratio = min(1, viewHeight / docHeight)
        let h = max(18, track.height * ratio)
        let maxScroll = docHeight - viewHeight
        let p = maxScroll > 0 ? min(1, scrollY / maxScroll) : 0
        return NSRect(x: track.minX + 1, y: track.minY + (track.height - h) * p,
                      width: track.width - 2, height: h)
    }

    private func attach() {
        guard let clip = scrollView?.contentView else { return }
        clip.postsBoundsChangedNotifications = true
        NotificationCenter.default.removeObserver(self)
        NotificationCenter.default.addObserver(self, selector: #selector(refresh),
                                               name: NSView.boundsDidChangeNotification, object: clip)
    }

    @objc func refresh() { needsDisplay = true }

    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()

        // 上下箭头
        for (r, up) in [(NSRect(x: 0, y: 0, width: bounds.width, height: arrowH), true),
                        (NSRect(x: 0, y: bounds.height - arrowH, width: bounds.width, height: arrowH), false)] {
            Theme.face.setFill(); r.fill()
            Bevel.raised(r)
            let c = NSPoint(x: r.midX, y: r.midY)
            let t = NSBezierPath()
            if up {
                t.move(to: NSPoint(x: c.x - 4, y: c.y + 2))
                t.line(to: NSPoint(x: c.x + 4, y: c.y + 2))
                t.line(to: NSPoint(x: c.x, y: c.y - 3))
            } else {
                t.move(to: NSPoint(x: c.x - 4, y: c.y - 2))
                t.line(to: NSPoint(x: c.x + 4, y: c.y - 2))
                t.line(to: NSPoint(x: c.x, y: c.y + 3))
            }
            t.close()
            Theme.dark.setFill(); t.fill()
        }

        // 轨道
        let track = trackRect
        if track.height > 0 {
            Theme.faceHi.setFill(); track.fill()
            Theme.shadow.setFill()
            NSRect(x: track.minX, y: track.minY, width: 1, height: track.height).fill()
            NSRect(x: track.maxX - 1, y: track.minY, width: 1, height: track.height).fill()
            Theme.white.setFill()
            NSRect(x: track.minX + 1, y: track.minY, width: 1, height: track.height).fill()
        }

        // 滑块
        let knob = knobRect()
        if !knob.isEmpty {
            Theme.face.setFill(); knob.fill()
            Bevel.raised(knob)
        }
    }

    private func scroll(to y: CGFloat) {
        guard let sv = scrollView else { return }
        let maxY = max(0, docHeight - viewHeight)
        let target = min(maxY, max(0, y))
        sv.contentView.scroll(to: NSPoint(x: 0, y: target))
        sv.reflectScrolledClipView(sv.contentView)
        needsDisplay = true
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let page = viewHeight - 20

        if p.y < arrowH {
            scroll(to: scrollY - 30)
        } else if p.y > bounds.height - arrowH {
            scroll(to: scrollY + 30)
        } else if knobRect().contains(p) {
            dragStartY = p.y
            dragStartScroll = scrollY
        } else if trackRect.contains(p) {
            scroll(to: p.y < knobRect().minY ? scrollY - page : scrollY + page)
        }
    }

    override func mouseDragged(with event: NSEvent) {
        guard dragStartY > 0 else { return }
        let p = convert(event.locationInWindow, from: nil)
        let track = trackRect
        let knobH = knobRect().height
        let travel = max(1, track.height - knobH)
        let maxScroll = max(1, docHeight - viewHeight)
        let dy = p.y - dragStartY
        scroll(to: dragStartScroll + dy / travel * maxScroll)
    }

    override func mouseUp(with event: NSEvent) {
        dragStartY = 0
    }
}
