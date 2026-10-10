#include "pch.h"
#include "ComicSource.h"

#include <archive.h>
#include <archive_entry.h>

namespace cdx
{
    namespace
    {
        constexpr std::wstring_view kImageExtensions[] = {
            L".jpg", L".jpeg", L".jpe", L".jfif", L".png", L".gif", L".bmp", L".tif", L".tiff",
            L".webp", L".avif", L".heic", L".heif", L".jxl", L".jxr", L".wdp",
        };

        constexpr std::wstring_view kArchiveExtensions[] = {
            L".cbz", L".cbr", L".cb7", L".cbt", L".zip", L".rar", L".7z", L".tar",
        };

        template <size_t N>
        bool HasExtension(std::wstring_view name, std::wstring_view const (&extensions)[N]) noexcept
        {
            auto dot = name.rfind(L'.');
            if (dot == std::wstring_view::npos) return false;
            auto ext = name.substr(dot);
            for (auto e : extensions)
            {
                if (CompareStringOrdinal(ext.data(), static_cast<int>(ext.size()),
                                         e.data(), static_cast<int>(e.size()), TRUE) == CSTR_EQUAL)
                {
                    return true;
                }
            }
            return false;
        }

        std::wstring_view FileNamePart(std::wstring_view path) noexcept
        {
            auto pos = path.find_last_of(L"/\\");
            return pos == std::wstring_view::npos ? path : path.substr(pos + 1);
        }

        std::wstring ParentDirectory(std::wstring path)
        {
            while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
            auto pos = path.find_last_of(L"/\\");
            if (pos == std::wstring::npos) return L".";
            path.resize(pos == 2 && path[1] == L':' ? pos + 1 : pos); // keep "C:\"
            return path;
        }

        std::wstring JoinPath(std::wstring const& dir, std::wstring_view name)
        {
            std::wstring result = dir;
            if (!result.empty() && result.back() != L'\\' && result.back() != L'/') result += L'\\';
            result += name;
            return result;
        }

        // Mac resource forks and hidden files are never pages.
        bool IsJunkEntry(std::wstring_view path) noexcept
        {
            if (path.find(L"__MACOSX") != std::wstring_view::npos) return true;
            auto name = FileNamePart(path);
            return name.empty() || name.front() == L'.';
        }

        std::wstring MultiByteToWide(const char* text, UINT codePage)
        {
            int len = MultiByteToWideChar(codePage, 0, text, -1, nullptr, 0);
            if (len <= 1) return {};
            std::wstring result(static_cast<size_t>(len - 1), L'\0');
            MultiByteToWideChar(codePage, 0, text, -1, result.data(), len);
            return result;
        }

        std::wstring EntryName(archive_entry* entry)
        {
            if (const wchar_t* w = archive_entry_pathname_w(entry)) return w;
            if (const char* u = archive_entry_pathname_utf8(entry)) return MultiByteToWide(u, CP_UTF8);
            if (const char* m = archive_entry_pathname(entry)) return MultiByteToWide(m, CP_ACP);
            return {};
        }

        [[noreturn]] void ThrowError(std::wstring const& message)
        {
            throw winrt::hresult_error(E_FAIL, winrt::hstring{ message });
        }

        // ---------------------------------------------------------------- folders

        class FolderSource final : public ComicSource
        {
        public:
            explicit FolderSource(std::wstring const& directory)
            {
                WIN32_FIND_DATAW data{};
                wil::unique_hfind find{ FindFirstFileExW(JoinPath(directory, L"*").c_str(), FindExInfoBasic, &data,
                                                         FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH) };
                if (!find) ThrowError(L"Cannot read folder: " + directory);
                do
                {
                    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;
                    if (IsImageFileName(data.cFileName)) m_names.emplace_back(data.cFileName);
                } while (FindNextFileW(find.get(), &data));

                std::sort(m_names.begin(), m_names.end(), NaturalLess);
                m_paths.reserve(m_names.size());
                for (auto const& name : m_names) m_paths.push_back(JoinPath(directory, name));
            }

