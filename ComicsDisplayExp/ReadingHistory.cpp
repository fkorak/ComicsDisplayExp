#include "pch.h"
#include "ReadingHistory.h"

#include <ShlObj.h>
#include <chrono>
#include <format>

namespace cdx
{
    namespace
    {
        constexpr wchar_t kHeader[] = L"CDXHISTORY\t1";
        constexpr std::chrono::days kMaxAge{ 120 }; // "a few months"
        constexpr size_t kMaxEntries = 5000;

        int64_t NowSeconds()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty()) return {};
            int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring result(static_cast<size_t>(len), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), len);
            return result;
        }

        std::string WideToUtf8(std::wstring_view text)
        {
            if (text.empty()) return {};
            int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            std::string result(static_cast<size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), len, nullptr, nullptr);
            return result;
        }

        std::vector<std::wstring_view> Split(std::wstring_view line, wchar_t separator)
        {
            std::vector<std::wstring_view> parts;
            size_t start = 0;
            for (;;)
            {
                size_t end = line.find(separator, start);
                parts.push_back(line.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start));
                if (end == std::wstring_view::npos) break;
                start = end + 1;
            }
            return parts;
        }
    }

    ReadingHistory::ReadingHistory()
    {
        wil::unique_cotaskmem_string localAppData;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)))
        {
            std::wstring dir = std::wstring(localAppData.get()) + L"\\ComicsDisplayExp";
            CreateDirectoryW(dir.c_str(), nullptr);
            m_file = dir + L"\\history.tsv";
        }
    }

    std::wstring ReadingHistory::Key(std::wstring const& path)
    {
        std::wstring key = path;
        CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
        return key;
    }

    void ReadingHistory::Load()
    {
        m_entries.clear();
        m_dirty = false;
        if (m_file.empty()) return;

        wil::unique_hfile file{ CreateFileW(m_file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr) };
        if (!file) return;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size) || size.QuadPart > (64 << 20)) return;
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        if (!ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) return;
        bytes.resize(read);
        file.reset();

        std::wstring text = Utf8ToWide(bytes);
        auto lines = Split(text, L'\n');
        if (lines.empty() || lines[0] != kHeader) return; // unknown format: start fresh

        // Line: lastOpened \t page \t fit \t zoom \t spread \t manga \t rotation \t path
        for (size_t i = 1; i < lines.size(); ++i)
        {
            auto fields = Split(lines[i], L'\t');
            if (fields.size() != 8 || fields[7].empty()) continue;
            try
            {
                Entry entry;
                entry.lastOpened = std::stoll(std::wstring(fields[0]));
                entry.state.page = static_cast<size_t>(std::stoull(std::wstring(fields[1])));
                entry.state.fit = static_cast<FitMode>(std::clamp(std::stoi(std::wstring(fields[2])), 0, 3));
                entry.state.zoom = std::clamp(std::stof(std::wstring(fields[3])), 0.05f, 32.0f);
                entry.state.spread = fields[4] == L"1";
                entry.state.manga = fields[5] == L"1";
                entry.state.rotation = std::stoi(std::wstring(fields[6])) & 3;
                entry.path = fields[7];
                m_entries[Key(entry.path)] = std::move(entry);
            }
            catch (...)
            {
                // Skip malformed lines rather than losing the whole history.
            }
        }

        Prune();
        SaveIfDirty();
    }

    void ReadingHistory::Prune()
    {
        int64_t cutoff = NowSeconds() - std::chrono::duration_cast<std::chrono::seconds>(kMaxAge).count();
        size_t before = m_entries.size();
        std::erase_if(m_entries, [&](auto const& item) { return item.second.lastOpened < cutoff; });

        if (m_entries.size() > kMaxEntries)
        {
            std::vector<int64_t> stamps;
            stamps.reserve(m_entries.size());
            for (auto const& [key, entry] : m_entries) stamps.push_back(entry.lastOpened);
            std::nth_element(stamps.begin(), stamps.begin() + (stamps.size() - kMaxEntries), stamps.end());
            int64_t oldestKept = stamps[stamps.size() - kMaxEntries];
            std::erase_if(m_entries, [&](auto const& item) { return item.second.lastOpened < oldestKept; });
        }
        if (m_entries.size() != before) m_dirty = true;
    }

    void ReadingHistory::SaveIfDirty()
    {
        if (!m_dirty || m_file.empty()) return;

        // Newest first, so the file reads like a "recently read" list.
        std::vector<Entry const*> ordered;
        ordered.reserve(m_entries.size());
        for (auto const& [key, entry] : m_entries) ordered.push_back(&entry);
        std::sort(ordered.begin(), ordered.end(), [](Entry const* a, Entry const* b) { return a->lastOpened > b->lastOpened; });

        std::wstring text = kHeader;
        text += L'\n';
        for (Entry const* entry : ordered)
        {
            auto const& s = entry->state;
            text += std::format(L"{}\t{}\t{}\t{:.4f}\t{}\t{}\t{}\t{}\n", entry->lastOpened, s.page, static_cast<int>(s.fit), s.zoom,
                                s.spread ? 1 : 0, s.manga ? 1 : 0, s.rotation, entry->path);
        }
        std::string bytes = WideToUtf8(text);

        // Write a sibling temp file, then swap it in: a crash never leaves a half-written history.
        std::wstring temp = m_file + L".tmp";
        {
            wil::unique_hfile file{ CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
            if (!file) return;
            DWORD written = 0;
            if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written != bytes.size()) return;
        }
        if (MoveFileExW(temp.c_str(), m_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) m_dirty = false;
    }

    std::optional<BookState> ReadingHistory::Find(std::wstring const& bookPath) const
    {
        auto it = m_entries.find(Key(bookPath));
        if (it == m_entries.end()) return std::nullopt;
        return it->second.state;
    }

    void ReadingHistory::Record(std::wstring const& bookPath, BookState const& state)
    {
        Entry& entry = m_entries[Key(bookPath)];
        entry.path = bookPath;
        entry.lastOpened = NowSeconds();
        entry.state = state;
        m_dirty = true;
    }
}
