import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    let controller: ExplorerController
    init(controller: ExplorerController) { self.controller = controller }

    func applicationDidFinishLaunching(_ notification: Notification) {
        controller.show()
        if CommandLine.arguments.contains("--selftest") {
            controller.runSelfTest()
        }
        if let idx = CommandLine.arguments.firstIndex(of: "--snapshot"),
           idx + 1 < CommandLine.arguments.count {
            let out = CommandLine.arguments[idx + 1]
            let a = CommandLine.arguments
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.7) {
                if let p = a.firstIndex(of: "--path"), p + 1 < a.count {
                    self.controller.debugNavigate(a[p + 1])
                }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.7) {
                    if let s = a.firstIndex(of: "--sort"), s + 2 < a.count,
                       let col = Int(a[s + 1]), let asc = Bool(a[s + 2]) {
                        self.controller.debugSort(col, ascending: asc)
                    }
                    if let v = a.firstIndex(of: "--view"), v + 1 < a.count {
                        self.controller.debugView(a[v + 1])
                    }
                    if let s = a.firstIndex(of: "--select"), s + 1 < a.count {
                        let rows = a[s + 1].split(separator: ",").compactMap { Int($0) }
                        self.controller.debugSelect(rows)
                    }
                    if let c = a.firstIndex(of: "--clicks"), c + 1 < a.count {
                        self.controller.debugClicks(a[c + 1].split(separator: ",").map(String.init))
                    }
                    if let c = a.firstIndex(of: "--clickrows"), c + 1 < a.count {
                        self.controller.debugClickRows(a[c + 1].split(separator: ",").compactMap { Int($0) })
                    }
                    if a.contains("--trashprobe") {
                        let p = NSHomeDirectory() + "/.Trash"
                        var out = ""
                        do {
                            let items = try FileManager.default.contentsOfDirectory(atPath: p)
                            out = "OK \(items.count) \(items.prefix(8))"
                        } catch {
                            out = "ERR \(error)"
                        }
                        try? out.write(toFile: "/tmp/moc_trashprobe.txt", atomically: true, encoding: .utf8)
                    }
                    if let c = a.firstIndex(of: "--ctxmenu"), c + 1 < a.count {
                        self.controller.debugMenu(titled: a[c + 1])
                    }
                    if let c = a.firstIndex(of: "--dblrows"), c + 1 < a.count {
                        self.controller.debugDoubleClickRows(a[c + 1].split(separator: ",").compactMap { Int($0) })
                    }
                    if let c = a.firstIndex(of: "--trashnode"), c + 1 < a.count {
                        self.controller.debugMenu(titled: a[c + 1])
                        self.controller.debugTrashNode()
                    }
                    if a.contains("--ctxitems") { self.controller.debugPrintContextMenu() }
                    if a.contains("--demo") { self.controller.debugDemoTree() }
                    if a.contains("--novolumes") { self.controller.debugHideVolumes() }
                    if a.contains("--fav") { self.controller.debugAddFavorite() }
                    if a.contains("--clearsel") { self.controller.debugClearSelection() }
                    if a.contains("--stalecell") { self.controller.debugStaleCell() }
                    if a.contains("--eject") { self.controller.debugEject() }
                    if a.contains("--preview") { self.controller.debugPreview() }
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.9) {
                        if CommandLine.arguments.contains("--dump") {
                            self.controller.debugDump()
                        }
                        self.snapshot(to: out)
                        NSApp.terminate(nil)
                    }
                }
            }
        }
    }

    private func snapshot(to path: String) {
        guard let view = controller.window.contentView else { return }
        let bounds = view.bounds
        guard let rep = view.bitmapImageRepForCachingDisplay(in: bounds) else { return }
        view.cacheDisplay(in: bounds, to: rep)
        if let data = rep.representation(using: .png, properties: [:]) {
            try? data.write(to: URL(fileURLWithPath: path))
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)

let controller = ExplorerController(start: FileManager.default.homeDirectoryForCurrentUser)
controller.installMainMenu()
let delegate = AppDelegate(controller: controller)
app.delegate = delegate
_ = controller // 保持强引用
app.run()
