#pragma once

#include "Viewer.h"

namespace cdx
{
    // Per-comic reading state (page, fit, zoom, spread, manga, rotation), kept in
    // %LOCALAPPDATA%\ComicsDisplayExp\history.tsv.
    //
    // The whole file is loaded into memory at startup (a few hundred bytes per comic),
    // so lookups are free. Writes replace the file atomically. Entries not opened for
    // kMaxAge are dropped on every load, and only the kMaxEntries most recent are kept.
    class ReadingHistory
    {
    public:
        ReadingHistory();

        void Load();        // also prunes stale entries
        void SaveIfDirty(); // atomic replace; no-op when nothing changed

        std::optional<BookState> Find(std::wstring const& bookPath) const;
        void Record(std::wstring const& bookPath, BookState const& state); // also stamps "last opened"

    private:
        struct Entry
        {
            std::wstring path; // original spelling, for readability of the file
            int64_t lastOpened = 0; // seconds since the Unix epoch
            BookState state;
        };

        static std::wstring Key(std::wstring const& path);
        void Prune();

        std::wstring m_file;
        std::unordered_map<std::wstring, Entry> m_entries; // keyed by lower-cased path
        bool m_dirty = false;
    };
}
