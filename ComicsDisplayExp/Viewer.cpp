#include "pch.h"
#include "Viewer.h"

namespace cdx
{
    namespace
    {
        // Size used to lay out pages that failed to decode.
        constexpr float kPlaceholderWidth = 1000.0f;
        constexpr float kPlaceholderHeight = 1500.0f;

        void PageSize(PageImage const& image, float& width, float& height) noexcept
        {
            if (image.Failed() || image.width == 0 || image.height == 0)
            {
                width = kPlaceholderWidth;
                height = kPlaceholderHeight;
            }
            else
            {
                width = static_cast<float>(image.width);
                height = static_cast<float>(image.height);
            }
        }
    }

    void Viewer::SetBook(std::shared_ptr<ComicSource> source, size_t startPage)
    {
        m_source = std::move(source);
        m_page = m_source && m_source->PageCount() ? std::min(startPage, m_source->PageCount() - 1) : 0;
        m_zoom = 1.0f;
        m_rotation = 0;
        m_anchor = Anchor::Start;
        m_layout = {};
        UpdateFocus(true);
    }

    BookState Viewer::CaptureState() const noexcept
    {
        return { m_page, m_options.fit, m_zoom, m_options.spread, m_options.manga, m_rotation };
    }

    void Viewer::ApplyState(BookState const& state)
    {
        m_options.fit = state.fit;
        m_options.spread = state.spread;
        m_options.manga = state.manga;
        m_zoom = std::clamp(state.zoom, 0.05f, 32.0f);
        m_rotation = state.rotation & 3;
        if (m_source && m_source->PageCount()) m_page = std::min(state.page, m_source->PageCount() - 1);
        m_anchor = Anchor::Start;
        m_layout = {};
        UpdateFocus(true);
    }

    size_t Viewer::FallbackVisibleCount() const noexcept
    {
        bool pair = m_options.spread && m_page + 1 < PageCount() && !(m_options.coverAlone && m_page == 0);
        return pair ? 2 : 1;
    }

    size_t Viewer::VisibleCount() const noexcept
    {
        return m_layout.valid && m_layout.page == m_page ? m_layout.visible : FallbackVisibleCount();
    }

    size_t Viewer::StepForward() const noexcept { return VisibleCount(); }

    bool Viewer::IsWide(size_t index) const
    {
        auto image = m_loader.Get(index);
        return image && image->IsWide();
    }

    void Viewer::UpdateFocus(bool forward)
    {
        if (m_source) m_loader.SetFocus(m_page, m_options.spread ? 2 : 1, forward);
    }

    void Viewer::SetViewport(float width, float height) noexcept
    {
        m_viewWidth = std::max(1.0f, width);
        m_viewHeight = std::max(1.0f, height);
    }

    bool Viewer::NextPage()
    {
        if (!m_source) return false;
        size_t next = m_page + StepForward();
        if (next >= PageCount()) return false;
        m_page = next;
        m_anchor = Anchor::Start;
        UpdateFocus(true);
        return true;
    }

    bool Viewer::PrevPage()
    {
        if (!m_source || m_page == 0) return false;
        size_t prev = m_page - 1;
        if (m_options.spread && prev >= 1)
        {
            // Step back over a whole spread if the two previous pages would pair up.
            size_t first = prev - 1;
            bool pairable = !(m_options.coverAlone && first == 0) && !IsWide(first) && !IsWide(prev);
            if (pairable) prev = first;
        }
        m_page = prev;
        m_anchor = Anchor::Start;
        UpdateFocus(false);
        return true;
    }

    bool Viewer::GoTo(size_t page, bool anchorEnd)
    {
        if (!m_source) return false;
        page = std::min(page, PageCount() - 1);
        if (page == m_page) return false;
        bool forward = page > m_page;
        m_page = page;
        m_anchor = anchorEnd ? Anchor::End : Anchor::Start;
        UpdateFocus(forward);
        return true;
    }

    // Scrolling works on a target position. With smooth scrolling on, the displayed
    // position eases towards it in StepScroll(); otherwise it jumps there at once.
    void Viewer::SetScrollTarget(float x, float y, bool smooth)
    {
        m_targetX = x;
        m_targetY = y;
        if (!smooth || !m_smoothScrolling)
        {
            m_scrollX = x;
            m_scrollY = y;
        }
    }

    bool Viewer::IsScrollAnimating() const noexcept
    {
        return std::abs(m_targetX - m_scrollX) > 0.5f || std::abs(m_targetY - m_scrollY) > 0.5f;
    }