            PageBytes ReadPage(size_t index) override
            {
                wil::unique_hfile file{ CreateFileW(m_paths[index].c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                                    nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr) };
                if (!file) return nullptr;
                LARGE_INTEGER size{};
                if (!GetFileSizeEx(file.get(), &size) || size.QuadPart > 0x7FFFFFFF) return nullptr;

                auto buffer = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(size.QuadPart));
                size_t offset = 0;
                while (offset < buffer->size())
                {
                    DWORD read = 0;
                    DWORD chunk = static_cast<DWORD>(std::min<size_t>(buffer->size() - offset, 16u << 20));
                    if (!ReadFile(file.get(), buffer->data() + offset, chunk, &read, nullptr) || read == 0) return nullptr;
                    offset += read;
                }
                return buffer;
            }

        private:
            std::vector<std::wstring> m_paths;
        };

        // --------------------------------------------------------------- archives

        struct ArchiveDeleter
        {
            void operator()(archive* a) const noexcept { archive_read_free(a); }
        };
        using ArchivePtr = std::unique_ptr<archive, ArchiveDeleter>;

        ArchivePtr OpenArchive(std::wstring const& path)
        {
            ArchivePtr a{ archive_read_new() };
            archive_read_support_format_zip(a.get());
            archive_read_support_format_rar(a.get());
            archive_read_support_format_rar5(a.get());
            archive_read_support_format_7zip(a.get());
            archive_read_support_format_tar(a.get());
            archive_read_support_filter_all(a.get());
            if (archive_read_open_filename_w(a.get(), path.c_str(), 1 << 20) != ARCHIVE_OK)
            {
                const char* err = archive_error_string(a.get());
                ThrowError(L"Cannot open archive: " + (err ? MultiByteToWide(err, CP_UTF8) : path));
            }
            return a;
        }

        // Pages are indexed with one cheap header pass, then a background thread
        // streams every page into memory in archive order (the only fast order for
        // solid RAR / 7z). Readers block only until their page has been extracted,
        // so the first page shows as soon as its bytes are in.
        class ArchiveSource final : public ComicSource
        {
        public:
            explicit ArchiveSource(std::wstring path) : m_path(std::move(path))
            {
                struct Item
                {
                    std::wstring name;
                    size_t ordinal;
                };
                std::vector<Item> items;
                size_t ordinal = 0;
                {
                    auto a = OpenArchive(m_path);
                    archive_entry* entry = nullptr;
                    int r;
                    while ((r = archive_read_next_header(a.get(), &entry)) == ARCHIVE_OK || r == ARCHIVE_WARN)
                    {
                        if (archive_entry_filetype(entry) == AE_IFREG)
                        {
                            auto name = EntryName(entry);
                            if (IsImageFileName(name) && !IsJunkEntry(name)) items.push_back({ std::move(name), ordinal });
                        }
                        ++ordinal;
                    }
                }
                if (items.empty()) ThrowError(L"No images found in " + m_path);

                std::stable_sort(items.begin(), items.end(),
                                 [](Item const& x, Item const& y) { return NaturalLess(x.name, y.name); });

                m_pageOfOrdinal.assign(ordinal, SIZE_MAX);
                m_names.reserve(items.size());
                for (size_t page = 0; page < items.size(); ++page)
                {
                    m_pageOfOrdinal[items[page].ordinal] = page;
                    m_names.push_back(std::move(items[page].name));
                }
                m_slots.resize(m_names.size());
                m_thread = std::thread([this] { ExtractAll(); });
            }

            ~ArchiveSource() override
            {
                Cancel();
                if (m_thread.joinable()) m_thread.join();
            }

