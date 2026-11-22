import AppKit
import Quartz
import UniformTypeIdentifiers

enum ViewMode: Equatable { case details, icons, list }

struct Entry {
    let url: URL
    var name: String
    var isDir: Bool
    var size: Int64?
    var type: String
    var date: Date?
}

final class ExplorerController: NSObject, NSTableViewDataSource, NSTableViewDelegate, QLPreviewPanelDataSource, QLPreviewPanelDelegate {
    let window: XPWindow
    let root = RootView()

    private var currentURL: URL
    private var entries: [Entry] = []
    private var backStack: [URL] = []
    private var forwardStack: [URL] = []
    private var sortColumn = 0
    private var sortAsc = true
    private var showHidden = false
    private var generation = 0
    private var sidebarVisible = true
    private var viewMode: ViewMode = .details
    private var clipURLs: [URL] = []
    private var clipIsCut = false
    private var contextFolder: URL?
    private var contextIsFavorite = false

    private let sizeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "yyyy/M/d HH:mm"
        return f
    }()

    init(start: URL) {
        currentURL = start
        window = XPWindow(contentRect: NSRect(x: 0, y: 0, width: 1000, height: 640))
        super.init()
        build()
    }

    // MARK: 构建界面

    private func build() {
        let titleBar = TitleBarView()
        titleBar.onClose = { [weak self] in self?.window.performClose(nil) }
        titleBar.onMinimize = { [weak self] in self?.window.miniaturize(nil) }
        titleBar.onZoom = { [weak self] in self?.window.zoom(nil) }

        let menuBar = MenuBarView()
        let toolBar = ToolbarView()
        let addrBar = AddressBarView()
        let sidebar = TreePaneView()
        let listView = FileListView()
        let gridView = GridContainer()
        let statusBar = StatusBarView()

        root.titleBar = titleBar
        root.menuBar = menuBar
        root.toolBar = toolBar
        root.addrBar = addrBar
        root.sidebar = sidebar
        root.listView = listView
        root.gridView = gridView
        root.statusBar = statusBar

        root.addSubview(titleBar)
        root.addSubview(menuBar)
        root.addSubview(toolBar)
        root.addSubview(addrBar)
        root.addSubview(sidebar)
        root.splitter.currentWidth = { [weak root] in root?.sidebarWidth ?? 200 }
        root.splitter.onResize = { [weak self] w in
            guard let self else { return }
            let maxW = max(120, self.root.bounds.width - 300)
            self.root.sidebarWidth = min(max(120, w), maxW)
            self.root.layoutSubtreeIfNeeded()
        }
        root.splitter.onEnd = { [weak self] w in
            guard let self else { return }
            let maxW = max(120, self.root.bounds.width - 300)
            UserDefaults.standard.set(Double(min(max(120, w), maxW)), forKey: "sidebarWidth")
        }
        root.addSubview(root.splitter)
        if let saved = UserDefaults.standard.object(forKey: "sidebarWidth") as? Double, saved > 0 {
            root.sidebarWidth = CGFloat(saved)
        }
        root.addSubview(listView)
        root.addSubview(gridView)
        root.addSubview(statusBar)

        let leftEdge = ResizeEdge(side: .left)
        let rightEdge = ResizeEdge(side: .right)
        let bottomEdge = ResizeEdge(side: .bottom)
        root.addSubview(leftEdge)
        root.addSubview(rightEdge)
        root.addSubview(bottomEdge)
        root.leftEdge = leftEdge
        root.rightEdge = rightEdge
        root.bottomEdge = bottomEdge

        window.contentView = root
        window.setFrameAutosaveName("XPExplorerMain")

        // 工具栏
        toolBar.items = [
            .init(id: "back", title: "后退", icon: Icon.arrowLeft(16), enabled: false),
            .init(id: "forward", title: "前进", icon: Icon.arrowRight(16), enabled: false),
            .init(id: "up", title: "向上", icon: Icon.arrowUp(16), enabled: true),
            .init(id: "folders", title: "文件夹", icon: Icon.folder(16)),
            .init(id: "view", title: "查看", icon: Icon.views(16)),
            .init(id: "refresh", title: "刷新", icon: Icon.refresh(16))
        ]
        toolBar.onAction = { [weak self] id in
            guard let self else { return }
            switch id {
            case "back": self.goBack()
            case "forward": self.goForward()
            case "up": self.goUp()
            case "folders": self.toggleSidebar()
            case "view": self.cycleViewMode()
            case "refresh": self.load(self.currentURL)
            default: break
            }
        }

        menuBar.items = [
            .init(title: "文件") { [weak self] in self?.fileMenu() ?? NSMenu() },
            .init(title: "编辑") { [weak self] in self?.editMenu() ?? NSMenu() },
            .init(title: "查看") { [weak self] in self?.viewMenu() ?? NSMenu() },
            .init(title: "收藏") { [weak self] in self?.favMenu() ?? NSMenu() },
            .init(title: "工具") { [weak self] in self?.toolMenu() ?? NSMenu() },
            .init(title: "帮助") { [weak self] in self?.helpMenu() ?? NSMenu() }
        ]

        addrBar.onGo = { [weak self] text in self?.goToPath(text) }
        addrBar.onPick = { [weak self] path in self?.navigate(to: URL(fileURLWithPath: path)) }

        sidebar.onSelect = { [weak self] url in self?.navigate(to: url) }

        listView.tableView.dataSource = self
        listView.tableView.delegate = self
        listView.tableView.onOpen = { [weak self] in self?.openSelection() }
        listView.tableView.onDelete = { [weak self] in self?.trashSelection() }
        listView.tableView.onGoUp = { [weak self] in self?.goUp() }
        listView.tableView.onPreview = { [weak self] in self?.togglePreview() }
        listView.tableView.nameExtent = { [weak self] row in
            guard let self, row >= 0, row < self.entries.count else { return 0 }
            let textW = (self.entries[row].name as NSString).size(withAttributes: [.font: Theme.font(11)]).width
            return 22 + textW + 6
        }
        listView.tableView.onRefresh = { [weak self] in self?.load(self?.currentURL ?? URL(fileURLWithPath: "/")) }
        listView.tableView.onRename = { [weak self] in self?.renameSelection() }
        listView.onSort = { [weak self] idx in self?.sortBy(idx) }
        listView.tableView.menuBuilder = { [weak self] in self?.contextMenu() ?? NSMenu() }
        listView.tableView.onContextRow = { [weak self] row in
            guard let self, row >= 0, row < self.entries.count, self.entries[row].isDir else {
                self?.contextFolder = nil; return
            }
            self.contextFolder = self.entries[row].url
        }
        listView.tableView.registerForDraggedTypes([.fileURL])

        gridView.isHidden = true
        gridView.grid.mode = .big
        gridView.grid.onOpen = { [weak self] entry in self?.open(entry) }
        gridView.grid.onSelectionChanged = { [weak self] in self?.updateStatus() }
        gridView.grid.onOpenFirst = { [weak self] in self?.openSelection() }
        gridView.grid.onDelete = { [weak self] in self?.trashSelection() }
        gridView.grid.onGoUp = { [weak self] in self?.goUp() }
        gridView.grid.onPreview = { [weak self] in self?.togglePreview() }
        gridView.grid.onRefresh = { [weak self] in self?.load(self?.currentURL ?? URL(fileURLWithPath: "/")) }
        gridView.grid.onRename = { [weak self] in self?.renameSelection() }
        gridView.grid.menuBuilder = { [weak self] in self?.contextMenu() ?? NSMenu() }
        gridView.grid.onContextIndex = { [weak self] i in
            guard let self, i >= 0, i < self.entries.count, self.entries[i].isDir else {
                self?.contextFolder = nil; return
            }
            self.contextFolder = self.entries[i].url
        }
        gridView.grid.onDrop = { [weak self] urls, folder in
            self?.performDrop(urls: urls, into: folder) ?? false
        }

        sidebar.onDrop = { [weak self] urls, folder in
            self?.performDrop(urls: urls, into: folder) ?? false
        }
        sidebar.menuBuilder = { [weak self] in self?.contextMenu() ?? NSMenu() }
        sidebar.onContextNode = { [weak self] url, isFav in
            self?.contextFolder = url
            self?.contextIsFavorite = isFav
        }

        load(currentURL)
    }

    /// 系统菜单栏：提供剪切/复制/粘贴与视图切换的快捷键
    func installMainMenu() {
        let main = NSMenu()

        let appItem = NSMenuItem()
        main.addItem(appItem)
        let appMenu = NSMenu()
        appMenu.addItem(item("关于 Win98 资源管理器", #selector(about), key: ""))
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(NSMenuItem(title: "隐藏", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h"))
        appMenu.addItem(NSMenuItem(title: "退出", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        appItem.submenu = appMenu

        let editItem = NSMenuItem()
        main.addItem(editItem)
        let edit = NSMenu(title: "编辑")
        edit.addItem(item("剪切", #selector(cutSelection), key: "x"))
        edit.addItem(item("复制", #selector(copySelection), key: "c"))
        edit.addItem(item("粘贴", #selector(pasteSelection), key: "v"))
        edit.addItem(NSMenuItem.separator())
        edit.addItem(item("全选", #selector(menuSelectAll), key: "a"))
        editItem.submenu = edit

        let goItem = NSMenuItem()
        main.addItem(goItem)
        let go = NSMenu(title: "转到")
        go.addItem(item("后退", #selector(menuBack), key: "["))
        go.addItem(item("前进", #selector(menuForward), key: "]"))
        go.addItem(item("向上", #selector(menuUp), key: "\u{1e}"))
        go.addItem(NSMenuItem.separator())
        go.addItem(item("快速预览", #selector(menuPreview), key: "y"))
        goItem.submenu = go

        let viewItem = NSMenuItem()
        main.addItem(viewItem)
        let view = NSMenu(title: "查看")
        view.addItem(item("大图标", #selector(menuBigIcons), key: "1"))
        view.addItem(item("列表", #selector(menuList), key: "2"))
        view.addItem(item("详细信息", #selector(menuDetails), key: "3"))
        view.addItem(NSMenuItem.separator())
        view.addItem(item("刷新", #selector(menuRefresh), key: "r"))
        viewItem.submenu = view

        NSApp.mainMenu = main
    }

    func show() {
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    // MARK: 导航

    private func navigate(to url: URL) {
        guard url.standardizedFileURL != currentURL.standardizedFileURL else { return }
        backStack.append(currentURL)
        forwardStack.removeAll()
        load(url)
    }

    private func goBack() {
        guard let prev = backStack.popLast() else { return }
        forwardStack.append(currentURL)
        load(prev)
    }

    private func goForward() {
        guard let next = forwardStack.popLast() else { return }
        backStack.append(currentURL)
        load(next)
    }

    private func goUp() {
        let parent = currentURL.deletingLastPathComponent()
        guard parent.path != currentURL.path else { return }
        navigate(to: parent)
    }

    private func goToPath(_ text: String) {
        var p = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if p.hasPrefix("~") { p = (p as NSString).expandingTildeInPath }
        guard !p.isEmpty else { return }
        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: p, isDirectory: &isDir), isDir.boolValue else {
            NSSound.beep()
            refreshAddress()
            return
        }
        navigate(to: URL(fileURLWithPath: p))
    }

    private func toggleSidebar() {
        sidebarVisible.toggle()
        root.sidebar.isHidden = !sidebarVisible
        root.needsLayout = true
        root.layoutSubtreeIfNeeded()
    }

    // MARK: 读取目录

    private func load(_ url: URL) {
        currentURL = url
        contextFolder = nil
        generation += 1
        let gen = generation

        let title: String
        if url.path == "/" { title = "我的电脑" } else { title = url.lastPathComponent }
        root.titleBar.title = title
        root.statusBar.rightText = title
        refreshAddress()
        root.sidebar.tree.showHidden = showHidden
        root.sidebar.currentURL = url
        // 立即清空旧目录内容，避免新路径下短暂显示上一个文件夹的文件
        entries = []
        refreshViews()
        updateStatus()

        let keys: [URLResourceKey] = [.isDirectoryKey, .isPackageKey, .fileSizeKey, .contentModificationDateKey]
        let options: FileManager.DirectoryEnumerationOptions = showHidden ? [] : [.skipsHiddenFiles]

        DispatchQueue.global(qos: .userInitiated).async {
            var found: [Entry] = []
            if let list = try? FileManager.default.contentsOfDirectory(at: url,
                                                                      includingPropertiesForKeys: keys,
                                                                      options: options) {
                let fm = FileManager.default
                for u in list {
                    let v = try? u.resourceValues(forKeys: Set(keys))
                    var isDir = v?.isDirectory ?? false
                    if v?.isPackage == true { isDir = false }
                    var type = "文件"
                    if isDir {
                        type = "文件夹"
                    } else if let t = UTType(filenameExtension: u.pathExtension) {
                        type = t.localizedDescription ?? "文件"
                    }
                    found.append(Entry(url: u,
                                       name: u.lastPathComponent,
                                       isDir: isDir,
                                       size: isDir ? nil : v?.fileSize.map { Int64($0) },
                                       type: type,
                                       date: v?.contentModificationDate))
                }
                _ = fm
            }
            DispatchQueue.main.async {
                guard gen == self.generation else { return }
                self.entries = found
                self.applySort()
                self.refreshViews()
                self.root.sidebar.noteChildren(of: self.currentURL,
                                               hasSubdir: self.entries.contains { $0.isDir })
                self.updateStatus()
                self.updateToolbar()
            }
        }
    }

    private func refreshAddress() {
        root.addrBar.path = (currentURL.path as NSString).abbreviatingWithTildeInPath
    }

    private func sortBy(_ column: Int) {
        if sortColumn == column { sortAsc.toggle() } else { sortColumn = column; sortAsc = true }
        root.listView.header.sortColumn = sortColumn
        root.listView.header.sortAsc = sortAsc
        applySort()
        refreshViews()
    }

    private func applySort() {
        entries.sort { a, b in
            if a.isDir != b.isDir { return a.isDir }
            let r: ComparisonResult
            switch sortColumn {
            case 1:
                r = (a.size ?? 0) == (b.size ?? 0)
                    ? a.name.localizedStandardCompare(b.name)
                    : ((a.size ?? 0) < (b.size ?? 0) ? .orderedAscending : .orderedDescending)
            case 2:
                r = a.type.localizedStandardCompare(b.type)
            case 3:
                let da = a.date ?? .distantPast, db = b.date ?? .distantPast
                r = da == db ? a.name.localizedStandardCompare(b.name)
                             : (da < db ? .orderedAscending : .orderedDescending)
            default:
                r = a.name.localizedStandardCompare(b.name)
            }
            return sortAsc ? r == .orderedAscending : r == .orderedDescending
        }
    }

    private func updateStatus() {
        let n = entries.count
        let sel = selectedEntries.count
        var s = "\(n) 个对象"
        if sel > 0 { s += "，已选定 \(sel) 个对象" }
        root.statusBar.leftText = s
    }

    private func updateToolbar() {
        var items = root.toolBar.items
        for i in items.indices {
            if items[i].id == "back" { items[i].enabled = !backStack.isEmpty }
            if items[i].id == "forward" { items[i].enabled = !forwardStack.isEmpty }
            if items[i].id == "up" { items[i].enabled = currentURL.path != "/" }
        }
        root.toolBar.items = items
    }

    // MARK: 文件操作

    private var selectedEntries: [Entry] {
        switch viewMode {
        case .details:
            return root.listView.tableView.selectedRowIndexes.compactMap { $0 < entries.count ? entries[$0] : nil }
        default:
            let sel = root.gridView.grid.selected
            return entries.filter { sel.contains($0.url) }
        }
    }

    /// 刷新列表与图标视图，并保持滚动条同步
    private func refreshViews() {
        root.listView.reload()
        root.gridView.grid.entries = entries
        root.gridView.reload()
    }

    private func open(_ entry: Entry) {
        if entry.isDir {
            navigate(to: entry.url)
        } else {
            NSWorkspace.shared.open(entry.url)
        }
    }

    // MARK: 视图模式

    private func cycleViewMode() {
        switch viewMode {
        case .details: setViewMode(.icons)
        case .icons: setViewMode(.list)
        case .list: setViewMode(.details)
        }
    }

    private func setViewMode(_ mode: ViewMode) {
        let keep = Set(selectedEntries.map { $0.url })
        viewMode = mode
        root.mode = mode
        root.listView.isHidden = (mode != .details)
        root.gridView.isHidden = (mode == .details)
        root.gridView.grid.mode = (mode == .icons) ? .big : .list
        root.gridView.grid.entries = entries
        root.gridView.grid.selected = keep
        root.needsLayout = true
        root.layoutSubtreeIfNeeded()

        if mode == .details {
            let idx = entries.enumerated().filter { keep.contains($0.element.url) }.map { $0.offset }
            root.listView.tableView.selectRowIndexes(IndexSet(idx), byExtendingSelection: false)
        }
        root.gridView.reload()
        if mode != .details { window.makeFirstResponder(root.gridView.grid) }
        updateStatus()
    }

    // MARK: 剪贴板与拖放

    private var isEditingText: Bool { window.firstResponder is NSTextView }

    @objc private func cutSelection() { writeClipboard(cut: true) }
    @objc private func copySelection() { writeClipboard(cut: false) }

    private func writeClipboard(cut: Bool) {
        guard !isEditingText else { return }
        let urls = selectedEntries.map { $0.url }
        guard !urls.isEmpty else { return }
        clipURLs = urls
        clipIsCut = cut
        let pb = NSPasteboard.general
        pb.clearContents()
        pb.writeObjects(urls as [NSURL])
    }

    @objc private func pasteSelection() {
        guard !isEditingText else { return }
        var urls = clipURLs
        var cut = clipIsCut
        if urls.isEmpty {
            urls = (NSPasteboard.general.readObjects(forClasses: [NSURL.self],
                                                      options: [.urlReadingFileURLsOnly: true]) as? [URL]) ?? []
            cut = false
        }
        guard !urls.isEmpty else { NSSound.beep(); return }
        let dest = contextFolder ?? currentURL
        contextFolder = nil
        var done = 0
        for src in urls where src.standardizedFileURL != dest.standardizedFileURL {
            let target = uniqueDestination(for: src, in: dest)
            do {
                if cut { try FileManager.default.moveItem(at: src, to: target) }
                else { try FileManager.default.copyItem(at: src, to: target) }
                done += 1
            } catch { NSSound.beep() }
        }
        if cut && done > 0 { clipURLs = [] }
        if done > 0 { refreshViews() }
    }

    @discardableResult
    private func performDrop(urls: [URL], into folder: URL?) -> Bool {
        let dest = folder ?? currentURL
        var isDir: ObjCBool = false
        guard FileManager.default.fileExists(atPath: dest.path, isDirectory: &isDir), isDir.boolValue else {
            NSSound.beep(); return false
        }
        let d = dest.standardizedFileURL
        var done = 0
        for src in urls {
            let s = src.standardizedFileURL
            if s == d { continue }
            if d.path.hasPrefix(s.path + "/") { NSSound.beep(); continue }
            let target = uniqueDestination(for: src, in: dest)
            do {
                if sameVolume(src, dest) { try FileManager.default.moveItem(at: src, to: target) }
                else { try FileManager.default.copyItem(at: src, to: target) }
                done += 1
            } catch { NSSound.beep() }
        }
        if done > 0 { refreshViews() }
        return done > 0
    }

    private func sameVolume(_ a: URL, _ b: URL) -> Bool {
        let va = (try? a.resourceValues(forKeys: [.volumeIdentifierKey]).volumeIdentifier).map { "\($0)" }
        let vb = (try? b.resourceValues(forKeys: [.volumeIdentifierKey]).volumeIdentifier).map { "\($0)" }
        guard let va, let vb else { return true }
        return va == vb
    }

    private func uniqueDestination(for src: URL, in dir: URL) -> URL {
        let fm = FileManager.default
        var candidate = dir.appendingPathComponent(src.lastPathComponent)
        guard fm.fileExists(atPath: candidate.path) else { return candidate }
        let base = src.deletingPathExtension().lastPathComponent
        let ext = src.pathExtension
        var n = 2
        while fm.fileExists(atPath: candidate.path) {
            let name = ext.isEmpty ? "\(base) (\(n))" : "\(base) (\(n)).\(ext)"
            candidate = dir.appendingPathComponent(name)
            n += 1
        }
        return candidate
    }

    private func openSelection() {
        guard let first = selectedEntries.first else { return }
        open(first)
    }

    @objc private func newFolder() {
        let base = contextFolder ?? currentURL
        var n = 1
        var url = base.appendingPathComponent("新建文件夹")
        while FileManager.default.fileExists(atPath: url.path) {
            n += 1
            url = base.appendingPathComponent("新建文件夹 (\(n))")
        }
        do {
            try FileManager.default.createDirectory(at: url, withIntermediateDirectories: false)
            load(currentURL)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { [weak self] in
                guard let self else { return }
                if let idx = self.entries.firstIndex(where: { $0.url == url }) {
                    self.root.listView.tableView.selectRowIndexes(IndexSet(integer: idx), byExtendingSelection: false)
                    self.root.listView.tableView.scrollRowToVisible(idx)
                }
            }
        } catch {
            NSSound.beep()
        }
    }

    @objc private func renameSelection() {
        guard let e = selectedEntries.first else { return }
        let alert = NSAlert()
        alert.messageText = "重命名"
        alert.informativeText = e.name
        alert.addButton(withTitle: "确定")
        alert.addButton(withTitle: "取消")
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 22))
        field.font = Theme.font(11)
        field.stringValue = e.name
        alert.accessoryView = field
        alert.window.initialFirstResponder = field
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let newName = field.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !newName.isEmpty, newName != e.name else { return }
        let dest = e.url.deletingLastPathComponent().appendingPathComponent(newName)
        do {
            try FileManager.default.moveItem(at: e.url, to: dest)
            load(currentURL)
        } catch {
            NSSound.beep()
        }
    }

    @objc private func trashSelection() {
        let targets = selectedEntries
        guard !targets.isEmpty else { return }
        for e in targets {
            try? FileManager.default.trashItem(at: e.url, resultingItemURL: nil)
        }
        load(currentURL)
    }

    @objc private func copyPaths() {
        let paths = selectedEntries.map { $0.url.path }.joined(separator: "\n")
        guard !paths.isEmpty else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(paths, forType: .string)
    }

    @objc private func revealInFinder() {
        guard let e = selectedEntries.first else { return }
        NSWorkspace.shared.activateFileViewerSelecting([e.url])
    }

    // MARK: 菜单

    private func item(_ title: String, _ action: Selector, key: String = "", enabled: Bool = true) -> NSMenuItem {
        let i = NSMenuItem(title: title, action: action, keyEquivalent: key)
        i.target = self
        i.isEnabled = enabled
        return i
    }

    private func fileMenu() -> NSMenu {
        let m = NSMenu()
        m.addItem(item("打开", #selector(menuOpen), key: ""))
        m.addItem(item("新建文件夹", #selector(newFolder), key: ""))
        m.addItem(NSMenuItem.separator())
        m.addItem(item("重命名", #selector(renameSelection), key: ""))
        m.addItem(item("移到废纸篓", #selector(trashSelection), key: ""))
        m.addItem(NSMenuItem.separator())
        m.addItem(item("关闭", #selector(menuClose), key: "w"))
        return m
    }

    private func editMenu() -> NSMenu {
        let m = NSMenu()
        let hasSel = !selectedEntries.isEmpty
        m.addItem(item("剪切", #selector(cutSelection), key: "", enabled: hasSel))
        m.addItem(item("复制", #selector(copySelection), key: "", enabled: hasSel))
        m.addItem(item("粘贴", #selector(pasteSelection), key: "", enabled: (NSPasteboard.general.types?.isEmpty == false)))
        m.addItem(NSMenuItem.separator())
        m.addItem(item("全选", #selector(menuSelectAll), key: "a"))
        m.addItem(item("复制路径", #selector(copyPaths), key: ""))
        m.addItem(NSMenuItem.separator())
        m.addItem(item("在 Finder 中显示", #selector(revealInFinder), key: "",
                       enabled: !selectedEntries.isEmpty))
        return m
    }

    private func viewMenu() -> NSMenu {
        let m = NSMenu()
        m.addItem(item("刷新", #selector(menuRefresh), key: "r"))
        m.addItem(NSMenuItem.separator())
        let hidden = item("显示隐藏文件", #selector(toggleHidden), key: "")
        hidden.state = showHidden ? .on : .off
        m.addItem(hidden)
        m.addItem(NSMenuItem.separator())
        let big = item("大图标", #selector(menuBigIcons), key: "")
        big.state = viewMode == .icons ? .on : .off
        m.addItem(big)
        let list = item("列表", #selector(menuList), key: "")
        list.state = viewMode == .list ? .on : .off
        m.addItem(list)
        let details = item("详细信息", #selector(menuDetails), key: "")
        details.state = viewMode == .details ? .on : .off
        m.addItem(details)
        return m
    }

    private func favMenu() -> NSMenu {
        let m = NSMenu()
        m.addItem(item("添加当前文件夹到收藏夹", #selector(addCurrentToFavorites), key: "d"))
        let favs = UserDefaults.standard.stringArray(forKey: "Favorites") ?? []
        if !favs.isEmpty {
            m.addItem(NSMenuItem.separator())
            for p in favs {
                let i = NSMenuItem(title: URL(fileURLWithPath: p).lastPathComponent,
                                   action: #selector(goFav(_:)), keyEquivalent: "")
                i.target = self
                i.representedObject = URL(fileURLWithPath: p)
                m.addItem(i)
            }
        }
        return m
    }

    private func toolMenu() -> NSMenu {
        let m = NSMenu()
        m.addItem(item("在终端中打开", #selector(openTerminal), key: ""))
        m.addItem(NSMenuItem.separator())
        m.addItem(item("文件夹选项", #selector(noop), key: "", enabled: false))
        return m
    }

    private func helpMenu() -> NSMenu {
        let m = NSMenu()
        m.addItem(item("关于 Win98 资源管理器", #selector(about), key: ""))
        return m
    }

    private var contextIsVolume: Bool {
        let target = (contextFolder ?? currentURL).standardizedFileURL
        guard target.path.hasPrefix("/Volumes/") else { return false }
        // /Volumes/Macintosh HD 是启动盘的符号链接，不能“弹出”
        return target.resolvingSymlinksInPath().path != "/"
    }

    @objc private func ejectVolume() {
        let target = (contextFolder ?? currentURL).standardizedFileURL
        guard contextIsVolume else { return }
        let parent = currentURL.path
        do {
            try NSWorkspace.shared.unmountAndEjectDevice(at: target)
        } catch {
            NSSound.beep()
            return
        }
        root.sidebar.reloadAll()
        if parent == target.path || parent.hasPrefix(target.path + "/") {
            load(URL(fileURLWithPath: "/"))
        } else {
            load(currentURL)
        }
    }

    private func contextMenu() -> NSMenu {
        let m = NSMenu()
        let hasSel = !selectedEntries.isEmpty
        m.addItem(item("打开", #selector(menuOpen), key: "", enabled: hasSel))
        m.addItem(item("在 Finder 中显示", #selector(revealInFinder), key: "", enabled: hasSel))
        m.addItem(NSMenuItem.separator())
        m.addItem(item("剪切", #selector(cutSelection), key: "", enabled: hasSel))
        m.addItem(item("复制", #selector(copySelection), key: "", enabled: hasSel))
        m.addItem(item("粘贴", #selector(pasteSelection), key: "", enabled: true))
        m.addItem(NSMenuItem.separator())
        m.addItem(item("新建文件夹", #selector(newFolder), key: ""))
        m.addItem(item("重命名", #selector(renameSelection), key: "", enabled: hasSel))
        m.addItem(item("移到废纸篓", #selector(trashSelection), key: "", enabled: hasSel))
        if contextIsVolume {
            m.addItem(NSMenuItem.separator())
            m.addItem(item("弹出", #selector(ejectVolume), key: ""))
        }
        m.addItem(NSMenuItem.separator())
        m.addItem(item("添加到收藏夹", #selector(addToFavorites), key: ""))
        if contextIsFavorite {
            m.addItem(item("从收藏夹中移除", #selector(removeFromFavorites), key: ""))
        }
        m.addItem(item("在终端中打开", #selector(openTerminal), key: ""))
        return m
    }

    @objc private func menuBack() { goBack() }
    @objc private func menuForward() { goForward() }
    @objc private func menuUp() { goUp() }
    @objc private func menuPreview() { togglePreview() }
    @objc private func menuOpen() { openSelection() }
    @objc private func menuClose() { window.performClose(nil) }
    @objc private func menuSelectAll() {
        guard !isEditingText else { return }
        if viewMode == .details {
            root.listView.tableView.selectAll(nil)
        } else {
            root.gridView.grid.selected = Set(entries.map { $0.url })
        }
        updateStatus()
    }

    @objc private func menuBigIcons() { setViewMode(.icons) }
    @objc private func menuList() { setViewMode(.list) }
    @objc private func menuDetails() { setViewMode(.details) }
    @objc private func menuRefresh() { load(currentURL) }
    @objc private func toggleHidden() {
        showHidden.toggle()
        load(currentURL)
    }
    @objc private func noop() {}
    @objc private func addCurrentToFavorites() {
        root.sidebar.addFavorite(currentURL)
    }

    @objc private func addToFavorites() {
        root.sidebar.addFavorite(contextFolder ?? currentURL)
    }

    @objc private func removeFromFavorites() {
        root.sidebar.removeFavorite(contextFolder ?? currentURL)
    }

    @objc private func goFav(_ sender: NSMenuItem) {
        if let u = sender.representedObject as? URL { navigate(to: u) }
    }
    @objc private func openTerminal() {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/usr/bin/open")
        p.arguments = ["-a", "Terminal", currentURL.path]
        try? p.run()
    }
    @objc private func about() {
        let alert = NSAlert()
        alert.messageText = "Win98 资源管理器"
        alert.informativeText = "macOS 上的 Windows 98 风格文件管理器原型。\n原生 Swift / AppKit，无第三方依赖。"
        alert.addButton(withTitle: "好")
        alert.runModal()
    }

    var currentPath: String { currentURL.path }

    // MARK: 空格预览（Quick Look）

    private var previewURLs: [URL] {
        if viewMode == .details {
            let rows = root.listView.tableView.selectedRowIndexes.sorted()
            return rows.compactMap { $0 < entries.count ? entries[$0].url : nil }
        }
        let sel = root.gridView.grid.selected
        return entries.map(\.url).filter { sel.contains($0) }
    }

    func togglePreview() {
        guard let panel = QLPreviewPanel.shared() else { return }
        if panel.isVisible {
            panel.orderOut(nil)
            return
        }
        guard !previewURLs.isEmpty else { return }
        panel.dataSource = self
        panel.delegate = self
        panel.makeKeyAndOrderFront(nil)
        panel.reloadData()
    }

    @objc func numberOfPreviewItems(in panel: QLPreviewPanel!) -> Int { previewURLs.count }

    @objc(previewPanel:previewItemAtIndex:) func previewPanel(_ panel: QLPreviewPanel!, previewItemAt index: Int) -> QLPreviewItem! {
        let urls = previewURLs
        guard index >= 0 && index < urls.count else { return nil }
        return urls[index] as NSURL
    }

    // MARK: 自检

    func runSelfTest() {
        let fm = FileManager.default
        let base = fm.temporaryDirectory.appendingPathComponent("xp-selftest-\(Int(Date().timeIntervalSince1970))")
        try? fm.createDirectory(at: base, withIntermediateDirectories: true)
        for i in 0..<3 {
            fm.createFile(atPath: base.appendingPathComponent("f\(i).txt").path, contents: Data("x".utf8))
        }
        try? fm.createDirectory(at: base.appendingPathComponent("sub"), withIntermediateDirectories: true)

        var results: [String] = []
        navigate(to: base)

        func finish() {
            print("SELFTEST " + results.joined(separator: " | "))
            try? FileManager.default.removeItem(at: base)
            NSApp.terminate(nil)
        }

        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
            results.append("列目录 \(self.entries.count == 4 ? "OK" : "FAIL(\(self.entries.count))")")
            self.setViewMode(.icons)
            results.append("大图标 \(self.root.gridView.grid.entries.count == 4 ? "OK" : "FAIL")")
            self.setViewMode(.list)
            results.append("列表 \(self.root.gridView.grid.entries.count == 4 ? "OK" : "FAIL")")
            self.setViewMode(.details)
            self.sortBy(1)
            results.append("排序 \(self.entries.first?.isDir == true ? "OK" : "FAIL")")
            self.newFolder()

            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                let made = FileManager.default.fileExists(atPath: base.appendingPathComponent("新建文件夹").path)
                results.append("新建文件夹 \(made ? "OK" : "FAIL")")

                if let idx = self.entries.firstIndex(where: { $0.name == "f0.txt" }) {
                    self.root.listView.tableView.selectRowIndexes(IndexSet(integer: idx), byExtendingSelection: false)
                    self.trashSelection()
                }

                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                    let gone = !FileManager.default.fileExists(atPath: base.appendingPathComponent("f0.txt").path)
                    results.append("移到废纸篓 \(gone ? "OK" : "FAIL")")

                    // 复制 / 剪切 / 粘贴
                    if let idx = self.entries.firstIndex(where: { $0.name == "f1.txt" }) {
                        self.root.listView.tableView.selectRowIndexes(IndexSet(integer: idx), byExtendingSelection: false)
                        self.copySelection()
                        self.pasteSelection()
                    }
                    let copied = FileManager.default.fileExists(atPath: base.appendingPathComponent("f1 (2).txt").path)
                    results.append("复制粘贴 \(copied ? "OK" : "FAIL")")

                    if let idx = self.entries.firstIndex(where: { $0.name == "f2.txt" }) {
                        self.root.listView.tableView.selectRowIndexes(IndexSet(integer: idx), byExtendingSelection: false)
                        self.cutSelection()
                        self.pasteSelection()
                    }
                    let cutGone = !FileManager.default.fileExists(atPath: base.appendingPathComponent("f2.txt").path)
                    let cutMade = FileManager.default.fileExists(atPath: base.appendingPathComponent("f2 (2).txt").path)
                    results.append("剪切粘贴 \(cutGone && cutMade ? "OK" : "FAIL")")

                    // 拖放：把 f1.txt 拖进 sub 文件夹
                    let src = base.appendingPathComponent("f1.txt")
                    self.performDrop(urls: [src], into: base.appendingPathComponent("sub"))
                    let dropped = FileManager.default.fileExists(atPath: base.appendingPathComponent("sub/f1.txt").path)
                    results.append("拖放移动 \(dropped ? "OK" : "FAIL")")

                    self.goBack()

                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                        results.append("后退 \(self.currentURL.path != base.path ? "OK" : "FAIL")")
                        let before = self.currentURL
                        self.goUp()

                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                            let expect = before.deletingLastPathComponent().path
                            results.append("向上 \(self.currentURL.path == expect ? "OK" : "FAIL")")
                            finish()
                        }
                    }
                }
            }
        }
    }

    // MARK: 调试入口

    func debugStaleCell() {
        let tv = root.listView.tableView
        if let rv = tv.rowView(atRow: 0, makeIfNecessary: false) {
            for case let c as XPCell in rv.subviews where c.identifier?.rawValue == "name" {
                c.configureSelected(true)
            }
            rv.needsDisplay = true
            rv.display()
            print("stale-cell test done")
        }
    }

    func debugClearSelection() {
        root.listView.tableView.deselectAll(nil)
        root.gridView.grid.selected = []
        print("cleared: sel rows =", root.listView.tableView.selectedRowIndexes.count)
    }

    func debugDemoTree() {
        TreeBodyView.demoRoot = currentURL
        root.sidebar.reloadAll()
    }

    func debugHideVolumes() {
        TreeBodyView.hideVolumes = true
        root.sidebar.reloadAll()
    }

    func debugEject() {
        contextFolder = currentURL
        ejectVolume()
        print("eject-done exists:", FileManager.default.fileExists(atPath: currentURL.path))
    }

    func debugAddFavorite() {
        addCurrentToFavorites()
        print("fav-after:", UserDefaults.standard.stringArray(forKey: "Favorites") ?? [])
    }

    func debugPreview() {
        togglePreview()
        print("preview visible:", QLPreviewPanel.shared()?.isVisible ?? false,
              "items:", previewURLs.count)
    }

    func debugNavigate(_ path: String) {
        navigate(to: URL(fileURLWithPath: (path as NSString).expandingTildeInPath))
    }

    func debugSort(_ column: Int, ascending: Bool) {
        sortColumn = column
        sortAsc = ascending
        applySort()
        root.listView.header.sortColumn = sortColumn
        root.listView.header.sortAsc = sortAsc
        root.listView.reload()
    }

    func debugView(_ name: String) {
        switch name {
        case "icons": setViewMode(.icons)
        case "list": setViewMode(.list)
        default: setViewMode(.details)
        }
    }

    func debugSelect(_ rows: [Int]) {
        let valid = rows.filter { $0 >= 0 && $0 < entries.count }
        if viewMode == .details {
            root.listView.tableView.selectRowIndexes(IndexSet(valid), byExtendingSelection: false)
        } else {
            root.gridView.grid.selected = Set(valid.map { entries[$0].url })
        }
        updateStatus()
    }

    // MARK: 表格

    func numberOfRows(in tableView: NSTableView) -> Int { entries.count }

    func tableView(_ tableView: NSTableView, rowViewForRow row: Int) -> NSTableRowView? {
        XPTableRow()
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        guard let col = tableColumn, row < entries.count else { return nil }
        let e = entries[row]
        let selected = tableView.selectedRowIndexes.contains(row)
        let cell = tableView.makeView(withIdentifier: col.identifier, owner: self) as? XPCell
            ?? XPCell(identifier: col.identifier)
        switch col.identifier.rawValue {
        case "name":
            cell.configure(text: e.name, image: e.isDir ? Icon.folder(16) : Icon.fileIcon(for: e.url), selected: selected)
        case "size":
            cell.configure(text: e.size.map { fmt($0) } ?? "", image: nil, selected: false, alignment: .right)
        case "type":
            cell.configure(text: e.type, image: nil, selected: false)
        case "date":
            cell.configure(text: e.date.map { sizeFormatter.string(from: $0) } ?? "", image: nil, selected: false)
        default:
            break
        }
        return cell
    }

    // MARK: 表格拖放

    func tableView(_ tableView: NSTableView, pasteboardWriterForRow row: Int) -> NSPasteboardWriting? {
        guard row < entries.count else { return nil }
        let item = NSPasteboardItem()
        item.setString(entries[row].url.absoluteString, forType: .fileURL)
        return item
    }

    func tableView(_ tableView: NSTableView, draggingSession session: NSDraggingSession,
                   sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation {
        .every
    }

    func tableView(_ tableView: NSTableView, validateDrop info: NSDraggingInfo,
                   proposedRow row: Int, proposedDropOperation dropOperation: NSTableView.DropOperation) -> NSDragOperation {
        guard info.draggingPasteboard.canReadObject(forClasses: [NSURL.self],
                                                     options: [.urlReadingFileURLsOnly: true]) else { return [] }
        if row < entries.count && entries[row].isDir {
            tableView.setDropRow(row, dropOperation: .on)
        } else {
            tableView.setDropRow(row, dropOperation: .above)
        }
        return .copy
    }

    func tableView(_ tableView: NSTableView, acceptDrop info: NSDraggingInfo,
                   row: Int, dropOperation: NSTableView.DropOperation) -> Bool {
        let urls = (info.draggingPasteboard.readObjects(forClasses: [NSURL.self],
                                                        options: [.urlReadingFileURLsOnly: true]) as? [URL]) ?? []
        guard !urls.isEmpty else { return false }
        var target: URL? = nil
        if dropOperation == .on, row < entries.count, entries[row].isDir { target = entries[row].url }
        return performDrop(urls: urls, into: target)
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        if QLPreviewPanel.shared()?.isVisible == true { QLPreviewPanel.shared()?.reloadData() }
        updateStatus()
        let tv = root.listView.tableView
        var seen = 0
        tv.enumerateAvailableRowViews { rowView, row in
            seen += 1
            let sel = tv.selectedRowIndexes.contains(row)
            for case let cell as XPCell in rowView.subviews {
                cell.configureSelected(sel)
            }
        }
        if CommandLine.arguments.contains("--dump") {
            print("selection changed: rows=\(tv.selectedRowIndexes.count) realized=\(seen)")
        }
    }

    func debugDump() {
        let tv = root.listView.tableView
        print("window:", window.frame)
        print("root:", root.bounds)
        print("sidebar:", root.sidebar.frame)
        print("list:", root.listView.frame)
        print("header:", root.listView.header.frame)
        var x: CGFloat = 2
        for c in tv.tableColumns {
            print(String(format: "col %@ x=%.1f w=%.1f right=%.1f", c.identifier.rawValue, x, c.width, x + c.width))
            x += c.width
        }
        print("header frame:", root.listView.header.frame, "widths:", root.listView.header.widths)
        print(root.sidebar.tree.debugInfo())
        if let ev = NSEvent.mouseEvent(with: .rightMouseDown,
                                       location: NSPoint(x: 500, y: 300),
                                       modifierFlags: [], timestamp: 0,
                                       windowNumber: window.windowNumber, context: nil,
                                       eventNumber: 0, clickCount: 1, pressure: 0) {
            let m = root.listView.tableView.menu(for: ev)
            print("context:", m?.items.map { $0.title } ?? [])
            let t = root.sidebar.tree.menu(for: ev)
            print("tree-context:", t?.items.map { $0.title } ?? [])
        }
        print("table frame:", tv.frame, "rows:", entries.count)
        if tv.numberOfRows > 0 {
            let r = tv.rect(ofRow: 0)
            let pt = NSPoint(x: r.midX, y: r.midY)
            print("hitTest at row0:", String(describing: type(of: tv.hitTest(pt) ?? NSView())))
        }
        print("currentURL:", currentURL.path)
        print("firstNames:", entries.prefix(4).map { $0.name })
        tv.enumerateAvailableRowViews { rowView, row in
            if row < 3 {
                let colors = rowView.subviews.compactMap { ($0 as? XPCell)?.debugColor }
                print("row \(row) selected=\(tv.selectedRowIndexes.contains(row)) colors=\(colors)")
            }
        }
    }

    private func fmt(_ n: Int64) -> String {
        if n < 1024 { return "\(n) 字节" }
        let kb = Double(n) / 1024
        if kb < 1024 { return String(format: "%.0f KB", kb) }
        let mb = kb / 1024
        if mb < 1024 { return String(format: "%.1f MB", mb) }
        return String(format: "%.2f GB", mb / 1024)
    }
}
