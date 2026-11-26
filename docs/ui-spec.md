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

### Thumbnails (Windows build)

A folder in which more than half of the files are pictures opens in the
big-icon view with each picture in place of its icon, whatever view is the
usual one: cells 112 by 132, the picture in a 96 box, the name under it, and
a frame in the selection's colour around a selected picture. A view chosen in
such a folder is that folder's own: the usual view is neither changed nor
written to the settings, and is back on leaving the folder. A thumbnail is the
shell's (the cache Explorer shares, so the picture is the right way up), made
on threads of the program's own and never on the window's: the icon is shown
until the thumbnail is in, then that cell and no other is drawn again.
Thumbnails asked for by cells that scrolled away are not made; what could not
be made is not asked for again until the folder is listed again. Thumbnails
made are kept for the run, 160 MB of them at most, the longest unused going
first.

## Behaviour contract

- Directories sort before files in every column; names use a natural
  (number-aware) comparison.
- Details rows draw the icon at x+3, the name at x+22, and highlight only the
  icon and the name text. There is no full-row select: clicks to the right of the
  name text (including the size, type and date columns) do not select the row.
- A single click selects; Ctrl/Command toggles (a file, that is - for a folder
  see below); Shift extends from the anchor;
  dragging on empty space (or on the blank part of a row) draws a rubber band.
  The band selects only the items whose icon or name it actually touches.
- Dragging an icon or a name starts a file drag; dropping it on a folder, on the
  tree or on another application moves (or, across volumes, copies) the items.
- Double-click opens; Enter opens; Backspace goes up; F2 renames; F5 refreshes;
  Delete moves to the Trash / Recycle Bin. Ctrl+W closes the window - typed in
  the address box too.
- Ctrl and a click on a folder - in the list, or on its row in the tree -
  opens it in a new window; so does Ctrl and Enter on the one folder
  selected. (Ctrl and a double click is one click more than that: one
  window.) The new window is another instance of this program, started in that
  folder and like this one: the settings as they are now, so the same view and
  the same size, placed a title bar down and to the right of this window. No
  instance opens exactly over the window of another: the step repeats for
  every one already there, and the title bar stays on the monitor. The new
  window comes to the front: the window it was opened from hands it the
  foreground as soon as it is up. A press with Ctrl on a folder still drags
  it, and let go off the folder it opens nothing. A folder joins a selection
  with Shift and a click or with the rubber band, not with Ctrl and a click;
  for a file, Ctrl changes nothing. The context menu's `在新窗口中打开` is
  the same thing.
