import AppKit

// MARK: - 列表区域

final class FileListView: NSView {
    let tableView = XPTableView()
    private let scroll = NSScrollView()
    private let vbar = Win98ScrollBar()
    let header = HeaderView()

    var onSort: ((Int) -> Void)?
    var onOpen: (() -> Void)?

    override var isFlipped: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        tableView.headerView = nil
        tableView.rowHeight = M.rowH
        tableView.intercellSpacing = .zero
        tableView.backgroundColor = .white
        tableView.style = .plain
        tableView.selectionHighlightStyle = .regular
        tableView.allowsMultipleSelection = true
        tableView.allowsEmptySelection = true
        tableView.usesAlternatingRowBackgroundColors = false
        tableView.gridStyleMask = []
        // 不让 AppKit 自动拉伸列宽，列宽完全由我们自己算
        tableView.columnAutoresizingStyle = .noColumnAutoresizing

        let cols: [(String, String, CGFloat)] = [
            ("name", "名称", 320), ("size", "大小", 80), ("type", "类型", 100), ("date", "修改日期", 130)
        ]
        for (id, title, w) in cols {
            let c = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(id))
            c.title = title
            c.width = w
            c.minWidth = 40
            tableView.addTableColumn(c)
        }

        scroll.documentView = tableView
        scroll.hasVerticalScroller = false
        scroll.hasHorizontalScroller = false
        scroll.autohidesScrollers = false
        scroll.drawsBackground = true
        scroll.backgroundColor = .white
        scroll.borderType = .noBorder
        addSubview(scroll)
        vbar.scrollView = scroll
        addSubview(vbar)

        header.onSort = { [weak self] idx in self?.onSort?(idx) }
        addSubview(header)

    }
    required init?(coder: NSCoder) { fatalError() }


    override func draw(_ dirtyRect: NSRect) {
        Theme.face.setFill()
        bounds.fill()
        // 列表区外围的凹陷边框，Win98 的经典客户区
        let area = NSRect(x: 1, y: M.headerH + 1,
                          width: max(0, bounds.width - 2 - M.scrollerW - 2),
                          height: max(0, bounds.height - M.headerH - 3))
        Theme.white.setFill(); area.fill()
        Bevel.sunken(area)
    }

    override func layout() {
        super.layout()
        let w = bounds.width
        let barW = M.scrollerW
        let tableW = max(0, w - 4 - barW)
        let fixed: CGFloat = 80 + 100 + 130
        let nameW = max(120, tableW - fixed)
        for (id, width) in [("name", nameW), ("size", 80), ("type", 100), ("date", 130)] {
            if let c = tableView.tableColumn(withIdentifier: NSUserInterfaceItemIdentifier(id)) { c.width = width }
        }
        // 表头与表格同宽同起点，列边界才不会错位
        header.widths = [nameW, 80, 100, 130]
        header.frame = NSRect(x: 2, y: 0, width: tableW, height: M.headerH)
        scroll.frame = NSRect(x: 2, y: M.headerH, width: tableW, height: max(0, bounds.height - M.headerH - 2))
        vbar.frame = NSRect(x: w - 2 - barW, y: M.headerH, width: barW, height: max(0, bounds.height - M.headerH - 2))
    }

    func reload() {
        tableView.reloadData()
        header.needsDisplay = true
        vbar.refresh()
    }
}

// MARK: - 表头（Win98 凸起列头）

final class HeaderView: NSView {
    var onSort: ((Int) -> Void)?
    var sortColumn: Int = 0
    var sortAsc: Bool = true
    var titles: [String] = ["名称", "大小", "类型", "修改日期"]
    var widths: [CGFloat] = []

    private var rects: [NSRect] = []

    override var isFlipped: Bool { true }

    private func computeRects() {
        var x: CGFloat = 0
        var out: [NSRect] = []
        for w in widths {
            out.append(NSRect(x: x, y: 0, width: w, height: M.headerH))
            x += w
        }
        rects = out
    }

    override func draw(_ dirtyRect: NSRect) {
        computeRects()
        Theme.face.setFill()
        bounds.fill()
        for (i, r) in rects.enumerated() {
            let cell = NSRect(x: r.minX + 1, y: 1, width: r.width - 2, height: M.headerH - 2)
            Theme.face.setFill(); cell.fill()
            Bevel.raised(cell)
            let attrs: [NSAttributedString.Key: Any] = [.font: Theme.font(11), .foregroundColor: Theme.text]
            let t = titles[i] as NSString
            let sz = t.size(withAttributes: attrs)
            let tx = (i == 1) ? cell.maxX - sz.width - 6 : cell.minX + 5
            t.draw(at: NSPoint(x: tx, y: (M.headerH - sz.height) / 2), withAttributes: attrs)
            if i == sortColumn {
                let arrow = Icon.sortArrow(8, up: sortAsc)
                let ax = i == 1 ? cell.minX + 3 : tx + sz.width + 3
                arrow.draw(in: NSRect(x: ax, y: (M.headerH - 8) / 2, width: 8, height: 8))
            }
        }
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        for (i, r) in rects.enumerated() where r.contains(p) {
            onSort?(i)
            return
        }
    }
}

// MARK: - 表格

