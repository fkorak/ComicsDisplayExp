#pragma once

#include "ComicSource.h"
#include "GraphicsDevice.h"

namespace cdx
{
    // A decoded page, resident on the GPU.
    struct PageImage
    {
        winrt::com_ptr<ID2D1Bitmap1> bitmap; // null when the page could not be decoded
        uint32_t width = 0;
        uint32_t height = 0;
        size_t bytes = 0;

        bool Failed() const noexcept { return !bitmap; }
        bool IsWide() const noexcept { return width > height; }
    };
    using PageImagePtr = std::shared_ptr<const PageImage>;

    // Decodes pages on a pool of worker threads (WIC -> D2D bitmap) and keeps an
    // LRU-ish cache of GPU bitmaps. Pages around the current position are
    // prefetched in reading direction so that turning a page is just a redraw.
    class PageLoader
    {
    public:
        // Invoked on a worker thread whenever a page finished loading.
        using ReadyCallback = std::function<void(uint64_t generation, size_t index)>;

        PageLoader(std::shared_ptr<GraphicsDevice> device, ReadyCallback onReady);
        ~PageLoader();

        PageLoader(PageLoader const&) = delete;
        PageLoader& operator=(PageLoader const&) = delete;

        // Switches to a new book; returns the new generation number.
        uint64_t SetSource(std::shared_ptr<ComicSource> source);
        // Called after device loss; drops all cached bitmaps.
        void SetDevice(std::shared_ptr<GraphicsDevice> device);
        // Tells the loader what is on screen so it can prioritise and prefetch.
        void SetFocus(size_t firstVisible, size_t visibleCount, bool forward);

        PageImagePtr Get(size_t index) const;
        uint64_t Generation() const;

    private:
        void WorkerLoop();
        std::optional<size_t> PickJobLocked() const;
        void EvictLocked();
        static PageImagePtr Decode(GraphicsDevice& device, ID2D1DeviceContext* context, PageBytes const& bytes);

        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::vector<std::thread> m_workers;
        bool m_stop = false;

        ReadyCallback m_onReady;
        std::shared_ptr<GraphicsDevice> m_device;
        std::shared_ptr<ComicSource> m_source;
        uint64_t m_generation = 0;

        std::unordered_map<size_t, PageImagePtr> m_cache;
        size_t m_cacheBytes = 0;
        std::vector<size_t> m_wanted; // in priority order
        std::vector<size_t> m_inFlight;
        size_t m_focus = 0;
    };
}
