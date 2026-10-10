# ComicsDisplayExp

A fast comic book viewer for Windows, inspired by CDisplayEx. Native C++20, WinUI 3 (Windows App SDK), Direct2D.

## Formats

- Archives: CBZ/ZIP, CBR/RAR (RAR4 and RAR5), CB7/7z, CBT/tar (via libarchive)
- Folders of images, or a single image (opens its folder at that image)
- Page images: anything WIC decodes. JPEG, PNG, GIF, BMP, TIFF and JPEG XR are built in. WebP, HEIF/AVIF and JPEG XL work when their Windows codec extensions are installed.

## Why it is fast

| Stage | What happens |
|---|---|
| Open | One header pass indexes the archive and sorts pages naturally (`page2` < `page10`). A background thread then streams every page into RAM in archive order, which is the only fast order for solid RAR/7z. A reader blocks only until its own page has arrived. |
| Decode | A pool of worker threads decodes with WIC and uploads straight to GPU bitmaps (multi-threaded D2D factory). The UI thread never decodes. |
| Prefetch | Pages around the current one are decoded ahead of time, favouring the reading direction (6 ahead, 2 behind). Turning a page is just a redraw. |
| Cache | Up to 1 GB of decoded pages stay on the GPU. The pages farthest from the current one are evicted first. |
| Render | A `SwapChainPanel` composition swap chain at native DPI. Scaling, zoom, rotation and spreads are a single GPU transform with high-quality cubic filtering. No XAML image elements are involved. |

## Keyboard and mouse

| Input | Action |
|---|---|
| → / PgDn / mouse forward button | Next page (← in manga mode) |
| ← / PgUp / Backspace / mouse back button | Previous page |
| Space / ↓ / wheel | Scroll, then next page. Shift+Space / ↑ go back. |
| Home / End | First / last page |
| Ctrl+→ / Ctrl+← or `]` / `[` | Next / previous comic in the folder |
| `F` `W` `H` `A` | Fit page / width / height, actual size |
| `1` / `2` | Single page / two-page spread (wide pages always show alone) |
| `C` | Cover alone in spread mode (on by default) |
| `M` | Manga mode (right to left) |
| `R` / Shift+`R` | Rotate |
| `+` `-` `0` / Ctrl+wheel / drag | Zoom in, out, reset; pan |
| F11 / Enter / middle click | Fullscreen (the menu bar slides in at the top edge) |
| F10 / Menu key / ☰ button | Main menu |
| `I` | Page info overlay |
| `T` | Cycle theme: follow Windows → light → dark |
| F1 | Shortcut help |
| Ctrl+O / Ctrl+Shift+O | Open file / folder |
| Esc | Leave fullscreen or quit |

You can also open a comic by dropping it on the window or by passing it on the command line (`ComicsDisplayExp.exe "My Comic.cbz"`).

**File associations** (☰ → File associations, per user, no admin rights needed):
- *Default viewer for comic books*: makes CBZ, CBR, CB7 and CBT files open in ComicsDisplayExp. If you previously picked another app in Windows, Windows insists on confirming the change itself, so the Default apps page opens on ComicsDisplayExp's entry.
- *Show in "Open with" for ZIP, RAR and 7Z*: adds the app to those files' "Open with" menu without changing what opens them by default.
- *Remove all file associations*: undoes both and restores the previous defaults.

The registration follows the exe: if you move the app, it updates itself the next time it starts.

The app opens maximized (F11 for fullscreen) and remembers the window size and position you leave it in. Its look follows Windows' light/dark setting by default; ☰ → Theme (or `T`) switches between *Use system setting*, *Light* and *Dark*.
Scrolling (wheel, Space, ↑/↓) is animated by default; turn it off under ☰ → View → Smooth scrolling.

Image quality (☰ → View): pages are scaled on the GPU with a high-quality cubic filter. **Sharpening** (Off / Light / Medium / Strong) sharpens the page *after* scaling, at screen resolution, which restores the ink edges that shrinking a large scan softens; Light suits most scans, Strong is for very soft ones. **Enlarge small pages** (on by default) can be switched off so low-resolution scans are shown at 100% instead of being stretched and blurred.

Each comic remembers how you left it: page, fit mode, zoom, spread, manga and rotation come back when you reopen it. This is stored in `%LOCALAPPDATA%\ComicsDisplayExp\history.tsv`, saved a few seconds after you stop changing things; comics not opened for 4 months are forgotten automatically. Defaults for new comics, window placement and recent files are in `settings.ini` next to it.

## Building

Requirements: Visual Studio 2026 with the *Desktop development with C++* workload (MSVC v145, Windows SDK 10.0.26100, bundled vcpkg).

```powershell
# NuGet packages (Windows App SDK, C++/WinRT, WIL)
msbuild ComicsDisplayExp.sln -t:restore -p:RestorePackagesConfig=true
# Build; vcpkg installs libarchive (+ zlib, bzip2, xz, zstd) automatically on the first build
msbuild ComicsDisplayExp.sln -p:Configuration=Release -p:Platform=x64
```

Or open `ComicsDisplayExp.sln` in Visual Studio and press F5.

The app is unpackaged and self-contained: `x64\Release\ComicsDisplayExp\` contains the whole Windows App SDK runtime and needs no installer.

### Installer

```powershell
.\installer\build-setup.ps1                     # build + package -> dist\ComicsDisplayExp-Setup-<version>-x64.exe
.\installer\build-setup.ps1 -InstallInnoSetup   # same, installing Inno Setup (per user) first if needed
.\installer\build-setup.ps1 -Platform ARM64 -Version 1.2.0
```

The setup ([installer/ComicsDisplayExp.iss](installer/ComicsDisplayExp.iss), Inno Setup 6) installs per user without admin rights by default, adds a Start menu entry, and offers to make the app the default comic viewer or add it to "Open with" for ZIP/RAR/7Z. Running a newer setup upgrades in place; uninstalling also removes the file associations. The version defaults to the newest `vX.Y.Z` git tag.

### Continuous builds

[.github/workflows/build.yml](.github/workflows/build.yml) builds every push to `main` and every pull request on GitHub's Windows runners (x64; ARM64 as an experimental extra), using the same `build-setup.ps1` for the installer. Each run attaches the ready-to-run app folder and the setup as artifacts. Pushing a tag such as `v0.2.0` also creates a GitHub release with the setup and a zip.

## Source layout

| File | Responsibility |
|---|---|
| `ComicSource.*` | Archive and folder reading, natural sort, sibling comics |
| `GraphicsDevice.*` | D3D11 / D2D / WIC device objects, shared across threads |
| `PageLoader.*` | Decode thread pool, prefetch priorities, GPU page cache |
| `Viewer.*` | Navigation state and layout (fit, spread, manga, zoom, scroll, rotation) |
| `Renderer.*` | Swap chain and Direct2D drawing |
| `Settings.*` | INI persistence (defaults, window, recent files) |
| `ReadingHistory.*` | Per-comic state (page, fit, zoom, …) with automatic expiry |
| `MainWindow.*` | WinUI window, input, file dialog, drag and drop, fullscreen |
