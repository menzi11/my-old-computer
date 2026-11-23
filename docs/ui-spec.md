# UI specification

Both builds — AppKit on macOS, Win32 on Windows — draw the same interface from
this document. If a value changes here, change it in
`Sources/Theme.swift` and `windows/src/theme.h` together.

## Palette

| Role | Hex | Used for |
|---|---|---|
| Face | `#C0C0C0` | window background, buttons, headers |
| Highlight | `#DFDFDF` | scrollbar track, hover |
| White | `#FFFFFF` | list background, bevel light edge |
| Shadow | `#808080` | bevel dark edge, separators |
| Black | `#000000` | 1 px window outline, text |
| Navy | `#000080` | title bar start, selection |
| Navy light | `#1084D0` | title bar end |
| Green | `#008000` | toolbar arrows, drive LED |
| Dim | `#808080` | disabled text |
| Folder top | `#FFFA66` | folder tab |
| Folder body | `#FAD91A` | folder body |
| Folder edge | `#998000` | folder bottom pixel row |

Selection is navy with white text; the selection rectangle never covers a whole
details row — only the icon and the name text.  Large icons are not put on a
grey tile; only the label gets the navy highlight.

## Metrics (logical pixels at 96 dpi)

| Element | Value |
|---|---|
| Window edge (face margin inside the frame) | 3 |
| Title bar | 22 |
| Menu bar | 20 |
| Toolbar | 32 |
| Address bar | 26 |
| Status bar | 20 |
| Sidebar default width | 200 (min 120) |
| Splitter | 5 |
| Tree / list row | 19 |
| Column header | 20 |
| Scrollbar | 16 (arrows 16) |
| Tree indent per level | 14 |
| Small icon | 16 |
| Large icon | 48 |
| Large-icon cell | 94 × 94 |
| List-view cell | 220 × 19 |
| Details columns | name (flex), size 80, type 100, date 130 |
| Title buttons | 16 × 14, 2 px gap, 4 px from top |
| Minimum window | 700 × 440 |

## Bevels

Every edge is one hard pixel, no gradients, no shadows, no blur.

- **Raised**: white on top/left, `#808080` on bottom/right.
- **Sunken**: `#808080` on top/left, white on bottom/right.
- **Field**: `#808080` outer edge, black inner edge, white on the far
  bottom/right — the 2 px address bar and text input frame.

## Layout

```
3 px face margin
┌ title bar ────────────────────────────────────────────┐
├ menu bar ─────────────────────────────────────────────┤
├ toolbar ──────────────────────────────────────────────┤
├ address bar ──────────────────────────────────────────┤
│ sidebar │ splitter │ file area                        │
├ status bar ───────────────────────────────────────────┤
```

The title bar is a horizontal navy → light-blue gradient with the folder icon,
bold white title and minimize / maximize / close buttons at the right.

## Icons

Windows uses the shell's own image lists (`SHGetFileInfo` + `SHGetImageList`), so
file types look exactly as they do in Explorer; the View menu can switch back to
the hand-drawn set.  macOS draws the icons in `Icons.swift` per extension.

## Behaviour contract

- Directories sort before files in every column; names use a natural
  (number-aware) comparison.
- Details rows draw the icon at x+3, the name at x+22, and highlight only the
  icon and the name text. There is no full-row select: clicks to the right of the
  name text (including the size, type and date columns) do not select the row.
- A single click selects; Ctrl/Command toggles; Shift extends from the anchor;
  dragging on empty space (or on the blank part of a row) draws a rubber band.
  The band selects only the items whose icon or name it actually touches.
- Dragging an icon or a name starts a file drag; dropping it on a folder, on the
  tree or on another application moves (or, across volumes, copies) the items.
- Double-click opens; Enter opens; Backspace goes up; F2 renames; F5 refreshes;
  Delete moves to the Trash / Recycle Bin.
- Sizes use the same units on both platforms (`字节`/KB/MB/GB); dates are
  `yyyy/M/d HH:mm`.
- The address bar always shows the real path (no `~` abbreviation) and accepts a
  path typed by hand.
- The window has one context menu for an item: the *items* come from the real
  Windows shell menu (`IContextMenu`, so every installed extension shows up),
  but the popup is drawn by this app in the Win98 style.  The file list and the
  tree show the same menu for a folder.  The empty area uses our own working
  View / Sort / Refresh / New commands plus the shell's Paste, Properties and
  extensions.
- Tools → Context menu settings lists the app's own entries and the shell's
  entries grouped by where the right-click happened (empty area, folder, file
  without an extension, one group per file extension); each is hidden in that
  group only, in the `[menu-bg]`, `[menu-folder]`, `[menu-file]` and
  `[menu-ext.<ext>]` sections of settings.ini.
- New Folder and New Text Document are flat items of the empty-area menu: one
  click, no submenu.

## Responsiveness contract

Lag is never traded for anything. For menus and hover feedback in particular:

- The highlight follows the pointer with nothing in between. A hover change
  repaints the affected rows on the spot, before any other work runs, and no
  timer, grace period or delay stands between the pointer and the screen.
- A submenu opens the moment its row is hovered and closes the moment the
  pointer leaves that row.
- The one exception is the diagonal path from the opening row to its submenu:
  while the straight line from the pointer's last position in that row to its
  current position meets the submenu, the selection is held so the submenu can
  be reached. Moving straight up or down selects the row under the pointer on
  its first pixel; a corridor must never reach into the neighbouring rows.

## Platform mapping

| Concept | macOS | Windows |
|---|---|---|
| Toolkit | AppKit | Win32 + GDI |
| Text rendering | Core Text, Tahoma 11 pt | GDI, Tahoma at `14.667 × scale` px |
| Delete | `NSFileManager.trashItem` | `SHFileOperation(FO_DELETE, FOF_ALLOWUNDO)` |
| Preview | Quick Look panel | Quick View window (WIC) |
| Reveal | Finder | Explorer `/select` |
| Terminal | Terminal.app | `cmd.exe` |
| Volumes | `/Volumes` | `GetLogicalDriveStrings` |
| Favorites | `UserDefaults` | `%APPDATA%\My Old Computer\settings.ini` |
| Snapshot | `--snapshot` | `--snapshot` |

Anything that cannot be reproduced on a platform — Quick Look thumbnails,
ejecting disk images, `.lnk` semantics — is listed as a limitation in the
README instead of being emulated badly.