final class XPTableView: NSTableView {
    var menuBuilder: (() -> NSMenu)?
    var onContextRow: ((Int) -> Void)?
    var onOpen: (() -> Void)?
    var onDelete: (() -> Void)?
    var onGoUp: (() -> Void)?
    var onPreview: (() -> Void)?
    var onRefresh: (() -> Void)?
    var onRename: (() -> Void)?
    /// 返回某行“图标+文字”实际占据的右边界，用于判断点击是否落在条目上
    var nameExtent: ((Int) -> CGFloat)?

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let clickedRow = row(at: p)
        if clickedRow >= 0 {
            let nameW = tableColumn(withIdentifier: NSUserInterfaceItemIdentifier("name"))?.width ?? bounds.width
            let extent = min(nameW, nameExtent?(clickedRow) ?? nameW)
            if p.x > extent {
                // 点在名字右侧空白或类型/大小/日期列：不改变选中
                return
            }
        }
        super.mouseDown(with: event)
        for case let row as XPTableRow in subviews {
            for case let cell as XPCell in row.subviews { cell.needsDisplay = true }
        }
        if event.clickCount == 2 && clickedRow >= 0 {
            onOpen?()
        }
    }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 36, 76:
            onOpen?()
        case 51:                      // 退格 = 上一级
            onGoUp?()
        case 117:                     // 前向删除 = 移到回收站
            onDelete?()
        case 49:                      // 空格 = 快速预览
            onPreview?()
        case 96:                      // F5 = 刷新
            onRefresh?()
        case 120:                     // F2 = 重命名
            onRename?()
        case 126 where event.modifierFlags.contains(.command):
            onGoUp?()
        default:
            super.keyDown(with: event)
        }
    }

    override func rightMouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let row = self.row(at: p)
        if row >= 0 && !selectedRowIndexes.contains(row) {
            selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        }
        onContextRow?(row)
        if let menu = menuBuilder?() {
            NSMenu.popUpContextMenu(menu, with: event, for: self)
        }
    }

    /// 窗口未激活时第一次点击也生效（macOS 默认会吞掉）
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func menu(for event: NSEvent) -> NSMenu? {
        let p = convert(event.locationInWindow, from: nil)
        let row = self.row(at: p)
        if row >= 0 && !selectedRowIndexes.contains(row) {
            selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        }
        onContextRow?(row)
        return menuBuilder?()
    }
}

final class XPTableRow: NSTableRowView {
    /// 行视图同样不拦截点击，统一交给表格处理
    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    /// 行重绘时必须让单元格一起重绘，否则会留下上一次的白色文字
    private func invalidateCells() {
        for case let cell as XPCell in subviews { cell.needsDisplay = true }
    }

    override func draw(_ dirtyRect: NSRect) {
        invalidateCells()
        super.draw(dirtyRect)
    }

    override func drawBackground(in dirtyRect: NSRect) {
        invalidateCells()
        super.drawBackground(in: dirtyRect)
    }

    override func drawSelection(in dirtyRect: NSRect) {
        invalidateCells()
        guard selectionHighlightStyle != .none else { return }
        // 只高亮“图标 + 名字”这一块，不做满行选择
        let nameW = (superview as? NSTableView)?
            .tableColumn(withIdentifier: NSUserInterfaceItemIdentifier("name"))?.width ?? bounds.width
        Theme.select.setFill()
        NSRect(x: 0, y: 0, width: min(nameW, bounds.width), height: bounds.height).fill()
        // 选中状态变了要让文字重新配色（否则圈选时是蓝底黑字）
        for case let cell as XPCell in subviews { cell.refreshSelection() }
    }
}

final class XPCell: NSView {
    private var text: String = ""
    private var iconImage: NSImage?
    private var isSelected = false
    private var alignment: NSTextAlignment = .left

    /// 当前是否真的处于选中态：以表格的选中集为准，圈选时也能立刻反映
    private var liveSelected: Bool {
        guard let rowView = superview as? NSTableRowView,
              let table = rowView.superview as? NSTableView else { return isSelected }
        let r = table.row(for: rowView)
        return r >= 0 && table.selectedRowIndexes.contains(r)
    }

    init(identifier: NSUserInterfaceItemIdentifier) {
        super.init(frame: .zero)
        self.identifier = identifier
    }
    required init?(coder: NSCoder) { fatalError() }

    override var isFlipped: Bool { true }

    /// 让点击穿透到表格，避免自定义视图吞掉单击/双击
    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    var debugColor: String { isSelected ? "sel" : "norm" }

    func configureSelected(_ selected: Bool) {
        isSelected = selected
        needsDisplay = true
    }

    func refreshSelection() {
        needsDisplay = true
    }

    func configure(text: String, image: NSImage?, selected: Bool, alignment: NSTextAlignment = .left) {
        self.text = text
        self.iconImage = image
        self.isSelected = selected
        self.alignment = alignment
        needsDisplay = true
    }

    override func draw(_ dirtyRect: NSRect) {
        NSGraphicsContext.saveGraphicsState()
        NSBezierPath(rect: bounds).addClip()
        // 单元格自己铺底色：即使行重绘顺序有偏差，也不会出现白字白底
        let onHighlight = liveSelected && identifier?.rawValue == "name"
        (onHighlight ? Theme.select : Theme.white).setFill()
        bounds.fill()
        if let img = iconImage {
            img.draw(in: NSRect(x: 3, y: 2, width: 16, height: 16))
        }
        // 只有名字列落在蓝底上，其他列始终黑字
        let color: NSColor = onHighlight ? .white : Theme.text
        let para = NSMutableParagraphStyle()
        para.lineBreakMode = .byTruncatingTail
        para.alignment = alignment
        let attrs: [NSAttributedString.Key: Any] = [
            .font: Theme.font(11), .foregroundColor: color, .paragraphStyle: para
        ]
        let tx: CGFloat = iconImage != nil ? 22 : 5
        let rightPad: CGFloat = alignment == .right ? 8 : 4
        let rect = NSRect(x: tx, y: 2, width: max(0, bounds.width - tx - rightPad), height: 15)
        (text as NSString).draw(in: rect, withAttributes: attrs)
        NSGraphicsContext.restoreGraphicsState()
    }
}
