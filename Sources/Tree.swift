import AppKit

// MARK: - 左侧文件树

final class TreeBodyView: NSView {
    final class Node {
        enum Kind { case machine, volume, folder, special, favorite, favorites, trash }
        let url: URL
        let title: String
        var depth: Int
        var kind: Kind
        var expanded = false
        var loaded = false
        var hasChildren = true
        var children: [Node] = []

        init(url: URL, title: String, depth: Int, kind: Kind) {
            self.url = url
            self.title = title
            self.depth = depth
            self.kind = kind
        }
    }

    var onSelect: ((URL, Bool) -> Void)?
    var onDrop: (([URL], URL) -> Bool)?
    var menuBuilder: (() -> NSMenu?)?
    var onContextNode: ((URL?, Bool) -> Void)?
    private var contextNodeURL: URL?
    private var contextIsFavorite = false

    /// 从树里点进来的导航：只更新选中，不展开、不滚动，避免树“乱跳”
    private var skipNextReveal = false

    var currentURL: URL = URL(fileURLWithPath: "/") {
        didSet {
            if let id = selectedID, let node = flat.first(where: { ObjectIdentifier($0) == id }),
               node.url.standardizedFileURL.path != currentURL.standardizedFileURL.path {
                selectedID = nil
            }
            if skipNextReveal {
                skipNextReveal = false
                needsDisplay = true
                return
            }
            reveal()
            scrollSelection()
            needsDisplay = true
        }
    }

    func setCurrentURL(_ url: URL, fromTreeClick: Bool) {
        let old = currentURL
        skipNextReveal = fromTreeClick
        currentURL = url
        if currentURL == old { skipNextReveal = false }
    }

    /// 与主视图一致：是否显示隐藏项（macOS 上 /usr、/private 等属于隐藏的 firmlink）
    var showHidden = false {
        didSet {
            guard showHidden != oldValue else { return }
            buildRoots()
            flatten()
            reveal()
            needsDisplay = true
        }
    }

    private var roots: [Node] = []
    private var specials: [Node] = []
    private var favorites: [Node] = []
    private var favoriteRoot: Node?
    /// 用户刚点过的节点：URL 相同的节点（我的电脑 / 磁盘、快捷入口 / 真实路径）也能分辨
    private var selectedID: ObjectIdentifier?
    private var flat: [Node] = []
    private var hovered = -1
    private var dropRow = -1