- A folder can be given a colour from its context menu (Windows build):
  `文件夹颜色` opens eight colours and `默认颜色`. The folder's icon is that
  colour wherever this program shows it - in the list, as a big icon, in the
  tree (and in the sidebar's Favorites): the yellow of the folder becomes the
  colour, its outline and the paper in it stay. With several folders selected
  they all get it. The colour is kept in a file of this program's own
  (`foldercolours.ini`, beside `settings.ini`): nothing is written into the
  folder, and Explorer shows it as it always did. Every window of this program
  shows what any of them coloured (it reads the file again when it comes to
  the front or opens a menu). The item can be switched off in the settings,
  like the other ones of that menu.
- Sizes use the same units on both platforms (`字节`/KB/MB/GB); dates are
  `yyyy/M/d HH:mm`.
- The address bar (Windows build; the macOS build still shows the path as
  text) is Explorer's breadcrumb bar: the way to the current folder as
  buttons, from the left - the machine with its icon and name, the drive as
  the tree names it, then one button per folder, each named as it is on the
  disk. While the way fits, nothing is left out and nothing is abbreviated.
- A click on a button goes to that folder when the button comes back up on it;
  let go somewhere else and nothing happens. The machine's button has nowhere
  to go: it opens the list of its drives. Only the first click of a double
  click counts - the buttons are laid out anew after it, and the second one
  lands wherever that is.
- A `>` follows every button but the current folder's. It lists that folder's
  subfolders, with the row the way goes through highlighted as the tree
  highlights a row. A click on a row, or Enter, goes there; the arrow keys and
  the wheel move through the list, which scrolls when it is long (a drive's
  root, Program Files). The `>` stays pressed while its list is up, a press on
  another `>` opens that one's list in its place (as between menu titles),
  Escape or a press anywhere else closes it, and that press goes no further.
- When the way is too long for the bar, the row still starts at the left and
  has no gap in it, as in Explorer: the machine gives up its name (an icon
  alone), the crumbs that do not fit are stood for by a `...` right after it,
  where they would be, and as many of the last crumbs as fit follow it. The
  current folder's name, when it is too long for the bar even alone, is cut
  with an ellipsis. The `...` lights up under the pointer, opens on a press
  and stays pressed while its list is up; the list holds the crumbs it stands
  for, the drive first, and a click on a row goes there. The blank part at
  the right end is never too small to click.
- A click on the blank part of the bar, F4, Ctrl+L or Alt+D (the label's
  `地址(D)`) puts the whole path in an edit box, selected, to be typed over.
  Enter goes there at once: the typed path stays in the box until the new
  folder's frame is ready, so nothing of the old one flashes in between. Enter
  on the path as it is reads the folder again; clicking elsewhere with the
  path as it is does nothing; Escape goes back to the buttons without going
  anywhere.
- The window is asked where its client area goes every time it is placed,
  whether or not its size changes. (At 100 % scaling the saved size is the
  physical one, so a window created at the size it keeps was never asked, and
  kept the system's title bar above its own.)
- The window has one context menu for an item: the *items* come from the real
  Windows shell menu (`IContextMenu`, so every installed extension shows up),
  but the popup is drawn by this app in the Win98 style.  The file list and the
  tree show the same menu for a folder.  While that menu is up, the folder's
  row is the tree's selected row (the current folder's highlight returns when
  the menu is gone), and in the file list the right-clicked items are
  selected.  The empty area uses our own working View / Sort / Refresh / New
  commands plus the shell's Paste, Properties and extensions.
- The shell's extensions - all that are registered for any file, any folder,
  a drive or the background of a folder - are loaded once, before the first
  menu a window builds, never while one is being built.  Building a folder's
  menu makes the shell list the libraries on a thread of its own, reporting
  back in a message to ours; an extension that makes a window as its DLL is
  loaded (Baidu Netdisk's did) lets that message run while the loader lock is
  still held, and what the message does needs a thread of its own: the window
  was dead for good, right after it was opened.  Loading them up front takes
  that load out of the build.
- The menus are built ahead, on a thread of the program's own that has
  nothing else to do (Tools → Context menu settings → 提前在后台构建菜单,
  on by default; off: built on the window's thread when the right-click
  comes, as before).  The shell and its extensions take 70 ms (a folder) to
  300 ms (an image) to build a menu, the first ones in a window several
  hundred more, and every right-click used to wait for all of it.  A folder's
  listing has its menus asked for at once - the background's, a folder's, a
  file of each of the four commonest kinds' - and the pointer resting 70 ms
  on an entry has that entry's asked for (the selection's, when the entry is
  in one of several).  A right-click shows what was built, at once: 5 to 20
  ms.  The thread stays behind the window (below normal priority) while it
  only builds ahead, and works at the window's own priority for a menu a
  right-click has nothing to show yet and for the command picked in one - on
  a machine that is busy with something else too.
- What is shown is the thing's own menu as it was last built (one built more
  than a few seconds ago is built again behind it), or, when nothing was
  built for the very thing, the menu of another of its kind in that folder
  (the shell's part of the menu of any file of a kind is the same as that of
  the others), with this program's own items made for the thing as it is.  A
  right-click never waits for a menu to be built when there is one to show.
  The thing's own menu, built anew, is what is on screen from then on: its
  say is put on the rows that are there, where they differ - the window stays,
  as does the row the pointer is on - so a menu is put up once, never taken
  down for another right after (that was a flicker).  A submenu that is open
  is not taken from under the pointer either: the rows are made to say it as
  soon as it is closed.  Only when the thing's own menu has other rows than
  the one on screen is that taken down for it.
  What a menu built ahead says is not final when it is built: the extensions
  fill the submenus in, and once more a moment after (发送到 gets its last
  item then), and a menu that is on screen says what the one it shows from
  has said since.  A few items are the file's own (WinRAR's "add to
  <name>.rar").  A shell command picked meanwhile is looked up in the thing's
  own menu by where the item lay and what it said there, and run from it only
  if both agree; if not, nothing ran, and the thing's own menu is shown in
  the other's place.
- While the menu is up, a left press anywhere else in the window - the menu
  bar's 查看, another file, blank canvas, the tree, a title button - closes
  the menu and is the click it would have been with no menu up: 查看's list
  opens, the file is selected, the selection is cleared, a rubber band starts
  with the press and ends with the release.  The menu is taken down between
  messages, never from inside the window's handling of that click (Windows
  drops a click whose window is activated while the click is being handled),
  and the press is put in the queue for its window as soon as the menu is
  down; a button-up that comes before that press is held by the window and
  given right after it, so press and release are never the wrong way round.
  A right-click elsewhere closes the menu and opens the new one there.