    bool Viewer::StepScroll(float seconds)
    {
        if (!IsScrollAnimating())
        {
            m_scrollX = m_targetX;
            m_scrollY = m_targetY;
            return false;
        }
        // Exponential ease-out: covers ~63% of the remaining distance every kTimeConstant.
        constexpr float kTimeConstant = 0.07f;
        float t = 1.0f - std::exp(-seconds / kTimeConstant);
        m_scrollX += (m_targetX - m_scrollX) * t;
        m_scrollY += (m_targetY - m_scrollY) * t;
        if (!IsScrollAnimating())
        {
            m_scrollX = m_targetX;
            m_scrollY = m_targetY;
            return false;
        }
        return true;
    }

    void Viewer::SetSmoothScrolling(bool enabled) noexcept
    {
        m_smoothScrolling = enabled;
        if (!enabled)
        {
            m_scrollX = m_targetX;
            m_scrollY = m_targetY;
        }
    }

    bool Viewer::ScrollOrNext(float fraction)
    {
        if (!m_layout.valid || m_layout.page != m_page) return NextPage();

        if (m_targetY < m_layout.maxScrollY - 0.5f)
        {
            SetScrollTarget(m_targetX, std::min(m_layout.maxScrollY, m_targetY + m_viewHeight * fraction), true);
            return true;
        }
        if (m_layout.maxScrollX > 0.5f)
        {
            float step = m_viewWidth * fraction;
            if (!m_options.manga && m_targetX < m_layout.maxScrollX - 0.5f)
            {
                SetScrollTarget(std::min(m_layout.maxScrollX, m_targetX + step), 0, true);
                return true;
            }
            if (m_options.manga && m_targetX > 0.5f)
            {
                SetScrollTarget(std::max(0.0f, m_targetX - step), 0, true);
                return true;
            }
        }
        return NextPage();
    }

    bool Viewer::ScrollOrPrev(float fraction)
    {
        if (!m_layout.valid || m_layout.page != m_page)
        {
            if (!PrevPage()) return false;
            m_anchor = Anchor::End;
            return true;
        }

        if (m_targetY > 0.5f)
        {
            SetScrollTarget(m_targetX, std::max(0.0f, m_targetY - m_viewHeight * fraction), true);
            return true;
        }
        if (m_layout.maxScrollX > 0.5f)
        {
            float step = m_viewWidth * fraction;
            if (!m_options.manga && m_targetX > 0.5f)
            {
                SetScrollTarget(std::max(0.0f, m_targetX - step), m_layout.maxScrollY, true);
                return true;
            }
            if (m_options.manga && m_targetX < m_layout.maxScrollX - 0.5f)
            {
                SetScrollTarget(std::min(m_layout.maxScrollX, m_targetX + step), m_layout.maxScrollY, true);
                return true;
            }
        }
        if (!PrevPage()) return false;
        m_anchor = Anchor::End;
        return true;
    }

    bool Viewer::ScrollBy(float dx, float dy, bool smooth)
    {
        if (!m_layout.valid) return false;
        float x = std::clamp(m_targetX + dx, 0.0f, m_layout.maxScrollX);
        float y = std::clamp(m_targetY + dy, 0.0f, m_layout.maxScrollY);
        if (x == m_targetX && y == m_targetY) return false;
        SetScrollTarget(x, y, smooth);
        return true;
    }

    void Viewer::SetFit(FitMode fit)
    {
        m_options.fit = fit;
        m_zoom = 1.0f;
        m_anchor = Anchor::Start;
    }

    void Viewer::ToggleSpread()
    {
        m_options.spread = !m_options.spread;
        m_anchor = Anchor::Start;
        UpdateFocus(true);
    }

    void Viewer::ToggleManga()
    {
        m_options.manga = !m_options.manga;
        m_anchor = Anchor::Start;
    }

    void Viewer::Rotate(int quarterTurns)
    {
        m_rotation = (m_rotation + quarterTurns) & 3;
        m_anchor = Anchor::Start;
    }

    void Viewer::ZoomBy(float factor)
    {
        float zoom = std::clamp(m_zoom * factor, 0.05f, 32.0f);
        if (zoom == m_zoom) return;
        if (m_layout.valid && m_layout.page == m_page)
        {
            // Keep the point at the centre of the window fixed.
            float ratio = zoom / m_zoom;
            float cx = m_layout.displayWidth > m_viewWidth ? (m_scrollX + m_viewWidth / 2) / m_layout.displayWidth : 0.5f;
            float cy = m_layout.displayHeight > m_viewHeight ? (m_scrollY + m_viewHeight / 2) / m_layout.displayHeight : 0.5f;
            SetScrollTarget(cx * m_layout.displayWidth * ratio - m_viewWidth / 2,
                            cy * m_layout.displayHeight * ratio - m_viewHeight / 2, false); // zoom is instant
        }
        m_zoom = zoom;
        m_anchor = Anchor::None;
    }

