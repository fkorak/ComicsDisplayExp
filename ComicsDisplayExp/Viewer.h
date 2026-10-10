#pragma once

#include "ComicSource.h"
#include "PageLoader.h"
#include "Renderer.h"

namespace cdx
{
    enum class FitMode : int
    {
        Page = 0,   // whole page(s) visible
        Width = 1,  // fill window width, scroll vertically
        Height = 2, // fill window height, scroll horizontally
        Actual = 3, // one image pixel per screen pixel
    };

    struct ViewOptions
    {
        FitMode fit = FitMode::Page;
        bool spread = false;     // two pages side by side
        bool manga = false;      // right-to-left reading
        bool coverAlone = true;  // first page is never paired in spread mode
        bool enlargeSmall = true; // fit modes may scale pages above 100%
    };

    // What is remembered per comic (see ReadingHistory).
    struct BookState
    {
        size_t page = 0;
        FitMode fit = FitMode::Page;
        float zoom = 1.0f;
        bool spread = false;
        bool manga = false;
        int rotation = 0;

        bool operator==(BookState const&) const = default;
    };

    // Navigation state and page layout, independent of XAML. All coordinates
    // are in physical pixels of the swap chain.
    class Viewer
    {
    public:
        explicit Viewer(PageLoader& loader) : m_loader(loader) {}

        void SetBook(std::shared_ptr<ComicSource> source, size_t startPage);
        bool HasBook() const noexcept { return m_source != nullptr; }
        ComicSource const* Book() const noexcept { return m_source.get(); }
        size_t PageCount() const noexcept { return m_source ? m_source->PageCount() : 0; }
        size_t CurrentPage() const noexcept { return m_page; }
        size_t VisibleCount() const noexcept;

        ViewOptions& Options() noexcept { return m_options; }
        BookState CaptureState() const noexcept;
        void ApplyState(BookState const& state); // after SetBook
        float Zoom() const noexcept { return m_zoom; }
        int Rotation() const noexcept { return m_rotation; }

        void SetViewport(float width, float height) noexcept;

        // Navigation. Return false when nothing changed (e.g. already at the end).
        bool NextPage();
        bool PrevPage();
        bool GoTo(size_t page, bool anchorEnd = false);
        bool ScrollOrNext(float fraction = 0.85f); // Space: scroll down, then turn the page
        bool ScrollOrPrev(float fraction = 0.85f);
        bool ScrollBy(float dx, float dy, bool smooth = false); // smooth: animate if enabled

        // Smooth scrolling: scroll inputs set a target that StepScroll() eases towards.
        void SetSmoothScrolling(bool enabled) noexcept;
        bool SmoothScrolling() const noexcept { return m_smoothScrolling; }
        bool IsScrollAnimating() const noexcept;
        bool StepScroll(float seconds); // returns true while still moving

        void SetFit(FitMode fit);
        void ToggleSpread();
        void ToggleManga();
        void Rotate(int quarterTurns);
        void ZoomBy(float factor);
        void ResetZoom();

        // True if is `index` one of the pages the current frame depends on.
        bool IsPageRelevant(size_t index) const noexcept { return index >= m_page && index <= m_page + 1; }

        // Lays out the current page(s). Returns false if they are still loading.
        bool BuildFrame(Frame& frame);

    private:
        struct Layout
        {
            bool valid = false;
            size_t page = 0;
            size_t visible = 1;
            float displayWidth = 0, displayHeight = 0;
            float maxScrollX = 0, maxScrollY = 0;
        };
        enum class Anchor { None, Start, End };

        void UpdateFocus(bool forward);
        void SetScrollTarget(float x, float y, bool smooth);
        size_t StepForward() const noexcept;
        size_t FallbackVisibleCount() const noexcept;
        bool IsWide(size_t index) const;

        PageLoader& m_loader;
        std::shared_ptr<ComicSource> m_source;
        ViewOptions m_options;
        size_t m_page = 0;
        float m_viewWidth = 1, m_viewHeight = 1;
        float m_scrollX = 0, m_scrollY = 0; // displayed scroll position
        float m_targetX = 0, m_targetY = 0; // where scrolling is heading
        bool m_smoothScrolling = true;
        float m_zoom = 1.0f;
        int m_rotation = 0; // quarter turns clockwise
        Anchor m_anchor = Anchor::Start;
        Layout m_layout;
    };
}