    override var isFlipped: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        buildRoots()
        flatten()
        registerForDraggedTypes([.fileURL])
    }
    required init?(coder: NSCoder) { fatalError() }

    var contentHeight: CGFloat { CGFloat(flat.count) * M.rowH + 4 }

    func debugInfo() -> String {
        let rows = flat.map { "\($0.depth):\($0.title)" }.joined(separator: ",")
        return "treeURL=\(currentURL.path) roots=\(roots.count) fav=\(favorites.count) rows=\(flat.count) sel=\(selectedRow.map(String.init) ?? "nil") [\(rows)]"
    }

    // MARK: 构建

    /// 截图用：不列出挂载卷，避免把本机挂载的镜像名带进公开截图
    static var hideVolumes = false
    /// 截图用：用演示目录替换整棵侧栏树，避免暴露真实主目录
    static var demoRoot: URL?

    private func buildRoots() {
        if let demo = TreeBodyView.demoRoot {
            let machine = Node(url: URL(fileURLWithPath: "/"), title: "我的电脑", depth: 0, kind: .machine)
            machine.expanded = true
            machine.loaded = true
            let vol = Node(url: demo, title: "Macintosh HD", depth: 1, kind: .volume)
            machine.children = [vol]
            machine.hasChildren = true
            roots = [machine]
            return
        }
        let fm = FileManager.default
        let machine = Node(url: URL(fileURLWithPath: "/"), title: "我的电脑", depth: 0, kind: .machine)
        machine.expanded = true
        machine.loaded = true

        var volumes: [Node] = []
        let boot = Node(url: URL(fileURLWithPath: "/"),
                        title: fm.displayName(atPath: "/"), depth: 1, kind: .volume)
        volumes.append(boot)
        let names = (try? fm.contentsOfDirectory(atPath: "/Volumes")) ?? []
        for n in names.sorted() where !TreeBodyView.hideVolumes {
            let path = "/Volumes/" + n
            if let dest = try? fm.destinationOfSymbolicLink(atPath: path), dest == "/" { continue }
            let url = URL(fileURLWithPath: path)
            let title = (try? url.resourceValues(forKeys: [.volumeLocalizedNameKey]))?.volumeLocalizedName ?? n
            volumes.append(Node(url: url, title: title, depth: 1, kind: .volume))
        }
        machine.children = volumes
        machine.hasChildren = !volumes.isEmpty
        roots = [machine]



        let home = fm.homeDirectoryForCurrentUser
        let list: [(String, String)] = [
            ("桌面", "Desktop"), ("我的文档", "Documents"), ("下载", "Downloads"),
            ("我的图片", "Pictures"), ("我的音乐", "Music"), ("我的影片", "Movies")
        ]
        // 收藏夹：与“我的电脑”平级的一组文件夹
        let favPaths = UserDefaults.standard.stringArray(forKey: "Favorites") ?? []
        favorites = []
        for p in favPaths {
            let url = URL(fileURLWithPath: p)
            var isDir: ObjCBool = false
            guard FileManager.default.fileExists(atPath: p, isDirectory: &isDir), isDir.boolValue else { continue }
            let node = Node(url: url, title: url.lastPathComponent, depth: 1, kind: .favorite)
            node.hasChildren = hasAnySubdirectory(url)
            favorites.append(node)
        }
        if !favorites.isEmpty {
            let group = Node(url: home, title: "收藏夹", depth: 0, kind: .favorites)
            group.children = favorites
            group.hasChildren = true
            group.expanded = true
            group.loaded = true
            favoriteRoot = group
            roots.append(group)
        }

        specials = list.map { title, dir in
            let node = Node(url: home.appendingPathComponent(dir), title: title, depth: 0, kind: .special)
            node.hasChildren = hasAnySubdirectory(node.url)
            return node
        }
        // 回收站：Windows 资源管理器把回收站放在树的最下面
        RecycleBin.ensureExists()
        let trashNode = Node(url: RecycleBin.root, title: "回收站", depth: 0, kind: .trash)
        trashNode.hasChildren = hasAnySubdirectory(RecycleBin.root)
        specials.append(trashNode)
    }

    private func loadChildren(_ node: Node) {
        guard !node.loaded else { return }
        node.loaded = true
        let keys: [URLResourceKey] = [.isDirectoryKey, .isPackageKey]
        var kids: [Node] = []
        let options: FileManager.DirectoryEnumerationOptions = showHidden ? [] : [.skipsHiddenFiles]
        if let list = try? FileManager.default.contentsOfDirectory(at: node.url,
                                                                   includingPropertiesForKeys: keys,
                                                                   options: options) {
            for u in list {
                guard let v = try? u.resourceValues(forKeys: Set(keys)),
                      v.isDirectory == true, v.isPackage != true else { continue }
                var depth = node.depth + 1
                var kind = Node.Kind.folder
                if node.kind == .machine { depth = 1; kind = .volume }
                kids.append(Node(url: u, title: u.lastPathComponent, depth: depth, kind: kind))
            }
        }
        kids.sort { $0.title.localizedStandardCompare($1.title) == .orderedAscending }
        for kid in kids { kid.hasChildren = hasAnySubdirectory(kid.url) }
        node.children = kids
        node.hasChildren = !kids.isEmpty
        Self.cacheHasChildren(node.url.standardizedFileURL.path, node.hasChildren)
    }

    /// 「是否有子文件夹」的结果缓存：启动时不必反复读桌面/文稿/下载，
    /// 也就不会每次启动都触发系统隐私授权
    private static var childCache: [String: Bool] = {
        (UserDefaults.standard.dictionary(forKey: "TreeHasChildren") as? [String: Bool]) ?? [:]
    }()

    private static func cacheHasChildren(_ path: String, _ value: Bool) {
        if childCache[path] != value {
            childCache[path] = value
            UserDefaults.standard.set(childCache, forKey: "TreeHasChildren")
        }
    }

    /// 只有真的含子文件夹才画 “+”
    private func hasAnySubdirectory(_ url: URL) -> Bool {
        let key = url.standardizedFileURL.path
        if let cached = Self.childCache[key] { return cached }
        let result = probeHasSubdirectory(url)
        Self.cacheHasChildren(key, result)
        return result
    }

    private func probeHasSubdirectory(_ url: URL) -> Bool {
        let keys: [URLResourceKey] = [.isDirectoryKey, .isPackageKey]
        guard let list = try? FileManager.default.contentsOfDirectory(at: url,
                                                                      includingPropertiesForKeys: keys,
                                                                      options: showHidden ? [] : [.skipsHiddenFiles]) else { return false }
        for u in list {
            if let v = try? u.resourceValues(forKeys: Set(keys)),
               v.isDirectory == true, v.isPackage != true {
                return true
            }
        }
        return false
    }

    private func flatten() {
        var out: [Node] = []
        func walk(_ n: Node) {
            out.append(n)
            if n.expanded { for c in n.children { walk(c) } }
        }
        for r in roots { walk(r) }
        for s in specials { walk(s) }
        flat = out
    }

    /// 添加/移除收藏并重建树
    func addFavorite(_ url: URL) {
        var list = UserDefaults.standard.stringArray(forKey: "Favorites") ?? []
        let p = url.standardizedFileURL.path
        guard !list.contains(p) else { return }
        list.append(p)
        UserDefaults.standard.set(list, forKey: "Favorites")
        rebuildPreservingExpansion()
    }

    func removeFavorite(_ url: URL) {
        var list = UserDefaults.standard.stringArray(forKey: "Favorites") ?? []
        list.removeAll { $0 == url.standardizedFileURL.path }
        UserDefaults.standard.set(list, forKey: "Favorites")
        rebuildPreservingExpansion()
    }

    /// 主视图列过某个目录后，用它来校正该目录的 + 状态（新建子文件夹后立刻生效）
    func noteChildren(of url: URL, hasSubdir: Bool) {
        let path = url.standardizedFileURL.path
        Self.cacheHasChildren(path, hasSubdir)
        if let node = flat.first(where: { $0.url.standardizedFileURL.path == path }) {
            node.hasChildren = hasSubdir
        }
    }

    func reloadAll() { rebuildPreservingExpansion() }

    private func rebuildPreservingExpansion() {
        buildRoots()
        flatten()
        reveal()
        needsDisplay = true
    }

    /// 展开当前路径的各级父目录
    private func reveal() {
        let target = currentURL.standardizedFileURL.path
        for r in roots { revealIn(r, target) }
        flatten()
        let path = target
        let hasRealNode = flat.contains { $0.kind != .special && $0.kind != .favorite && $0.kind != .favorites && $0.url.standardizedFileURL.path == path }
        if !hasRealNode {
            for s in specials { revealIn(s, target) }
            flatten()
        }
        updateHeight()
    }

    private func revealIn(_ node: Node, _ target: String) {
        if node.kind == .favorites { return }   // 收藏夹是固定分组，不参与路径展开
        let base = node.url.standardizedFileURL.path
        let inside = target == base || (base == "/" ? target.hasPrefix("/") : target.hasPrefix(base + "/"))
        guard inside else { return }
        if node.kind == .machine {
            node.expanded = true
            if !node.loaded { loadChildren(node) }
        }
        if target == base {
            // 进入某个文件夹时，树里把它展开，跟 Explorer 一致
            if node.kind != .machine {
                if !node.loaded { loadChildren(node) }
                node.expanded = node.hasChildren
            }
            return
        }
        if !node.loaded { loadChildren(node) }
        node.expanded = true
        for c in node.children { revealIn(c, target) }
    }

    private func scrollSelection() {
        guard let idx = selectedRow else { return }
        scrollToVisible(rowRect(idx).insetBy(dx: 0, dy: -M.rowH))
    }

    private var selectedRow: Int? {
        let target = currentURL.standardizedFileURL.path
        if let id = selectedID, let i = flat.firstIndex(where: { ObjectIdentifier($0) == id }),
           flat[i].url.standardizedFileURL.path == target {
            return i
        }
        var bestReal: (Int, Int)?
        var bestShortcut: (Int, Int)?
        for (i, n) in flat.enumerated() where n.url.standardizedFileURL.path == target {
            if n.kind == .special || n.kind == .favorite || n.kind == .favorites {
                if bestShortcut == nil { bestShortcut = (i, n.depth) }
            } else if bestReal == nil || n.depth > bestReal!.1 {
                bestReal = (i, n.depth)
            }
        }
        return bestReal?.0 ?? bestShortcut?.0
    }

    private func updateHeight() {
        guard let superview else { return }
        let h = max(contentHeight, superview.bounds.height)
        if abs(frame.height - h) > 0.5 {
            setFrameSize(NSSize(width: superview.bounds.width, height: h))
        }
    }

    override func layout() {
        super.layout()
        updateHeight()
    }

    // MARK: 绘制

    private func rowRect(_ i: Int) -> NSRect {
        NSRect(x: 0, y: CGFloat(i) * M.rowH + 2, width: bounds.width, height: M.rowH)
    }

    private func boxRect(_ i: Int, _ node: Node) -> NSRect {
        let r = rowRect(i)
        let indent = CGFloat(node.depth) * 14 + 3
        return NSRect(x: r.minX + indent, y: r.minY + 5, width: 9, height: 9)
    }

    override func draw(_ dirtyRect: NSRect) {
        Theme.white.setFill()
        bounds.fill()

        for (i, node) in flat.enumerated() {
            let r = rowRect(i)
            guard r.intersects(dirtyRect) else { continue }
            let selected = selectedRow == i
            if selected {
                Theme.select.setFill()
                r.fill()
            } else if hovered == i || dropRow == i {
                Theme.faceHi.setFill()
                r.fill()
            }

            let indent = CGFloat(node.depth) * 14 + 3
            if node.hasChildren {
                let box = boxRect(i, node)
                Theme.white.setFill(); box.fill()
                Theme.shadow.setStroke()
                let p = NSBezierPath(rect: box.insetBy(dx: 0.5, dy: 0.5))
                p.lineWidth = 1
                p.stroke()
                Theme.dark.setStroke()
                let mid = NSPoint(x: box.midX, y: box.midY)
                let h = NSBezierPath()
                h.move(to: NSPoint(x: box.minX + 2, y: mid.y))
                h.line(to: NSPoint(x: box.maxX - 2, y: mid.y))
                h.lineWidth = 1
                h.stroke()
                if !node.expanded {
                    let v = NSBezierPath()
                    v.move(to: NSPoint(x: mid.x, y: box.minY + 2))
                    v.line(to: NSPoint(x: mid.x, y: box.maxY - 2))
                    v.lineWidth = 1
                    v.stroke()
                }
            }

            let iconRect = NSRect(x: r.minX + indent + 13, y: r.minY + 1.5, width: 16, height: 16)
            switch node.kind {
            case .machine: Icon.computer(16).draw(in: iconRect)
            case .volume: Icon.drive(16).draw(in: iconRect)
            case .trash: Icon.trash(16).draw(in: iconRect)
            case .special, .favorites, .favorite, .folder: Icon.folder(16, open: node.expanded).draw(in: iconRect)
            }

            let para = NSMutableParagraphStyle()
            para.lineBreakMode = .byTruncatingTail
            let attrs: [NSAttributedString.Key: Any] = [
                .font: Theme.font(11),
                .foregroundColor: selected ? NSColor.white : Theme.text,
                .paragraphStyle: para
            ]
            let textRect = NSRect(x: iconRect.maxX + 3, y: r.minY + 3,
                                  width: max(0, bounds.width - iconRect.maxX - 8), height: 14)
            (node.title as NSString).draw(in: textRect, withAttributes: attrs)
        }
    }

    // MARK: 交互

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        for (i, node) in flat.enumerated() where rowRect(i).contains(p) {
            // 点在 +/- 小方块上：只展开/收起，不导航
            if node.kind != .favorites, node.hasChildren,
               boxRect(i, node).insetBy(dx: -2, dy: -2).contains(p) {
                toggle(node)
                return
            }
            performClick(row: i, node: node, clickCount: event.clickCount)
            return
        }
    }

    private func performClick(row i: Int, node: Node, clickCount: Int) {
        if node.kind == .favorites {
            if node.hasChildren { toggle(node) }
            return
        }
        if clickCount == 2 {
            selectedID = ObjectIdentifier(node)
            if node.hasChildren { toggle(node) }
            needsDisplay = true
            return
        }
        selectedID = ObjectIdentifier(node)
        needsDisplay = true
        onSelect?(node.url, true)
    }

    // MARK: 调试

    var debugCount: Int { flat.count }
    var debugTopRow: Int {
        let y = enclosingScrollView?.contentView.bounds.origin.y ?? 0
        for i in 0..<flat.count where rowRect(i).maxY > y + 0.5 { return i }
        return flat.count
    }
    var debugSelected: Int? { selectedRow }
    func debugTitle(_ i: Int) -> String { i < flat.count ? flat[i].title : "?" }
    func debugRowIndex(titled t: String) -> Int? { flat.firstIndex { $0.title == t } }
    func debugClickRow(_ i: Int, clickCount: Int = 1) {
        guard i >= 0, i < flat.count else { print("  (no row \(i))"); return }
        let n = flat[i]
        print("  -> click row \(i) x\(clickCount): kind=\(n.kind) title=\(n.title) url=\(n.url.path) expanded=\(n.expanded)")
        performClick(row: i, node: n, clickCount: clickCount)
    }

    func debugRightClick(titled t: String) {
        guard let i = flat.firstIndex(where: { $0.title == t }) else { print("no node \(t)"); return }
        let n = flat[i]
        selectedID = ObjectIdentifier(n)
        contextNodeURL = n.url
        contextIsFavorite = n.kind == .favorite
        onContextNode?(n.url, n.kind == .favorite)
        if let m = menuBuilder?() {
            print("menu[\(t)]: " + m.items.map { "\($0.isSeparatorItem ? "|" : $0.title)(\($0.isEnabled ? "on" : "off"))" }.joined(separator: " "))
        }
    }

    override func rightMouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        var hit: Node?
        for (i, node) in flat.enumerated() where rowRect(i).contains(p) {
            hit = node
        }
        if let n = hit {
            selectedID = ObjectIdentifier(n)
            needsDisplay = true
        }
        contextNodeURL = hit?.url
        contextIsFavorite = hit?.kind == .favorite
        onContextNode?(hit?.url, hit?.kind == .favorite)
        if let menu = menuBuilder?() {
            NSMenu.popUpContextMenu(menu, with: event, for: self)
        }
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func menu(for event: NSEvent) -> NSMenu? {
        menuBuilder?()
    }

    private func toggle(_ node: Node) {
        if node.expanded {
            node.expanded = false
        } else {
            loadChildren(node)
            node.expanded = true
        }
        flatten()
        updateHeight()
        needsDisplay = true
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
        for i in 0..<flat.count where rowRect(i).contains(p) { h = i }
        if h != hovered { hovered = h; needsDisplay = true }
    }

    override func mouseExited(with event: NSEvent) {
        if hovered != -1 { hovered = -1; needsDisplay = true }
    }

    // MARK: 拖放目标

    private func rowIndex(at p: NSPoint) -> Int {
        for i in 0..<flat.count where rowRect(i).contains(p) { return i }
        return -1
    }

    private func dropDestination(_ info: NSDraggingInfo) -> URL? {
        let p = convert(info.draggingLocation, from: nil)
        let i = rowIndex(at: p)
        guard i >= 0 else { return nil }
        if dropRow != i { dropRow = i; needsDisplay = true }
        return flat[i].url
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        _ = dropDestination(sender)
        return .copy
    }

    override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        _ = dropDestination(sender)
        return .copy
    }

    override func draggingExited(_ sender: NSDraggingInfo?) {
        dropRow = -1
        needsDisplay = true
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        let urls = (sender.draggingPasteboard.readObjects(forClasses: [NSURL.self],
                                                          options: [.urlReadingFileURLsOnly: true]) as? [URL]) ?? []
        guard !urls.isEmpty, let folder = dropDestination(sender) else { return false }
        dropRow = -1
        needsDisplay = true
        return onDrop?(urls, folder) ?? false
    }
}