    void Viewer::ResetZoom()
    {
        m_zoom = 1.0f;
        m_anchor = Anchor::Start;
    }

    bool Viewer::BuildFrame(Frame& frame)
    {
        if (!m_source) return false;

        auto first = m_loader.Get(m_page);
        if (!first) return false;

        std::vector<PageImagePtr> pages{ first };
        bool canPair = m_options.spread && m_page + 1 < PageCount() && !(m_options.coverAlone && m_page == 0);
        if (canPair && !first->IsWide())
        {
            auto second = m_loader.Get(m_page + 1);
            if (!second) return false;
            if (!second->IsWide()) pages.push_back(second);
        }
        size_t visible = pages.size();
        if (m_options.manga && pages.size() == 2) std::swap(pages[0], pages[1]);

        // Content space: pages side by side, scaled to a common height.
        float contentHeight = 0;
        for (auto const& page : pages)
        {
            float w, h;
            PageSize(*page, w, h);
            contentHeight = std::max(contentHeight, h);
        }
        frame.items.clear();
        float x = 0;
        for (auto const& page : pages)
        {
            float w, h;
            PageSize(*page, w, h);
            float width = w * (contentHeight / h);
            frame.items.push_back({ page, D2D1::RectF(x, 0, x + width, contentHeight) });
            x += width;
        }
        float contentWidth = x;

        bool sideways = (m_rotation & 1) != 0;
        float boxWidth = sideways ? contentHeight : contentWidth;
        float boxHeight = sideways ? contentWidth : contentHeight;

        float fit = 1.0f;
        switch (m_options.fit)
        {
        case FitMode::Page: fit = std::min(m_viewWidth / boxWidth, m_viewHeight / boxHeight); break;
        case FitMode::Width: fit = m_viewWidth / boxWidth; break;
        case FitMode::Height: fit = m_viewHeight / boxHeight; break;
        case FitMode::Actual: fit = 1.0f; break;
        }
        // Low-resolution scans stay pixel-sharp instead of being blown up, if so chosen.
        if (!m_options.enlargeSmall && m_options.fit != FitMode::Actual) fit = std::min(fit, 1.0f);
        float scale = fit * m_zoom;
        float displayWidth = boxWidth * scale;
        float displayHeight = boxHeight * scale;
        float maxX = std::max(0.0f, displayWidth - m_viewWidth);
        float maxY = std::max(0.0f, displayHeight - m_viewHeight);

        // A new page starts at its beginning (or end, when paging backwards) without animating.
        if (m_anchor == Anchor::Start) SetScrollTarget(m_options.manga ? maxX : 0, 0, false);
        else if (m_anchor == Anchor::End) SetScrollTarget(m_options.manga ? 0 : maxX, maxY, false);
        m_anchor = Anchor::None;
        m_targetX = std::clamp(m_targetX, 0.0f, maxX);
        m_targetY = std::clamp(m_targetY, 0.0f, maxY);
        m_scrollX = std::clamp(m_scrollX, 0.0f, maxX);
        m_scrollY = std::clamp(m_scrollY, 0.0f, maxY);

        float originX = std::round(displayWidth <= m_viewWidth ? (m_viewWidth - displayWidth) / 2 : -m_scrollX);
        float originY = std::round(displayHeight <= m_viewHeight ? (m_viewHeight - displayHeight) / 2 : -m_scrollY);

        // Same mapping split as Scale(scale) * postTransform, for the renderer's sharpening path.
        frame.scale = scale;
        frame.postTransform = D2D1::Matrix3x2F::Translation(-contentWidth * scale / 2, -contentHeight * scale / 2) *
                              D2D1::Matrix3x2F::Rotation(90.0f * m_rotation) *
                              D2D1::Matrix3x2F::Translation(originX + displayWidth / 2, originY + displayHeight / 2);
        frame.transform = D2D1::Matrix3x2F::Translation(-contentWidth / 2, -contentHeight / 2) *
                          D2D1::Matrix3x2F::Rotation(90.0f * m_rotation) *
                          D2D1::Matrix3x2F::Scale(scale, scale) *
                          D2D1::Matrix3x2F::Translation(originX + displayWidth / 2, originY + displayHeight / 2);

        m_layout = { true, m_page, visible, displayWidth, displayHeight, maxX, maxY };
        return true;
    }
}
