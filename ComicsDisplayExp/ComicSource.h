#pragma once

// A comic "book" is an ordered list of page images coming from an archive
// (cbz/cbr/cb7/cbt/zip/rar/7z/tar) or from a plain folder of images.

namespace cdx
{
    using PageBytes = std::shared_ptr<const std::vector<uint8_t>>;

    class ComicSource
    {
    public:
        virtual ~ComicSource() = default;

        size_t PageCount() const noexcept { return m_names.size(); }
        const std::wstring& PageName(size_t index) const { return m_names[index]; }

        // Returns the encoded bytes of a page. May block until the page has been
        // extracted. Returns nullptr on failure or after Cancel().
        virtual PageBytes ReadPage(size_t index) = 0;

        // Unblocks pending ReadPage calls and stops background work.
        virtual void Cancel() noexcept {}

    protected:
        std::vector<std::wstring> m_names; // display names, natural sort order
    };

    // Opens a comic archive, a folder, or the folder containing an image file.
    // On success, *startPage receives the page to show first (non-zero when an
    // individual image was opened). Throws winrt::hresult_error on failure.
    std::shared_ptr<ComicSource> OpenComic(std::wstring const& path, size_t* startPage);

    bool IsImageFileName(std::wstring_view name) noexcept;
    bool IsComicArchiveName(std::wstring_view name) noexcept;

    // Lists openable comics (archives and image folders) next to `path`,
    // in natural sort order. Used for "next / previous book".
    std::vector<std::wstring> ListSiblingComics(std::wstring const& path);

    // Explorer-style "logical" comparison: "page2" < "page10".
    inline bool NaturalLess(std::wstring const& a, std::wstring const& b) noexcept
    {
        return StrCmpLogicalW(a.c_str(), b.c_str()) < 0;
    }
}
