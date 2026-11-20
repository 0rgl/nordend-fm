# Nordend: every feature

Everything Nordend does today, by area. Numbers are measurements from
[benchmarks.md](benchmarks.md), taken on a Ryzen 7 5700G with an RX 6650 XT; how each feature came
to be is in the [devlog](devlog.md), and what comes next is in the
README's [What's next](../README.md#whats-next).

## What it is

- A file manager written from scratch in C++20 for Wayland: its own OpenGL ES renderer for
  boxes, text, icons and images. **No GTK, no Qt, no GLib.**
- Its own project, at home on any Wayland desktop. It follows the desktop's theme by itself (GTK
  colours, the portal, Noctalia's palette), with Tabler's line icons in its colours. Its window
  has one header of its own everywhere, as a modern file manager (`[window] title_bar` keeps the
  compositor's title bar instead).
- One window thread, a pool of workers for everything that touches the disk, and **zero CPU when
  nothing moves**: no frame is drawn unless something changed (0 ticks in 30 s, measured).

## Three views of the same folder

- **Columns** (Ctrl+1), as in Finder: each folder you pick opens in the next column, and the last
  column shows the selected file. Left from the first column climbs to the parent. Columns are
  resized by dragging their edge (a double click on the edge resets it), and the row slides
  sideways by itself to keep the focused column in sight.
- **Grid** (Ctrl+2): large icons and thumbnails. Ctrl+plus, Ctrl+minus or Ctrl+wheel resize the
  cells from 48 to 512 px, and Ctrl+0 goes back to the usual size. The largest sizes fit the
  available width when the window is narrow; Ctrl+I hides the information pane for more room.
  Folder and fallback file icons stay at most 256 px, centred in the thumbnail space.
- **Details** (Ctrl+3): a table of name, size, date and type. Click a title to sort by it and
  click again to reverse; drag a column's edge to widen it. By size, folders keep their name
  order; by type, unknown types come last. The order you choose is used in the other views too.
- **Zoom in the lists** too: Ctrl+plus, Ctrl+minus or Ctrl+wheel make the rows of the columns and
  the details larger or smaller (five sizes: row, icon and text grow together, the details' other
  columns with them), Ctrl+0 the usual size; the cursor's row stays where it is on screen. The
  lists share one size, the grid keeps its own. Each tab has its own zoom (a new tab starts with
  the one it comes from), remembered with the tabs.
- **Sort By** in the context menu (on a file or on empty space), in every view: name, size,
  modified or type, ascending or descending, a dot on the order in use. One order for the whole
  window: every folder in every view is sorted so, and it comes back at the next start.
- **Tabs** (Ctrl+T): each with its own folder, view and back/forward history. The bar above the
  path shows them when there are two or more: a click shows a tab, its × or a middle click
  closes it, + opens one. Ctrl+Tab and Ctrl+Shift+Tab go round them, Alt+1…9 picks one, Ctrl+W
  closes the one shown (never the last). Each tab keeps its own zoom too. A middle click on a folder, or the context menu's Open in
  New Tab (Tabs, for several folders), opens it in a tab behind the one shown; a middle click on
  a selected folder opens every selected folder. The order, hidden files, the sidebar and the info pane
  are the window's, the same in every tab.
- **One folder, three ways of looking at it.** Switching views keeps the cursor, the selection and
  the file types already detected. The status bar has a button for each view.

## Moving and selecting

- **Natural scrolling.** On a touchpad the list follows the fingers 1:1, keeps gliding when they
  lift, slows down smoothly and stops softly at the ends. A mouse wheel glides instead of jumping,
  and quick turns add up. The arrows and Page Up/Down glide; Home and End jump.
- **Scrollbars you can take:** a thin bar shows where you are in every list, in the columns'
  strip and on a text page; near it the pointer finds it from 14 px away and it widens. Drag it
  and the list follows, held where you took it; a click beside it on the track brings it there.
  A press on a bar never selects or drags a file.
- **Keyboard first:** arrows, Page Up/Down, Home and End; Enter, Right or Alt+Down to go in;
  Backspace, Left or Alt+Up to go back. The cursor lands on the folder you came from.
- **History:** Alt+Left and Alt+Right, or the mouse's back and forward buttons, go through the
  folders opened (the last 100), each with the cursor where it was left. Opening a folder after
  going back drops the ones ahead; a folder deleted meanwhile is skipped, with a message.
- **Hidden files out of the way:** dotfiles are hidden at start; Ctrl+H shows them (dimmed) in
  every view and hides them again. Hidden, they are never selected, so Ctrl+A and Delete touch
  only what you see, and the status bar says how many there are ("12 items, 3 hidden"). A hidden
  folder you open (from a bookmark, say) stays in sight on the way to it.
- **Selection as you expect it:** click, Ctrl+click and Shift+click; Shift+arrows for a range,
  Ctrl+arrows and Ctrl+Space to pick here and there, Ctrl+A for all, Escape for one again.
- **Rubber band:** a drag from empty space (below the entries, a row away from its icon and
  name, a cell away from them in the grid) draws a rectangle that selects what it touches; with
  Ctrl it adds to the selection. Past the top or bottom of the list it scrolls, faster the
  farther the pointer; in the column view it stays in its column. A drag from an icon or a name
  still moves the files, and a click anywhere on a row still selects it.
- **The selection follows its files:** cursor and selection are kept by name, so they stay on a
  file that another program renames or that moves in the order.
- **Every shortcut on one card:** F1 or "?" lists them all, from the same table the code runs on,
  and a test checks that the card and the behaviour agree. In a window too low for them all, the
  groups scroll under the title (wheel, touchpad, arrows, Page Up/Down, Home/End, its scrollbar).

## Photos first

- **Thumbnails in a fraction of a second.** Nordend reads the previews that cameras already store
  inside their raw files instead of developing the raw. A screenful of photos is ready in
  0.15-0.4 s the first time and in 12-22 ms from the cache. One thumbnail takes 12 ms from an
  OM-1 JPEG and 36 ms from an ORF.
- **Raw files from many cameras:** OM System and Olympus ORF (including the maker-note preview),
  Canon CR2, Nikon NEF and TIFF, Sony ARW, Fujifilm RAF, Panasonic RW2 (cropped to the shot's
  aspect ratio), Samsung SRW, Epson ERF, Hasselblad 3FR and DNG (including previews stored as a
  JPEG inside a TIFF: full size in a Samsung GX20's DNG). A DNG whose previews are too small for
  where it is shown (more than 4/3 stretched: Leica M240 and M8, DJI drones, files from Adobe's
  converter) is developed by Nordend's own reader instead: lossless JPEG or uncompressed sensor
  data (8-16 bit, strips or tiles, linear raw too), black and white levels, the camera's white
  balance with highlights kept white, the file's colour matrices for the shot's light, its
  baseline exposure and tone curve (else a light curve of Nordend's), the default crop, the
  file's corrections (gain maps for vignetting and colour shading, the lens warp: a DJI drone's
  corners no longer dark; a correction Nordend does not apply keeps the preview), at half
  size or smaller, straight to the size shown; in Quick Look in about 105 ms (Canon) to 260 ms
  (M240 at 2560x1440). Each is shown upright, in the shape it
  was shot, without the black bars some cameras put on their thumbnails; a preview the camera
  stored broken is passed over for the next one. Formats with no preview Nordend can read (Sigma
  X3F, old Panasonic RAW) say so in Quick Look.
- **Images:** PNG, JPEG and GIF (wuffs), WebP (libwebp), JPEG XL (libjxl), binary PGM and PPM
  (8 and 16 bits), and **TIFF** with Nordend's own reader (BigTIFF too; uncompressed, LZW,
  Deflate, PackBits, JPEG; 1 to 16-bit and floating-point samples; grey, RGB, YCbCr, palette,
  CMYK, alpha; strips and tiles), with EXIF orientation. The format is told by the file's bytes, not its name
  (a PNG called `.jpg` still shows), and only camera files are searched for stored previews.
- **Pictures made apart:** the code that reads photos (libwebp, libjxl, wuffs, and Nordend's own
  TIFF, DNG and camera-file readers) run in small `nordend-preview` processes, not in Nordend: a file that crashes or hangs
  one costs only its preview ("Preview unavailable"), and Nordend carries on. Each one lives in
  a cage of Nordend's own (seccomp and landlock, no library): it cannot open a file, reach the
  network, start a program or signal another one, and has at most 4 GiB of memory. They start when a
  picture is first needed (at most one per worker), give their memory back after a quiet second,
  and Nordend closes them after 30 s without work.
- **Engines described by manifests:** each reader program (an *engine*; `nordend-preview` is the
  base engine, for images and text) comes with a small TOML manifest in `share/nordend/engines/`
  saying which types it shows and what it gives back. Text files too are read in its cage, never
  by Nordend. Nordend reads them at start-up (about 0.05 ms) and asks them
  who shows a file; a broken manifest or a missing program is skipped with a warning, and the
  files it would show get the information card. `nordend --about` lists the engines found.
- **Plain reasons:** a file that cannot be shown says why in one sentence, the same in Quick Look
  and everywhere else: not a kind Nordend shows, no readable preview in a raw file, too large,
  damaged, or unreadable.
- **Shared freedesktop thumbnail cache:** thumbnails go where GNOME, KDE and other apps look for
  them, byte-compatible, so they are made once for all of them. Files that fail are remembered.
- **Quick Look** (Space): the photo fills the window, raw files included. An ORF's sharp picture is on
  screen about 100 ms after Space (its thumbnail at once). The arrows walk the folder without closing it, and the next
  photo is already loading. Text files open on a page you can scroll, in your monospace font
  (darktable sidecars included), read in UTF-8, UTF-16 (with its byte order mark) or Latin-1
  (Windows-1252, for files from old editors: "caffè 3€" shows as written), with **syntax
  colours** for C, C++, Rust, Python, JavaScript, TypeScript, Lua, Luau, shell scripts (heredocs
  included), fish, CMake, Makefiles, JSON (with JSONC's and JSON5's comments), TOML, YAML
  (block scalars, anchors and tags included) and INI files (desktop entries, systemd units,
  `.gitconfig`, `.editorconfig`), keys set apart from their values, and `.gitignore` (and
  `.dockerignore`, `.ignore`, `.npmignore`...) and `.gitattributes`, wildcards and negations
  standing out, Markdown (highlighted, not rendered: its code blocks in their own language, a
  YAML or TOML front matter) and diffs and patches (added and removed lines, hunks, headers), the
  language found from the file's type, its name (`PKGBUILD`,
  Void's `template`, `CMakeLists.txt`, `Cargo.lock`, `.clang-format`, `.gitignore`), its extension or its `#!` line, made from your theme's colours and readable
  on its page; the info pane's first lines are coloured too; any other file shows what it is.
  On a text page you **select and copy**: drag (the page scrolls when you go past its top or
  bottom), double click a word, triple click a line, Shift+click to extend, Ctrl+A for the whole
  file, Ctrl+C copies the text (with nothing selected it copies the file, as anywhere else); a
  click beside the page still closes Quick Look. Long lines scroll sideways (Shift+wheel, or the
  touchpad), the line numbers staying put; Home and End reach the text's top and bottom, Page Up
  and Page Down a page (the arrows still walk the folder). The caption says the language, the
  encoding, CRLF when the file has it, the lines and the size. A minified file (lines over 500
  bytes on average) is shown plain, its caption saying so. A log (`.log`) larger than what is read
  shows its newest lines: its last 256 KiB, opened at the bottom, "last N lines" in the caption.
- **The info pane** beside every view shows the preview and what the file is: size, pixel size,
  dates (including the birth time), type, and for photos the shot itself (camera, lens,
  exposure, aperture, ISO, focal length, when). "More details" adds permissions, owner and group, link target and item count. In the
  grid and the details Ctrl+I hides it, and its edge can be dragged from 240 to 720 px.
- **A folder's size** in the info pane: once the cursor rests on a folder for a quarter of a
  second, a worker adds up everything inside ("Counting… 1.2 GB" as it grows), then "Size" and
  "Contains" (files and folders). Several entries selected with folders among them: their whole
  size, added up the same way (a file hard-linked from two of them once). Moving on cancels it,
  and nothing is read on the main thread.
  Sizes are the files' own (as their rows show; hidden files included); a hard link counts once;
  links inside are never followed. Disks mounted inside and btrfs subvolumes are not entered
  (as `du -x`), nor folders deeper than 64 levels: the size then says "at least". Folders on a
  network or FUSE file system (NFS, SMB, sshfs) are not counted at all ("—"), so a stuck server
  never holds a worker; NTFS and exFAT disks (fuseblk) are.
- **darktable-aware:** sidecars get their own icon and follow their photo when it is renamed,
  copied, moved or trashed. Their ratings are not read: Nordend's own ratings come later.

## Working with files

- **Opens in the right app.** Enter or a double click opens a file in its default application,
  chosen exactly as GTK apps choose it: 0 differences from `gio` on all 1 232 file types and
  aliases. Nordend implements the freedesktop specs itself, without `xdg-open`:
  - several files for the same app start one instance (five photos, one darktable);
  - terminal apps such as Helix open in a terminal (`$TERMINAL`, `xdg-terminal-exec`, foot,
    kitty, ghostty, alacritty, wezterm...);
  - the new window comes up in front, through an xdg-activation token;
  - apps run detached: no zombies, and closing Nordend leaves them open;
  - **a program starts itself** (issue #17): an ELF executable or an AppImage, named as one
    (Nordend's type, by name then content: a program called `invoice.pdf` is a PDF), that may
    already run, starts in its folder like any app. One that may not run yet asks first, on the
    card ("Allow “Tool.AppImage” to run?", **Allow and Run** gives it `u+x` and starts it).
    Safety first: scripts never start this way (a downloaded `.sh` looks like a document: it
    opens as text), nothing ever runs by itself (a stick plugged in, a folder opened, Quick
    Look), a disk mounted `noexec` stays so (said in the status bar), and `[open] programs =
    false` in `config.toml` turns it off. A program runs with your permissions, as from a
    terminal: Nordend's cage is for its own preview helper, not for other people's programs;
  - the status bar says what happened ("Opening 5 files with Darktable…"), in red when there is
    no app for a type or the app fails at once.
- **Open in a terminal.** F4 opens a terminal in the folder under the cursor, else in the folder
  shown (also from the context menu, "Open in Terminal"); the same terminal as for terminal apps:
  `[terminal] program` (and `args`) in `config.toml` when set, read live, then `$TERMINAL`,
  `xdg-terminal-exec`, foot... A known terminal runs a command its own way, any other with `-e`;
  a configured program that is not installed is reported in the log and the order goes on.
- **Rename in place** (F2): the name becomes a field where it is, in all three views, with the
  name selected but not its extension. Typing works with accents and compose. Enter keeps the
  name, Escape gives up, and a click elsewhere keeps it. The usual editing keys work, including
  words with Ctrl+arrows and selection with Shift; Ctrl+C and Ctrl+X copy and cut the selected
  text, Ctrl+V pastes the clipboard's text (its first line); Ctrl+Z undoes and Ctrl+Shift+Z redoes
  (a word typed, characters deleted one after another, a cut or a paste is one step; up to 100).
  With the pointer, a drag or Shift+click selects and a double click selects the whole name. The
  same goes for every text field: a bookmark's label, Ctrl+K and the file chooser's name.
- **New folder** (Ctrl+Shift+N): "New Folder" (or "New Folder 2"...), ready to be named.
- **Never overwrites:** renames use `RENAME_NOREPLACE`, so an existing file is never replaced,
  even one that appears in the meantime. A taken or invalid name is refused with a message and
  the field stays open for another try.
- **Photo-safe renames:** renaming `P1.ORF` also renames `P1.ORF.xmp` and the duplicates
  `P1_01.ORF.xmp`, `P1_02.ORF.xmp`… If any of their new names is taken, nothing moves.
- **Copy and move** (Ctrl+C or Ctrl+X, then Ctrl+V), in any of the three views:
  - a move on one disk is a rename: instant, whatever the size;
  - a copy runs as fast as `cp` (3.1 GB of photos in 1.39 s, warm cache, against 1.39 s), by
    the kernel's `copy_file_range`, on a thread of its own, so the window never waits;
  - the status bar shows "Copying 163 files · 2.1 GB of 3.4 GB · 180 MB/s · 8 s left", a
    progress line and an × to stop (or Shift+Escape, in Quick Look too); what arrived is
    selected afterwards;
  - nothing half-copied ever carries a real name: each file is written under a hidden temporary
    name and renamed when complete, and a folder is built whole the same way. Stopping leaves
    nothing behind;
  - moving off another disk (an SD card, a USB drive) deletes the original only after its copy is
    flushed to disk;
  - dates (to the nanosecond), permissions and symbolic links are kept, and darktable sidecars
    travel with their photo, even under a new name;
  - cut files fade until pasted (Escape keeps them where they are);
  - a taken name asks on a card: Replace (the old file goes only once the new one is complete),
    Skip, Keep both ("P1 2.ORF", sidecars included), for this one or for the rest; for two
    folders Merge: their files put together, each file whose name is taken there asked on the
    same card (a photo with its sidecars, folders inside merged without a question; a Merge for
    the rest never answers for the files); pasting into the same folder keeps both by itself;
  - refused before anything moves: a folder into itself, or more than the free space.
- **Undo** (Ctrl+Z, in any view and in Quick Look) the last operation on files, and redo it
  (Ctrl+Shift+Z):
  - a rename gets its old name back, darktable sidecars included; a move goes back where it came
    from, across disks too; a Delete is restored; a restore goes back to the trash;
  - copies and new folders or files go to the trash, never deleted, so even an undo can be
    undone; a new folder only while it is empty, a new file only while it is as it was made;
  - after a merge only what the copy or move put there that was not there goes (a move's files
    go back, their folder made again if the move took it away); a replaced file's old contents
    are gone, and the undo says how many ("1 replaced file cannot be brought back");
  - a permanent delete cannot be undone, and the status bar says so; what changed meanwhile
    (a file another program moved) is skipped and said;
  - the status bar says what happened: "Undone: “Lake.ORF” is “P1.ORF” again", "Undone: 3
    items moved to the trash", "Redone: “P1.ORF” back in album".
- **Drag and drop:** a press on an entry's icon or name moved a few pixels drags it (the whole selection when it
  is in it), with its icon and name under the pointer. It drops onto a folder in any view (marked),
  the empty part of a column or view (that folder), a step of the path above or a place in the
  sidebar; the Trash trashes. On the same disk it moves, onto another it copies; Ctrl copies,
  Shift moves. It goes through the same engine as a paste: sidecars, questions, progress, Ctrl+Z.
  A folder never goes into itself, and dropping where the files already are does nothing. Other
  programs receive the files as file URIs and paths (Firefox, terminals), only to copy: Nordend
  never deletes its files because another program says it took them. Files dragged from another
  program (Firefox's downloads) are copied; a web address is not a file. Near the top or bottom of
  a list the drag scrolls it. Opening a folder by hovering over it comes later.
- **The system clipboard**, with other programs:
  - files copied or cut in Nordend paste in other programs: a file manager, Firefox (an upload, a
    message), a terminal or an editor (their paths); Nordend offers them as
    `x-special/gnome-copied-files`, `text/uri-list` (each file URI as GLib writes it) and text,
    and says "cut" the GNOME and the KDE way;
  - files copied or cut in another program paste in Nordend with Ctrl+V: copied, or moved when
    the other program cut them, through the same engine (sidecars, conflicts, progress); only
    files on this computer are taken;
  - read and written through pipes the window's loop watches: a program that does not answer
    never freezes Nordend (it is given up after 3 s);
  - when another program copies something, the files Nordend cut stop fading: Ctrl+V pastes what
    is on the clipboard now. A cut pasted once empties the clipboard.
  - Ctrl+Shift+C (or "Copy Path" on a folder's empty space, or the command palette) copies the
    path of the folder shown as text, whatever is selected, and the status bar says which; the
    entries' own paths come with Ctrl+C. Text on the clipboard replaces the files Nordend took.
- **The Trash** in the sidebar (its icon full or empty):
  - every trash as one folder: the home's and each disk's (`.Trash-1000` on a data disk), by
    original name; the same name trashed twice shows as "a.txt" and "a.2.txt";
  - the info pane adds where it came from and when it was deleted; previews, thumbnails and
    Quick Look work as anywhere else; it follows what other programs trash or restore, live;
  - Restore (right click) puts things back where they were, makes their folder again if it is
    gone, asks on the card when the name is taken (a folder is never merged), and brings a
    photo's darktable sidecars back with it;
  - Delete (or "Delete Permanently…" in the menu) deletes the selection for good, a photo with
    its trashed darktable sidecars; "Empty Trash…" (on the trash's empty space, or a right click
    on Trash in the sidebar) empties every trash at once, orphaned `.trashinfo` files and
    `directorysizes` included; both after the confirmation card;
  - nothing is made, renamed or pasted there, and files are not opened from the trash.
- **Context menu**: a right click (or a two-finger tap on the touchpad) in any of the three views
  or on the info pane, the Menu key or Shift+F10:
  - on files and folders: Open, Open With, Copy, Cut, Paste, Rename (one entry), Move to Trash,
    Delete Permanently; on a folder also Open in Terminal and Add to Sidebar;
    **Allow Running as Program** on files of a type that runs (programs, AppImages, scripts, or
    a type not known yet), or **Stop Running as Program** when every selected file may run
    already (any file with an execute bit, such as photos from a FAT stick): `u+x` only to
    allow, every `x` away to stop; never with a folder among them, in the trash or the file
    chooser; in the command palette too, and undone with Ctrl+Z (issue #17); a file that may
    run as a program wears **a small dot** at its icon's bottom right, in every view (the
    theme's accent; the icon's colour on a selected row), said in words in the info pane ("Runs
    as program: Yes/No"): a dot or none, never red against green, so it reads without colours;
    only on files of a type that runs, so photos from a FAT stick (all 0777) get none;
    on a folder's empty space: New Folder, New File, Paste, Add to Sidebar, Open in Terminal,
    Copy Path; what cannot be done now is dimmed,
    and each item shows its shortcut;
  - a right click on an entry selects it first, unless it is part of the selection already;
  - Open With lists the applications for the file's type exactly as GTK apps do (0 differences
    from `gio mime` on all 1 232 types and aliases), the default one first, and opens every
    selected file with the one chosen (on a folder: the applications for folders, such as an
    editor, and the selected folders);
  - **actions of your own** (`[[action]]` in `config.toml`, like Thunar's custom actions): a name
    and a command run on the selected files (Exec's quoting and field codes, `%d` for the folder
    shown, or a list of words where a code counts only as a whole word), shown after Open With
    only when every selected entry fits: files and/or folders, globs on the whole name (case left
    aside) or types with their subtypes, how many may be selected; grouped in submenus; on the
    empty space too (`background`), in a terminal (`terminal`); started detached in the folder
    shown, a quick failing exit reported; never in the trash or the file chooser; followed live,
    a broken action left out and reported by `--check-config` (0rgl's issue #14);
  - New File makes an empty file, or a copy of one of your templates (`XDG_TEMPLATES_DIR`),
    never over an existing file, with the name ready to edit;
  - drawn by Nordend in your theme's colors, it stays whole near the window's edges, and the
    keyboard works in it: arrows, Enter, → into a submenu, ← and Escape back.
- **Empty files open as their name says**: a new, empty `notes.txt` opens in your text editor
  (GLib calls any empty file `application/x-zerosize`, which no application opens).
- **Trash** (Delete), the freedesktop Trash specification 1.0, in any of the three views and in
  Quick Look:
  - files on the home's disk go to `~/.local/share/Trash`, files on other disks to that disk's
    own trash (`.Trash-1000` at its top, or `.Trash/1000`): a rename, never a copy, so trashing
    is instant (1 001 items in 25 ms, `gio trash` 70 ms); where no trash can be made, the files
    stay and the status bar says why;
  - other trash programs (`trash-list`, `trash-restore`, gio, file managers) see what Nordend
    trashed and restore it to its place: the `.trashinfo` files are the same as GLib's, odd
    names included;
  - darktable sidecars go with their photo, each restorable on its own;
  - the cursor moves on to the next entry; the status bar says what went ("“P2.ORF” with 1
    darktable sidecar moved to the trash") or what could not;
  - trashed folders are listed in `directorysizes` with their size, so trash tools need not
    measure them again.
- **Delete permanently** (Shift+Delete, in any view and in Quick Look; Delete in the Trash):
  - first a card says what goes: "Delete “P1.ORF” permanently?", "With its darktable sidecar ·
    21.0 MB", and for folders how many files and folders they hold in all; the size is added up
    on a worker, the card is there at once; Enter (or the red Delete) deletes, Escape (or Cancel)
    keeps everything;
  - on the transfer thread, with progress in the status bar and its × to stop; links are never
    followed, read-only folders of yours are opened up to be emptied; darktable sidecars go with
    their photo, and a photo that stays keeps them;
  - 10 101 files and folders in 0.20 s on ext4 (`rm -rf` 0.17 s: the difference is the first
    pass that counts them for the progress bar).
- **Clear errors:** a read-only folder, permission denied or a missing file shows a message in the
  status bar, never a crash.

## Finding your way

- **Command palette** (Ctrl+K): a card at the top of the window; type a few letters of anything
  Nordend can do and Enter runs it. Every command is there, the ones with a shortcut (shown at the
  right) and the ones only in menus (Sort by…, Open With…, New Empty File, Empty Trash, Eject,
  Zoom, Select All, Quit); only what can be used at that moment is offered ("Paste" with
  something to paste, "Rename…" with one entry). The sidebar's places are there as "Go to …" and
  the other tabs as "Tab 2: …". The letters are matched in order anywhere (fzy, as Noctalia's
  launcher): "nf" finds New Folder, "gdo" Go to Downloads; the matched letters show in the accent.
  With nothing typed, the ten most recently used come first, remembered in `state.toml`. ↑ ↓
  and Page Up/Down move, Enter runs, Escape or Ctrl+K again closes; the mouse works too. Each item
  goes the same way as its key or menu item, so they never differ. "Open With…" lists the
  applications in the palette itself.
- **Find files** (Ctrl+F): the same card, on every file and folder under the folder you are in,
  at any depth. They are listed once, nearest first, on a worker (273 000 entries in under half a
  second), and what you type is matched against their paths on all the workers, a new letter
  cancelling the search before: the first results show in about 8 ms. A match in a file's name
  ranks above one that needs its folders, so "vinc17" finds `Piramide Vincent/_7230017.ORF`;
  spaces in what you type do not count. Enter shows the file in the view (the columns unfold down
  to it), Space opens it in Quick Look, Shift+Enter in its application. Hidden files are left out
  unless Ctrl+H shows them; links are not followed and other disks mounted inside are not entered;
  the search stops at 300 000 entries and says so. The memory the paths took (about 85 bytes
  each) goes back to the system when the card closes.
- **Sidebar:** your home and its folders (Downloads, Pictures… in your language, from
  `user-dirs.dirs`), the file system, your GTK bookmarks (shared with other file managers) and
  the mounted disks. A disk appears as soon as it is mounted and disappears when it is unmounted.
  Ctrl+B hides the sidebar, and F6 moves the keyboard into it and back.
- **Disks:** through udisks2, a disk plugged in appears among the devices, quieter, without
  mounting itself (encrypted ones come later); a click mounts it and opens it, and a disk that
  mounts read-only says why (an NTFS disk not closed properly) and what fixes it. The eject icon of
  an external disk unmounts every partition on it and powers it off, taking the view home first if
  it shows that disk; "in use" says to close what is open there. The right click mounts, unmounts
  and ejects. The list follows disks coming and going; nothing waits on udisks2.
- **Archives:** Enter (or a double click) on a zip, tar, tar.gz or .gz opens it like a folder, read
  by Nordend's own code in the sandboxed engine; folders inside open the same way, and Quick Look
  shows photos and texts from inside it without writing anything to disk. Nothing inside can be
  changed; encrypted entries and compressions not read yet are listed and said; names that would
  lead outside the archive are left out, and the status bar says how many. Inside one, the path
  bar shows the archive's step with an archive icon and a quiet "read-only" after the last step,
  and the status bar says "zip archive · read-only · 3 items". From outside, Quick Look shows what
  an archive holds (files and folders, size unpacked, encrypted entries) and its first entries,
  folders first; the info pane shows the same counts, short.
  Extracting: Ctrl+C inside an archive and Ctrl+V anywhere (or a drag into Nordend) extracts,
  "Extract Here" in an archive's menu puts it next to it (in a folder named after it when it holds
  more than one item), and Enter on a file inside opens it from a private copy. Nordend makes
  every file itself in a hidden folder where nothing can lead out (no `..`, no link followed), and
  moves the items in only when all is written, never over anything ("x 2"); setuid bits are
  dropped, links that lead outside left out, encrypted entries said; more than the free space is
  refused, and an archive that unpacks to more than 100 times its size (over 1 GiB) is asked
  about. Shift+Esc stops it, leaving nothing behind.
  Making archives: "Compress to .zip" or "Compress to .tar.gz" in the menu puts the selection
  into one archive next to it, named after it and ready to rename; links are kept as links,
  permissions and times kept, ZIP64 when large; it is written under a hidden name and appears
  only when complete. Shift+Esc stops it, Ctrl+Z sends it to the trash.
- **Disc images:** Enter (or a double click) on an `.iso` mounts it read-only through udisks2 and
  opens it: Nordend opens the file itself and hands udisks2 the open file, never a path; the
  kernel reads the file system, no parser of Nordend's. It shows among the devices with a disc
  icon; ejecting it unmounts it and lets its loop device go. An image mounted already just opens;
  a file without a file system the kernel can read says so and leaves nothing behind.
- **Favourites:** Ctrl+D adds the folder shown, "Add to Sidebar" the folders selected (or the
  folder, from its empty part), and folders dragged onto the "BOOKMARKS" title or below the last
  place are added at the end, a line showing where; dropping onto a place still moves into it.
  A favourite's right click: **Rename…** (F2 with the keyboard in the sidebar), a field on its
  row, Enter or a click elsewhere keeping the name, Escape leaving it, an empty name giving the
  folder's back; **Choose Folder…**, Nordend's own folder chooser in a window of its own,
  starting in the favourite's folder, the one chosen taking its place with its name and
  position (one with a favourite already is refused); **Move Up** and **Move Down** (Alt+↑,
  Alt+↓ with the keyboard in the sidebar), among the favourites shown; **Remove from Sidebar**.
  They are the GTK bookmarks: other file managers and the Open dialogs of other apps (Firefox's)
  show them too, renamed and in the same order. The file is written all at once on a worker,
  changing only that favourite's line and keeping the rest (remote bookmarks, other labels);
  a folder already there is not added twice. Dragging to reorder comes later.
- **Breadcrumb:** the folder's path as a row of clickable steps, starting from your home. A long
  path is shortened from the left.
- **Status bar:** item count, selection, free space on the disk (updated after a copy into the
  folder), the three view buttons and "?". Messages appear here for a few seconds.

## The file chooser of other applications

- **Nordend as the desktop's Open and Save dialog**, through xdg-desktop-portal's FileChooser:
  Flatpak applications always ask the portal; Firefox, GTK and Qt applications when told to. A
  small service, `nordend-portal` (in `libexec`), is started by D-Bus when an application asks,
  opens one `nordend --pick` window for each request and leaves after 30 s with nothing to do:
  no memory used while nobody is choosing, and a crash costs only that one window.
- **Choosing:** the same views, sidebar, Quick Look and thumbnails as Nordend itself (the grid
  the first time; its own `picker.toml` remembers the folder, view, tabs and size apart from the
  main window). Enter or a double click chooses a file, the selection when the application takes
  several; Ctrl+Enter or the application's button ("Open", "Select") chooses; Escape or Cancel
  cancels. Only folders, when the application asks for a folder.
- **Filters** the application offers, in a list at the bottom: patterns without case, MIME types
  with their subtypes, `image/*`; told by the name alone, as a GTK chooser does. The status bar
  counts what they leave out.
- **Saving:** a name field with the proposed name, its stem selected so typing keeps the
  extension; New Folder; a question before replacing a file; several files saved into one folder
  with one question. The application's own choices (an encoding list, a checkbox) sit in the bar.
- **Over the application's window:** xdg-foreign and xdg-dialog keep the chooser over it, modal
  (Umbriel, GNOME, Plasma; Umbriel attaches it to the application's tile like any dialog); on a
  compositor without them it is a window of its own. Closing the application closes the chooser.
- **`nordend --set-file-chooser`** makes Nordend the chooser of the current desktop: one line in
  your `~/.config/xdg-desktop-portal/<desktop>-portals.conf`, where xdg-desktop-portal looks
  first (a new file starts as a copy of the desktop's own, so its other portals stay as they
  were). `--unset-file-chooser` takes it out again and brings back the line it replaced; a file
  it wrote and nobody changed is removed. xdg-desktop-portal reads the change at the next login,
  and finds Nordend only when it is installed under `/usr` (as the package is): the command warns
  otherwise.
- Tried with zenity, gnome-text-editor, Firefox and kate, on Umbriel, GNOME and Plasma.

## "Show in Folder"

- **Browsers and other applications reach Nordend** (issue #16): "Show in Folder" for a
  download (Firefox, Chromium and the browsers built on it, Electron apps such as VS Code, Flatpak
  apps through the desktop portal) calls `org.freedesktop.FileManager1`, which
  `nordend-portal --file-manager1` serves once you have asked for it:
  - `nordend --set-file-manager` writes the service file in your own
    `~/.local/share/dbus-1/services`, which the session bus reads before the system's (where
    Thunar, Nautilus or Dolphin put theirs): never for everyone, as two system files for one name
    would be a conflict. It says when another file manager holds the name right now (Nordend
    answers once it exits, or after the next login) and when folders do not open in Nordend
    (`nordend-setup configure` sets that). `--unset-file-manager` removes the file, only if it is
    Nordend's.
  - The service answers at once (Firefox gives up after a second and opens the folder itself)
    and starts `nordend --reveal`: each item's folder with the item selected, one tab per folder,
    a folder shown selected in its parent; "properties" also opens the info pane's details. It
    leaves after 30 seconds without a call.
  - The caller's activation token brings the window to the front; Nordend now reads
    `XDG_ACTIVATION_TOKEN` at start whoever started it (a launcher, a dock), and takes it out of
    the environment so the programs it starts never reuse it.
- **`nordend --reveal PATH...`** (and `--properties PATH...`) do the same by hand.

## Installing and updating

- **nordend-setup**, one POSIX shell script for distributions without a Nordend package:
  `install`, `update`, `check`, `configure`, `status`, `uninstall`; without a command it shows
  the state and asks what to do. Read before running ("download, read, run", no `curl | sh`);
  every command is shown before it runs, `--dry-run` only shows them, `--yes` answers for you.
  Colours from the terminal's own palette, none with `NO_COLOR` or off a terminal.
- **The system:** the distribution from os-release (derivatives by `ID_LIKE`), its package
  manager (apt, dnf, pacman, zypper, xbps), the compiler (GCC 13+), CMake, Ninja, Git and the 17
  libraries with sdbus-c++ 2.1+; what is missing is installed with the exact command shown, after
  one question for sudo. Releases known too old (Ubuntu before 26.04 and its derivatives, Debian
  before 13) are told so; NixOS is pointed to the flake.
- **Installing:** the newest release's tag, cloned over HTTPS (`git clone --depth 1`), built in
  the cache and installed into `/usr/local` (or `~/.local`, or `--prefix`); `nordend.portal`
  copied to `/usr/share` for the file chooser, on request. A Nordend from a package is left to
  its package manager: the script stops. A record beside the install keeps the version, the
  commit and every file installed.
- **Updating:** `check` asks GitHub for the newest release's tag (nothing else; Nordend itself
  never goes online), `update` builds it with the same choices and removes the files the new
  version no longer has. The script runs from a copy of itself, so it can replace itself.
- **Settings:** `configure` makes Nordend the file chooser (`nordend --set-file-chooser`) and
  the folder opener (`xdg-mime`, remembering the one before) or gives them back, and says where
  to set a key for it; `uninstall` gives them back before removing the files.
- Tried in CI (`.github/workflows/setup.yml`, weekly too) on Debian 13, Ubuntu 26.04, Fedora 43,
  Arch and openSUSE Tumbleweed, from a bare container to installed, updated and removed; by hand
  on Void with GNOME and Plasma.

## Always current

- **Live folders:** files created, removed, changed or renamed by any program show up at once
  (inotify). A thousand-file copy arrives in a few smooth updates, not a thousand.
- **File types exactly as GTK sees them:** the freedesktop shared-mime-info database, read by
  name first and by content only when needed (and only for what is on screen). 0 differences from
  `gio` on about 4 750 files.
- **Live theme:** change the palette in Noctalia and Nordend follows instantly (with the
  `nordend` template enabled in Noctalia); elsewhere, switch
  the desktop between light and dark or pick another accent colour and Nordend follows too.

## Looks

- Its own icon: a folder with the Nordend's snowy summit, in one ice-blue hue
  (`share/icons/hicolor/scalable/apps/nordend.svg`, named after the app id), shown by docks,
  launchers and menus on any desktop.
- **The icon follows the accent:** Nordend writes its icon in the desktop's accent (Noctalia's
  primary colour, else GNOME's or Plasma's accent from the portal) to
  `~/.local/share/icons/hicolor/scalable/apps/nordend.svg`, which docks and launchers read before
  the installed one, and rewrites it when the accent changes, only when it changes. The three
  tones keep the ice icon's lightness and steps in OKLCH, with the accent's hue and chroma (held
  to what the icon can carry); the snow stays white. No accent, or `icon.follow_accent = false`:
  the copy is taken away (installed under `~/.local`, the ice icon is written back). An icon not
  written by Nordend at that path is never touched.
- Noctalia's colors and the same line icons as Noctalia (Tabler), tinted by the theme; camera
  raw files and darktable sidecars have their own icons. The icons are compiled into the
  program (a 14 KB subset of the official Tabler font), so they show on any desktop.
- Sharp at any scale: fractional scaling (1, 1.25, 1.5, 2…), boxes snapped to the pixel grid.
- Text shaped by HarfBuzz, with font fallback for accents, emoji and CJK. Your font family and
  monospace font come from `config.toml`, and so does the text size (`[font] size`, 13 by
  default): the whole interface grows or shrinks with it in proportion (rows, paddings, icons,
  the sidebar, Quick Look's text), as sharp as at a fractional scale, while the window's frame and
  shadow stay as they are; the lists' zoom goes on from there (0rgl's issue #15).
- **One header, as a modern file manager** (GNOME Files, Finder): Nordend asks the compositor
  to leave the title bar to it (xdg-decoration, client-side). The sidebar runs to the window's
  top; the path row is the header, with back and forward (dimmed when there is nowhere to go;
  they act on release over them), the path, and minimize (when the compositor can), maximize
  (restore when maximized) and close at the right, close red under the pointer. Its free space
  and the top of the sidebar move the window, a double click maximizes it, a right click opens
  the compositor's window menu, also maximized; never while a menu, a dialog or Quick Look is
  open. Its colour is the theme's header bar (GTK's `headerbar_bg_color`, else the content's).
  With two tabs or more, the tabs come under it. The window's edges and corners resize it, with
  the matching arrows (not when maximized, nor on an edge tiled against the screen). No "Nordend"
  title: the path says where you are. `[window] title_bar = "compositor"` keeps the compositor's
  title bar (the path back in its own row, at the next start); a compositor that insists on its
  own bar has the last word, and one that draws none always gets Nordend's header.
- **Rounded corners and a shadow** around Nordend's own header, as GTK draws them (radius 12 px,
  `[window] corner_radius` from 0, square, to 24, live; a lighter shadow when the window is in the
  back): the window's surface is 24 px larger on every side, the window geometry tells the
  compositor where the window itself is, and the opaque region spares it blending the window. The
  10 px of shadow nearest the window resize it, as in GTK; the rest lets clicks through. None
  when maximized, fullscreen or tiled, nor under the compositor's title bar.
- **A see-through background** (`[window] background_opacity`, 0-1, live; by barrulus): the
  window's and the panels' backgrounds let the desktop through (and the compositor's blur, where
  it has one), while text, icons, thumbnails and overlays stay solid; the shadow never shows
  through the window. Opaque by default.
- Pointer shapes from the compositor's cursor theme (cursor-shape-v1): a resize arrow on every
  draggable edge, a hand on what a click acts on (the status bar's buttons, a transfer's ×, the
  path's crumbs, a disk's eject icon, "More details", the buttons of a question), the text cursor
  over a name being edited. Where the compositor has no cursor-shape-v1 (GNOME, weston), Nordend
  draws them itself from the xcursor theme (`XCURSOR_THEME`, `XCURSOR_SIZE`, sharp at any
  scale); `NORDEND_FORCE_XCURSOR=1` does so on any compositor.

## Fast, and measured

| What | Time |
|------|-----:|
| First entries of a 10 000-file folder (warm / cold cache) | 0.8 ms / 1.7 ms |
| The whole folder loaded and sorted (warm / cold) | 19.5 ms / 81 ms |
| A scrolling frame at 2880x1620 (columns / grid / details) | 0.26 / 0.30 / 0.36-0.50 ms |
| Sorting 10 000 entries by size, date or type | 1-3 ms |
| Quick Look of an ORF on screen (its thumbnail at once) | 80-117 ms, median about 102 (target 100) |
| Reading every installed app to choose one | 0.5 ms |
| Copying 342 photos, 3.1 GB, warm cache (Nordend / `cp -a`) | 1.39 s / 1.39 s |
| CPU while nothing moves | 0 |

A 4.26 MB binary (3.55 MB stripped) with 17 direct libraries; 24 MB of its own memory at start.
Pictures are decoded in the `nordend-preview` helpers, which give their memory back after a quiet
second (binary measured 2026-11-14, memory 2026-11-13).

## Settings

- `~/.config/nordend/config.toml`: font family, monospace font, text size, double-click time and distance,
  the icon in the accent ([example](config.example.toml)). `nordend --check-config` reports every problem with its
  line.
- No settings window: the defaults are the system's fonts (fontconfig's `sans-serif` and
  `monospace`) and the desktop's colours, and the few settings live in the file.
  `config.toml` is followed while Nordend runs: the double click, the text size and the icon change at once,
  the font families at the next start; a file with problems keeps the settings in use, with the problems in the log.
- **`nordend --about`**: Nordend's version and commit, the distribution, the kernel, the
  desktop, the versions of Noctalia and Umbriel when they are running (asked of the very
  programs running, never of another copy) and the GPU (from a surfaceless OpenGL context), one
  "Label: value" line each, ready to paste into a bug report; and the engines found, with
  their programs.
- Colors follow the user's theme with nothing to set up, as GTK apps do: GTK's named colours
  (`@define-color`) from `~/.config/gtk-4.0/gtk.css`, else `gtk-3.0/gtk.css`, with what they
  import, read again live when any of those files changes (written by Noctalia's GTK theming,
  Stylix, matugen, or by hand). The window, view, sidebar, card, popover, text, accent and
  error colours go to the places that show them; the other levels are derived around them
  (OKLCH), text kept readable. Values as GTK writes them: `#hex`, `rgb()`/`rgba()`
  (translucent ones drawn over the window), references; computed ones (`shade()`, `alpha()`)
  are left out. GTK's folders are only read, never created. Without GTK colours, Nordend
  follows the desktop through the freedesktop portal (GNOME, Plasma): light or dark, and the
  accent colour for the selection, icons and links, with neutral surfaces and text kept
  readable (WCAG AA contrast, tested for 15 accents). Without a portal either, a built-in
  default palette is used. `~/.config/nordend/colors.toml` (every role by name, as the
  Noctalia palette names them) still overrides all of them, for whoever wants exact colours.
- **Remembered from one start to the next** (`~/.local/state/nordend/state.toml`, written by
  Nordend): the view, the details' order and column widths, hidden files shown or not, the
  sidebar and the info pane (shown, width, "More details"), the window's size and whether it was
  maximized; with two tabs or more, the tabs (each one's folder, view, cursor and, in the columns,
  where they start) and which was shown, back when Nordend starts without a folder (Super+E;
  folders gone meanwhile are left out). With one tab, not the last folder: Nordend opens Home, or
  the folder given. Not the back/forward history. A broken file only means the defaults.

## Standards it follows

freedesktop thumbnails, shared-mime-info, MIME applications associations, Desktop Entry, XDG
base and user directories, GTK bookmarks; Wayland xdg-shell, xdg-decoration, fractional-scale,
viewporter, cursor-shape, xdg-activation, xdg-foreign and xdg-dialog; xdg-desktop-portal's
FileChooser and portals.conf; org.freedesktop.FileManager1.

## Next

0.9.0 is out (archives: zip, tar and tar.gz browsed like folders by Nordend's own caged reader, looked at with Quick Look, extracted safely and made from the menu; `.iso` images mounted; from veinhusk's issue #13; an RPM spec from 0rgl), after 0.8.1 (a name with room drawn whole at any text size, from 0rgl's pull request), after 0.8.0 (copying about twice as fast: a folder's files on eight threads, close to fcp and
ahead of `cp`; merging folders asks about each taken file, and Ctrl+Z after a merge takes away
only what was copied; Shift+Escape stops a transfer), after 0.7.6 (programs and AppImages started with Enter, safely, "Allow Running as Program" and
a dot on what can run, from Evergaster's issue #17; "Show in Folder" opening Nordend, from 0rgl's
issue #16), after 0.7.5 (the text size in `config.toml`, from 0rgl's issue #15; actions of your own in the
context menu, `[[action]]`, from 0rgl's issue #14; a folder's size in the info pane, or several
selected), after 0.7.4 (small fixes: the developer tools answer `--help` and refuse unknown options; an
ordinary logout is no longer logged as an error), after 0.7.3 (scrollbars that widen under the
pointer and can be dragged; the shortcuts card scrolls in a low window, from llego's issue #12),
0.7.2 (nordend-setup: Nordend
installed, updated, set up and removed by one script on any distribution, tried in CI on
Debian, Ubuntu, Fedora, Arch and openSUSE), 0.7.1 (Quick Look and grid thumbnails faster: the image scaler rewritten, portrait
photos fitted to the screen; the sidebar's bookmarks renamed, pointed elsewhere and reordered,
from Evergaster's issue #10), after 0.7.0 (Nordend as the file chooser of other applications
through xdg-desktop-portal: opening, folders, filters, saving, `nordend --set-file-chooser`;
tried on Umbriel, GNOME and Plasma), 0.6.2 (Ctrl+Shift+C copies the folder's path; the terminal chosen in
`config.toml`; both from Evergaster's issue #9), 0.6.1 (rounded corners and a shadow around Nordend's header,
square on request; a see-through background by barrulus; a clean exit at logout), 0.6.0 (the colours of your GTK
theme, read live with nothing to set up; one header for the whole window), 0.5.1 (grid thumbnails up to 512 px, hardened builds, a Nix flake,
sorting by type for names with several types), 0.5.0 (text previews with syntax colours, selecting and
copying, logs from their end; the helper became the first engine), 0.4.0 (image previews read by Nordend's own code in caged
helpers: DNG, TIFF, raw previews and EXIF without exiv2), 0.3.1 (more raw previews), 0.3.0 (the
command palette, finding files, the icon in the accent), 0.2.0 (Sort By, tabs, zoom in the
lists) and the first release, 0.1.0. Then, roughly in this order: network shares in the
sidebar, other common formats, colour labels and ratings, a Quick Look for photographers, a
dual pane. See the README's
[What's next](../README.md#whats-next).