            PageBytes ReadPage(size_t index) override
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [&] { return m_slots[index].done || m_finished || m_cancel.load(); });
                return m_slots[index].data;
            }

            void Cancel() noexcept override
            {
                {
                    std::lock_guard lock(m_mutex);
                    m_cancel = true;
                }
                m_cv.notify_all();
            }

        private:
            struct Slot
            {
                PageBytes data;
                bool done = false;
            };

            void ExtractAll() noexcept
            {
                SetThreadDescription(GetCurrentThread(), L"cdx archive extractor");
                try
                {
                    auto a = OpenArchive(m_path);
                    archive_entry* entry = nullptr;
                    size_t ordinal = 0;
                    int r;
                    while (!m_cancel && ((r = archive_read_next_header(a.get(), &entry)) == ARCHIVE_OK || r == ARCHIVE_WARN))
                    {
                        size_t page = ordinal < m_pageOfOrdinal.size() ? m_pageOfOrdinal[ordinal] : SIZE_MAX;
                        ++ordinal;
                        if (page == SIZE_MAX) continue; // libarchive skips unread data

                        auto data = ReadEntry(a.get(), entry);
                        {
                            std::lock_guard lock(m_mutex);
                            m_slots[page] = { std::move(data), true };
                        }
                        m_cv.notify_all();
                    }
                }
                catch (...)
                {
                }
                {
                    std::lock_guard lock(m_mutex);
                    m_finished = true;
                }
                m_cv.notify_all();
            }

            PageBytes ReadEntry(archive* a, archive_entry* entry)
            {
                auto buffer = std::make_shared<std::vector<uint8_t>>();
                la_int64_t expected = archive_entry_size_is_set(entry) ? archive_entry_size(entry) : 0;
                if (expected < 0 || expected > 0x7FFFFFFF) expected = 0;
                buffer->resize(expected > 0 ? static_cast<size_t>(expected) : (1u << 20));

                size_t offset = 0;
                for (;;)
                {
                    if (m_cancel) return nullptr;
                    if (offset == buffer->size()) buffer->resize(buffer->size() * 2);
                    size_t chunk = std::min<size_t>(buffer->size() - offset, 4u << 20);
                    la_ssize_t n = archive_read_data(a, buffer->data() + offset, chunk);
                    if (n < 0) return nullptr; // corrupt or encrypted entry
                    if (n == 0) break;
                    offset += static_cast<size_t>(n);
                }
                buffer->resize(offset);
                return buffer;
            }

            std::wstring m_path;
            std::vector<size_t> m_pageOfOrdinal;
            std::vector<Slot> m_slots;
            std::mutex m_mutex;
            std::condition_variable m_cv;
            bool m_finished = false;
            std::atomic<bool> m_cancel{ false };
            std::thread m_thread;
        };
    }

    bool IsImageFileName(std::wstring_view name) noexcept { return HasExtension(name, kImageExtensions); }
    bool IsComicArchiveName(std::wstring_view name) noexcept { return HasExtension(name, kArchiveExtensions); }

    std::shared_ptr<ComicSource> OpenComic(std::wstring const& path, size_t* startPage)
    {
        *startPage = 0;
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) ThrowError(L"File not found: " + path);

        std::shared_ptr<ComicSource> source;
        if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            source = std::make_shared<FolderSource>(path);
        }
        else if (IsImageFileName(path))
        {
            source = std::make_shared<FolderSource>(ParentDirectory(path));
            auto name = FileNamePart(path);
            for (size_t i = 0; i < source->PageCount(); ++i)
            {
                if (CompareStringOrdinal(source->PageName(i).c_str(), -1, name.data(), static_cast<int>(name.size()), TRUE) == CSTR_EQUAL)
                {
                    *startPage = i;
                    break;
                }
            }
        }
        else
        {
            source = std::make_shared<ArchiveSource>(path);
        }

        if (source->PageCount() == 0) ThrowError(L"No images found in " + path);
        return source;
    }

    std::vector<std::wstring> ListSiblingComics(std::wstring const& path)
    {
        std::vector<std::wstring> names;
        std::wstring directory = ParentDirectory(path);
        bool includeFolders = (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;

        WIN32_FIND_DATAW data{};
        wil::unique_hfind find{ FindFirstFileExW(JoinPath(directory, L"*").c_str(), FindExInfoBasic, &data,
                                                 FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH) };
        if (!find) return {};
        do
        {
            std::wstring_view name = data.cFileName;
            if (name == L"." || name == L".." || (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
            bool isFolder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (isFolder ? includeFolders : IsComicArchiveName(name)) names.emplace_back(name);
        } while (FindNextFileW(find.get(), &data));

        std::sort(names.begin(), names.end(), NaturalLess);
        for (auto& name : names) name = JoinPath(directory, name);
        return names;
    }
}
