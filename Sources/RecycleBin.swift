import Foundation

/// 应用自己的回收站。
///
/// 系统的 `~/.Trash` 受 macOS 隐私保护：没有“完全磁盘访问”权限时只能往里放，
/// 读不出来，也没法列出或清空。所以这里放在 Application Support 下，
/// 让左侧“回收站”能真正显示内容、清空和还原（Windows 风格）。
enum RecycleBin {
    static var root: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                            in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory() + "/Library/Application Support")
        return base.appendingPathComponent("My Old Computer/Recycle Bin", isDirectory: true)
    }

    private static let originalsKey = "RecycleBinOriginals"

    static func ensureExists() {
        if !FileManager.default.fileExists(atPath: root.path) {
            try? FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        }
    }

    static func items() -> [URL] {
        let list = (try? FileManager.default.contentsOfDirectory(at: root,
                                                                 includingPropertiesForKeys: nil,
                                                                 options: [])) ?? []
        return list.filter { $0.lastPathComponent != ".DS_Store" }
    }

    static var isEmpty: Bool { items().isEmpty }

    /// 回收站文件名 -> 删除前的原始路径
    static func originalPaths() -> [String: String] {
        (UserDefaults.standard.dictionary(forKey: originalsKey) as? [String: String]) ?? [:]
    }

    static func forget(_ name: String) { setOriginal(name, nil) }

    private static func setOriginal(_ name: String, _ path: String?) {
        var d = originalPaths()
        if let path {
            d[name] = path
        } else {
            d.removeValue(forKey: name)
        }
        UserDefaults.standard.set(d, forKey: originalsKey)
    }

    private static func uniqueURL(for name: String) -> URL {
        var candidate = root.appendingPathComponent(name)
        if !FileManager.default.fileExists(atPath: candidate.path) { return candidate }
        let ext = (name as NSString).pathExtension
        let base = (name as NSString).deletingPathExtension
        var i = 2
        while true {
            let n = ext.isEmpty ? "\(base) \(i)" : "\(base) \(i).\(ext)"
            candidate = root.appendingPathComponent(n)
            if !FileManager.default.fileExists(atPath: candidate.path) { return candidate }
            i += 1
        }
    }

    /// 把文件或文件夹移进回收站，并记住原来的位置
    @discardableResult
    static func move(_ url: URL) -> Bool {
        ensureExists()
        let dest = uniqueURL(for: url.lastPathComponent)
        do {
            try FileManager.default.moveItem(at: url, to: dest)
            setOriginal(dest.lastPathComponent, url.path)
            return true
        } catch {
            return false
        }
    }

    static func empty() {
        for u in items() {
            try? FileManager.default.removeItem(at: u)
        }
        UserDefaults.standard.removeObject(forKey: originalsKey)
    }

    /// 还原到原位置；原位置被占用时自动改名，原目录不存在则重建
    static func restore(_ stored: URL) -> Bool {
        let name = stored.lastPathComponent
        guard let originalPath = originalPaths()[name] else { return false }
        let fm = FileManager.default

        var dest = URL(fileURLWithPath: originalPath)
        if fm.fileExists(atPath: dest.path) {
            let ext = dest.pathExtension
            let base = dest.deletingPathExtension().lastPathComponent
            let dir = dest.deletingLastPathComponent()
            var i = 2
            while fm.fileExists(atPath: dest.path) {
                let n = ext.isEmpty ? "\(base) (\(i))" : "\(base) (\(i)).\(ext)"
                dest = dir.appendingPathComponent(n)
                i += 1
            }
        }
        let parent = dest.deletingLastPathComponent()
        if !fm.fileExists(atPath: parent.path) {
            try? fm.createDirectory(at: parent, withIntermediateDirectories: true)
        }
        do {
            try fm.moveItem(at: stored, to: dest)
            setOriginal(name, nil)
            return true
        } catch {
            return false
        }
    }
}
