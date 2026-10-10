#pragma once

// Per-user file type registration (HKCU, no admin rights needed).
//
// Windows does not let apps silently become the default handler: when the user has
// picked another app for a type (Explorer's protected "UserChoice"), only the user can
// change it. RegisterAsComicViewer() therefore registers everything Windows needs and
// reports whether that already made us the default; if not, OpenDefaultAppsSettings()
// opens the Settings page for this app so the user can confirm with a click.
namespace cdx::FileAssociations
{
    // .cbz .cbr .cb7 .cbt: claim them and offer ourselves as their default.
    void RegisterAsComicViewer();
    bool IsDefaultComicViewer();

    // .zip .rar .7z: only add an "Open with" entry; their default app is left alone.
    void SetOpenWithForArchives(bool enabled);
    bool IsInOpenWithForArchives();

    // Undo everything above, restoring previous defaults for the comic types.
    void UnregisterAll();

    // If registered, re-point the commands at this exe (e.g. after the app was moved).
    void RefreshIfRegistered();

    void OpenDefaultAppsSettings();
}