// MARK: - 侧栏外壳（固定表头 + 可滚动树）

final class TreePaneView: NSView {
    let tree = TreeBodyView()
    private let scroll = NSScrollView()
    private let vbar = Win98ScrollBar()

    var onSelect: ((URL, Bool) -> Void)? {
        get { tree.onSelect }
        set { tree.onSelect = newValue }
    }
    var onDrop: (([URL], URL) -> Bool)? {
        get { tree.onDrop }
        set { tree.onDrop = newValue }
    }
    var currentURL: URL {
        get { tree.currentURL }
        set { tree.currentURL = newValue }
    }

    func setCurrentURL(_ url: URL, fromTreeClick: Bool) {
        tree.setCurrentURL(url, fromTreeClick: fromTreeClick)
    }
    var menuBuilder: (() -> NSMenu?)? {
        get { tree.menuBuilder }
        set { tree.menuBuilder = newValue }
    }
    var onContextNode: ((URL?, Bool) -> Void)? {
        get { tree.onContextNode }
        set { tree.onContextNode = newValue }
    }
    func addFavorite(_ url: URL) { tree.addFavorite(url); reload() }
    func removeFavorite(_ url: URL) { tree.removeFavorite(url); reload() }
    func reloadAll() { tree.reloadAll(); reload() }
    func noteChildren(of url: URL, hasSubdir: Bool) {
        tree.noteChildren(of: url, hasSubdir: hasSubdir)
        tree.needsDisplay = true
    }

    override var isFlipped: Bool { true }

    override init(frame: NSRect) {
        super.init(frame: frame)
        scroll.documentView = tree
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

        let head = NSRect(x: 1, y: 1, width: bounds.width - 2, height: M.headerH - 1)
        Theme.face.setFill(); head.fill()
        Bevel.raised(head)

        let attrs: [NSAttributedString.Key: Any] = [.font: Theme.font(11, bold: true), .foregroundColor: Theme.text]
        ("文件夹" as NSString).draw(at: NSPoint(x: 5, y: 3), withAttributes: attrs)
    }

    override func layout() {
        super.layout()
        let barW = M.scrollerW
        let top = M.headerH + 1
        let height = max(0, bounds.height - top - 1)
        let viewport = NSRect(x: 1, y: top, width: max(0, bounds.width - 2 - barW), height: height)
        scroll.frame = viewport
        vbar.frame = NSRect(x: bounds.width - 1 - barW, y: top, width: barW, height: height)
        tree.setFrameSize(NSSize(width: viewport.width, height: max(tree.contentHeight, height)))
        vbar.refresh()
    }

    func reload() {
        needsLayout = true
        layoutSubtreeIfNeeded()
        tree.needsDisplay = true
        vbar.refresh()
    }
}