- The background's menu says whether there is something to paste: those menus
  are let go of whenever the clipboard changes (WM_CLIPBOARDUPDATE), and the
  one of the folder shown is built again at once.
- A right-click on something no menu stands for asks for that menu, and the
  window goes on until it is in.  If anything is done in between (a click, a
  key, a scroll), that menu is not shown.
- Menus kept: 24 at most, for five minutes; one a command was run from is let
  go of at once.
- Tools → Context menu settings lists the app's own entries and the shell's
  entries grouped by where the right-click happened (empty area, folder, file
  without an extension, one group per file extension); each is hidden in that
  group only, in the `[menu-bg]`, `[menu-folder]`, `[menu-file]` and
  `[menu-ext.<ext>]` sections of settings.ini.
- The empty-area menu starts with View, Sort by and Refresh, then **New** as
  a submenu holding Folder and Text Document - Explorer's order, nothing flat.
- The sidebar's Favorites are this app's own, not Explorer's: an ordered list
  of folders in the `[favorites]` section of settings.ini (`fav<n>` the
  folder, `favName<n>` a name of its own when it has one). The group is the
  first thing in the tree and is always there, so it is in reach.
- A folder joins them from its own context menu (Add to Favorites), by being
  dropped on the group (files are not taken, and nothing is moved), or
  through Favorites → Add current folder. It leaves them the same way, from
  the context menu of the folder or of its row in the group.
- A favorite may have a name of its own (its row's context menu → Rename
  favorite) and is shown under it, else under its folder's name. It is
  reordered by dragging its row up or down inside the group; a line shows
  where it will land, Esc cancels. A favorite whose folder is gone stays,
  greyed out, so that it can be removed.
- Pressing a tree row navigates when the button comes back up on that row,
  and drags the folder when the pointer moves first - as Explorer's tree does.
- New Folder and New Text Document end in that edit, as in Explorer: the new
  item is scrolled into view, selected, and its name is being typed over
  (a file's extension is left out). Esc leaves it with the name it got.
- F2 (and Rename) edits the name in place: the name's selection box becomes a
  white field with a 1 px black frame, the name stays on exactly its pixels,
  the base name is selected (a folder's whole name), Enter commits, Escape
  cancels, losing focus commits. The field follows its row when the list
  scrolls and is clipped like the row. While a row is renamed its selection
  box is the system's highlight colour - the one the edit box gives the
  selected part of the name - so the row being renamed is one blue, whatever
  the machine's highlight is (Windows 98's was the app's own navy; Windows 10
  and 11 use `#0078D7`). Every other selection stays the app's navy.
- In every view the selection box is painted under the icon, never over it: a
  selected row keeps its icon.

## Responsiveness contract

Lag is never traded for anything. For menus and hover feedback in particular:

- The highlight follows the pointer with nothing in between. A hover change
  repaints the affected rows on the spot, before any other work runs, and no
  timer, grace period or delay stands between the pointer and the screen. An
  address bar button is lit the same way: the bar alone is repainted, into the
  frame that is on screen.
- A submenu opens the moment its row is hovered and closes the moment the
  pointer leaves that row.
- The row under the pointer is the selected row, with one exception, and it
  is a matter of place, not of time: while the pointer is on the path from
  the row it left to that row's submenu, the row keeps the selection and the
  submenu stays. The path is a funnel - as wide as the row where the pointer
  left it, as wide as the submenu plus a row of slack where it meets it, and
  never further than three rows from its own row. A pointer crossing the rows
  beside a submenu on its way to it therefore does not switch them; moving
  along the menu leaves the funnel on its first pixel. (Without this a
  submenu is gone before a diagonally moving pointer gets to it, and nothing
  in it can be clicked.)
- A click in a menu window activates nothing, and a menu does not take the
  foreground moving to its own window for the user leaving it. (Windows asks
  a window whether to activate it on a click; answering yes for a submenu
  took the foreground from the root menu, which closed the whole menu before
  the click reached the submenu.)
- The window shows one frame at a time. Hovering and scrolling draw into the
  frame that is on screen, and it is rendered again only when something says
  that it changed. So whatever changes what the window shows says so itself -
  going to a folder, reading it again, the edit boxes opening and closing.
  (An edit box that ended without saying so left the old folder listed after
  Enter in the address bar, and an empty white box where a name had been after
  Escape; what was on screen and what a click hit were then two different
  things.) The tests hold what the desktop shows against a frame rendered from
  the state, after each of those.
- The tree asks a folder whether it has a subfolder when its row is drawn or
  clicked, never when the folder above it is listed: listing System32 used to
  ask its 300 folders first, half a millisecond each, before the new folder
  could show - and again after every file operation in it. Some thirty rows
  are ever in view.

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
