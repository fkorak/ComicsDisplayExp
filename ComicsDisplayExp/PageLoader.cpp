#include "pch.h"
#include "PageLoader.h"

namespace cdx
{
    namespace
    {
        constexpr size_t kPrefetchAhead = 6;
        constexpr size_t kPrefetchBehind = 2;
        constexpr size_t kCacheBudgetBytes = size_t{ 1024 } << 20; // GPU memory for decoded pages

        bool Contains(std::vector<size_t> const& v, size_t value) noexcept
        {
            return std::find(v.begin(), v.end(), value) != v.end();
        }

        size_t Distance(size_t a, size_t b) noexcept { return a > b ? a - b : b - a; }
    }

    PageLoader::PageLoader(std::shared_ptr<GraphicsDevice> device, ReadyCallback onReady)
        : m_onReady(std::move(onReady)), m_device(std::move(device))
    {
        unsigned hw = std::max(2u, std::thread::hardware_concurrency());
        unsigned count = std::clamp(hw / 2, 2u, 6u);
        for (unsigned i = 0; i < count; ++i) m_workers.emplace_back([this] { WorkerLoop(); });
    }

    PageLoader::~PageLoader()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
            if (m_source) m_source->Cancel();
        }
        m_cv.notify_all();
        for (auto& t : m_workers) t.join();
    }

    uint64_t PageLoader::SetSource(std::shared_ptr<ComicSource> source)
    {
        std::lock_guard lock(m_mutex);
        if (m_source) m_source->Cancel();
        m_source = std::move(source);
        m_cache.clear();
        m_cacheBytes = 0;
        m_wanted.clear();
        m_focus = 0;
        return ++m_generation;
    }

    void PageLoader::SetDevice(std::shared_ptr<GraphicsDevice> device)
    {
        {
            std::lock_guard lock(m_mutex);
            m_device = std::move(device);
            m_cache.clear();
            m_cacheBytes = 0;
        }
        m_cv.notify_all();
    }

    void PageLoader::SetFocus(size_t firstVisible, size_t visibleCount, bool forward)
    {
        {
            std::lock_guard lock(m_mutex);
            if (!m_source) return;
            size_t count = m_source->PageCount();
            if (count == 0) return;
            firstVisible = std::min(firstVisible, count - 1);

            m_focus = firstVisible;
            m_wanted.clear();
            size_t lastVisible = std::min(count - 1, firstVisible + std::max<size_t>(visibleCount, 1) - 1);
            for (size_t i = firstVisible; i <= lastVisible; ++i) m_wanted.push_back(i);

            // Interleave both directions, favouring the reading direction.
            size_t ahead = forward ? kPrefetchAhead : kPrefetchBehind;
            size_t behind = forward ? kPrefetchBehind : kPrefetchAhead;
            for (size_t k = 1; k <= std::max(ahead, behind); ++k)
            {
                size_t next = lastVisible + k;
                bool hasNext = k <= ahead && next < count;
                bool hasPrev = k <= behind && k <= firstVisible;
                if (forward)
                {
                    if (hasNext) m_wanted.push_back(next);
                    if (hasPrev) m_wanted.push_back(firstVisible - k);
                }
                else
                {
                    if (hasPrev) m_wanted.push_back(firstVisible - k);
                    if (hasNext) m_wanted.push_back(next);
                }
            }
        }
        m_cv.notify_all();
    }

    PageImagePtr PageLoader::Get(size_t index) const
    {
        std::lock_guard lock(m_mutex);
        auto it = m_cache.find(index);
        return it == m_cache.end() ? nullptr : it->second;
    }

    uint64_t PageLoader::Generation() const
    {
        std::lock_guard lock(m_mutex);
        return m_generation;
    }

    std::optional<size_t> PageLoader::PickJobLocked() const
    {
        for (size_t index : m_wanted)
        {
            if (!m_cache.contains(index) && !Contains(m_inFlight, index)) return index;
        }
        return std::nullopt;
    }

    void PageLoader::EvictLocked()
    {
        while (m_cacheBytes > kCacheBudgetBytes)
        {
            auto victim = m_cache.end();
            for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            {
                if (Contains(m_wanted, it->first)) continue;
                if (victim == m_cache.end() || Distance(it->first, m_focus) > Distance(victim->first, m_focus)) victim = it;
            }
            if (victim == m_cache.end()) return; // everything left is wanted
            m_cacheBytes -= victim->second->bytes;
            m_cache.erase(victim);
        }
    }

    void PageLoader::WorkerLoop()
    {
        SetThreadDescription(GetCurrentThread(), L"cdx page decoder");
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        std::shared_ptr<GraphicsDevice> contextDevice;
        winrt::com_ptr<ID2D1DeviceContext> context;

        std::unique_lock lock(m_mutex);
        for (;;)
        {
            std::optional<size_t> job;
            m_cv.wait(lock, [&] { return m_stop || (job = PickJobLocked()).has_value(); });
            if (m_stop) break;

            size_t index = *job;
            uint64_t generation = m_generation;
            auto source = m_source;
            auto device = m_device;
            m_inFlight.push_back(index);
            lock.unlock();

            PageImagePtr image;
            try
            {
                if (contextDevice != device)
                {
                    context = device->CreateContext();
                    contextDevice = device;
                }
                image = Decode(*device, context.get(), source->ReadPage(index));
            }
            catch (...)
            {
                image = std::make_shared<PageImage>();
            }

            lock.lock();
            m_inFlight.erase(std::find(m_inFlight.begin(), m_inFlight.end(), index));
            bool current = generation == m_generation && device == m_device;
            if (current)
            {
                m_cache[index] = image;
                m_cacheBytes += image->bytes;
                EvictLocked();
                lock.unlock();
                m_onReady(generation, index);
                lock.lock();
            }
        }
        context = nullptr;
        contextDevice = nullptr;
        lock.unlock();
        winrt::uninit_apartment();
    }

    PageImagePtr PageLoader::Decode(GraphicsDevice& device, ID2D1DeviceContext* context, PageBytes const& bytes)
    {
        auto image = std::make_shared<PageImage>();
        if (!bytes || bytes->empty()) return image;

        IWICImagingFactory* wic = device.Wic();
        winrt::com_ptr<IWICStream> stream;
        winrt::check_hresult(wic->CreateStream(stream.put()));
        winrt::check_hresult(stream->InitializeFromMemory(const_cast<BYTE*>(bytes->data()), static_cast<DWORD>(bytes->size())));

        winrt::com_ptr<IWICBitmapDecoder> decoder;
        winrt::check_hresult(wic->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put()));
        winrt::com_ptr<IWICBitmapFrameDecode> frame;
        winrt::check_hresult(decoder->GetFrame(0, frame.put()));

        UINT width = 0, height = 0;
        winrt::check_hresult(frame->GetSize(&width, &height));
        if (width == 0 || height == 0) return image;

        winrt::com_ptr<IWICBitmapSource> source = frame;

        // Pages larger than the GPU's texture limit are scaled down while decoding.
        UINT maxSize = context->GetMaximumBitmapSize();
        if (width > maxSize || height > maxSize)
        {
            double scale = std::min(static_cast<double>(maxSize) / width, static_cast<double>(maxSize) / height);
            UINT w = std::max(1u, static_cast<UINT>(width * scale));
            UINT h = std::max(1u, static_cast<UINT>(height * scale));
            winrt::com_ptr<IWICBitmapScaler> scaler;
            winrt::check_hresult(wic->CreateBitmapScaler(scaler.put()));
            winrt::check_hresult(scaler->Initialize(source.get(), w, h, WICBitmapInterpolationModeFant));
            source = scaler;
            width = w;
            height = h;
        }

        winrt::com_ptr<IWICFormatConverter> converter;
        winrt::check_hresult(wic->CreateFormatConverter(converter.put()));
        winrt::check_hresult(converter->Initialize(source.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                                   nullptr, 0.0, WICBitmapPaletteTypeMedianCut));

        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        winrt::check_hresult(context->CreateBitmapFromWicBitmap(converter.get(), &props, image->bitmap.put()));

        image->width = width;
        image->height = height;
        image->bytes = size_t{ width } * height * 4;
        return image;
    }
}
