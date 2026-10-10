#include "pch.h"
#include "Renderer.h"

namespace cdx
{
    namespace
    {
        bool IsDeviceLost(HRESULT hr) noexcept
        {
            return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == D2DERR_RECREATE_TARGET;
        }
    }

    Renderer::Renderer(std::shared_ptr<GraphicsDevice> device) : m_device(std::move(device))
    {
        m_context = m_device->CreateContext();
    }

    void Renderer::Attach(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel)
    {
        m_panel = panel;
        CreateSwapChain();
    }

    void Renderer::SetDevice(std::shared_ptr<GraphicsDevice> device)
    {
        m_device = std::move(device);
        m_target = nullptr;
        m_placeholderBrush = nullptr;
        m_sharpened.clear(); // bitmaps and effects belong to the old device
        m_scaleEffect = nullptr;
        m_sharpenEffect = nullptr;
        m_swapChain = nullptr;
        m_context = m_device->CreateContext();
        if (m_panel) CreateSwapChain();
    }

    void Renderer::CreateSwapChain()
    {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        winrt::com_ptr<IDXGIAdapter> adapter;
        winrt::check_hresult(m_device->Dxgi()->GetAdapter(adapter.put()));
        winrt::com_ptr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()));
        winrt::check_hresult(factory->CreateSwapChainForComposition(m_device->D3D(), &desc, nullptr, m_swapChain.put()));

        auto native = m_panel.as<ISwapChainPanelNative>();
        winrt::check_hresult(native->SetSwapChain(m_swapChain.get()));

        ApplyScaleTransform();
        CreateTarget();
    }

    void Renderer::CreateTarget()
    {
        winrt::com_ptr<IDXGISurface> surface;
        winrt::check_hresult(m_swapChain->GetBuffer(0, __uuidof(IDXGISurface), surface.put_void()));
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        winrt::check_hresult(m_context->CreateBitmapFromDxgiSurface(surface.get(), &props, m_target.put()));
        m_context->SetTarget(m_target.get());
        m_context->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
        if (!m_placeholderBrush)
        {
            winrt::check_hresult(m_context->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.35f, 0.35f), m_placeholderBrush.put()));
        }
    }

    void Renderer::ApplyScaleTransform()
    {
        // The swap chain is sized in physical pixels; tell the compositor to map it
        // back to the panel's DIPs so that it is shown 1:1 on screen.
        if (auto chain2 = m_swapChain.try_as<IDXGISwapChain2>())
        {
            DXGI_MATRIX_3X2_F inverseScale{};
            inverseScale._11 = 1.0f / m_scaleX;
            inverseScale._22 = 1.0f / m_scaleY;
            chain2->SetMatrixTransform(&inverseScale);
        }
    }

    void Renderer::Resize(float widthDips, float heightDips, float scaleX, float scaleY)
    {
        m_scaleX = scaleX > 0 ? scaleX : 1.0f;
        m_scaleY = scaleY > 0 ? scaleY : 1.0f;
        UINT width = std::max(1u, static_cast<UINT>(std::lround(widthDips * m_scaleX)));
        UINT height = std::max(1u, static_cast<UINT>(std::lround(heightDips * m_scaleY)));
        if (!m_swapChain) return;

        if (width != m_width || height != m_height)
        {
            m_width = width;
            m_height = height;
            m_context->SetTarget(nullptr);
            m_target = nullptr;
            HRESULT hr = m_swapChain->ResizeBuffers(2, m_width, m_height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
            if (IsDeviceLost(hr)) return; // the next Render() reports the loss
            winrt::check_hresult(hr);
            CreateTarget();
        }
        ApplyScaleTransform();
    }

    void Renderer::SetSharpening(Sharpening sharpening)
    {
        if (sharpening == m_sharpening) return;
        m_sharpening = sharpening;
        m_sharpened.clear();
    }

    // Returns the page scaled to (scaleX, scaleY) and sharpened, or null when it cannot
    // be cached (the result would be too large), in which case the page is drawn plainly.
    ID2D1Bitmap1* Renderer::SharpenedPage(PageImagePtr const& page, float scaleX, float scaleY)
    {
        for (auto& entry : m_sharpened)
        {
            if (entry.page == page && std::abs(entry.scaleX - scaleX) < 1e-5f && std::abs(entry.scaleY - scaleY) < 1e-5f)
            {
                entry.lastUsed = m_frameNumber;
                return entry.bitmap.get();
            }
        }

        UINT width = std::max(1u, static_cast<UINT>(std::lround(page->width * scaleX)));
        UINT height = std::max(1u, static_cast<UINT>(std::lround(page->height * scaleY)));
        constexpr uint64_t kMaxPixels = 48ull << 20; // ~192 MB of GPU memory per page at most
        UINT maxSize = m_context->GetMaximumBitmapSize();
        if (width > maxSize || height > maxSize || uint64_t{ width } * height > kMaxPixels) return nullptr;

        if (!m_scaleEffect)
        {
            winrt::check_hresult(m_context->CreateEffect(CLSID_D2D1Scale, m_scaleEffect.put()));
            winrt::check_hresult(m_context->CreateEffect(CLSID_D2D1Sharpen, m_sharpenEffect.put()));
            m_scaleEffect->SetValue(D2D1_SCALE_PROP_INTERPOLATION_MODE, D2D1_SCALE_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            m_scaleEffect->SetValue(D2D1_SCALE_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
            m_sharpenEffect->SetInputEffect(0, m_scaleEffect.get());
        }

        // Strengths tuned on downscaled line art: enough to restore ink edges without
        // halos; the threshold keeps paper texture and JPEG noise from being amplified.
        static constexpr float kSharpness[] = { 0.0f, 0.8f, 1.6f, 3.0f };
        static constexpr float kThreshold[] = { 0.0f, 0.03f, 0.02f, 0.015f };
        int level = static_cast<int>(m_sharpening);
        m_scaleEffect->SetInput(0, page->bitmap.get());
        m_scaleEffect->SetValue(D2D1_SCALE_PROP_SCALE, D2D1::Vector2F(width / static_cast<float>(page->width),
                                                                       height / static_cast<float>(page->height)));
        m_sharpenEffect->SetValue(D2D1_SHARPEN_PROP_SHARPNESS, kSharpness[level]);
        m_sharpenEffect->SetValue(D2D1_SHARPEN_PROP_THRESHOLD, kThreshold[level]);

        winrt::com_ptr<ID2D1Bitmap1> bitmap;
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(m_context->CreateBitmap({ width, height }, nullptr, 0, &props, bitmap.put()))) return nullptr;

        m_context->SetTarget(bitmap.get());
        m_context->BeginDraw();
        m_context->SetTransform(D2D1::Matrix3x2F::Identity());
        m_context->Clear(D2D1::ColorF(0, 0, 0, 0));
        m_context->DrawImage(m_sharpenEffect.get());
        HRESULT hr = m_context->EndDraw();
        m_context->SetTarget(m_target.get());
        m_scaleEffect->SetInput(0, nullptr); // don't keep the page alive through the effect
        if (FAILED(hr)) return nullptr;

        // Keep the few most recently used results (the visible pages plus a little slack).
        constexpr size_t kMaxEntries = 4;
        if (m_sharpened.size() >= kMaxEntries)
        {
            auto oldest = std::min_element(m_sharpened.begin(), m_sharpened.end(),
                                           [](auto const& a, auto const& b) { return a.lastUsed < b.lastUsed; });
            m_sharpened.erase(oldest);
        }
        m_sharpened.push_back({ page, scaleX, scaleY, bitmap, m_frameNumber });
        return bitmap.get();
    }

    bool Renderer::Render(Frame const& frame, bool waitForVBlank)
    {
        if (!m_swapChain || !m_target) return !m_device->IsLost();
        ++m_frameNumber;

        // Sharpened pages are prepared before the frame's own drawing starts.
        std::vector<ID2D1Bitmap1*> sharpened(frame.items.size(), nullptr);
        if (m_sharpening != Sharpening::Off)
        {
            for (size_t i = 0; i < frame.items.size(); ++i)
            {
                auto const& item = frame.items[i];
                if (!item.image || item.image->Failed()) continue;
                float scaleX = (item.dest.right - item.dest.left) * frame.scale / item.image->width;
                float scaleY = (item.dest.bottom - item.dest.top) * frame.scale / item.image->height;
                sharpened[i] = SharpenedPage(item.image, scaleX, scaleY);
            }
        }

        m_context->BeginDraw();
        m_context->SetTransform(D2D1::Matrix3x2F::Identity());
        m_context->Clear(frame.background);

        for (size_t i = 0; i < frame.items.size(); ++i)
        {
            auto const& item = frame.items[i];
            if (sharpened[i])
            {
                // Already at screen size: place it with the rotation/translation only.
                auto size = sharpened[i]->GetPixelSize();
                D2D1_RECT_F dest = D2D1::RectF(item.dest.left * frame.scale, item.dest.top * frame.scale,
                                               item.dest.left * frame.scale + size.width, item.dest.top * frame.scale + size.height);
                m_context->SetTransform(frame.postTransform);
                m_context->DrawBitmap(sharpened[i], &dest, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
            }
            else if (item.image && !item.image->Failed())
            {
                m_context->SetTransform(frame.transform);
                m_context->DrawBitmap(item.image->bitmap.get(), &item.dest, 1.0f,
                                      D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
            }
            else
            {
                // Unreadable page: draw a crossed-out box.
                m_context->SetTransform(frame.transform);
                float stroke = 4.0f;
                m_context->DrawRectangle(item.dest, m_placeholderBrush.get(), stroke);
                m_context->DrawLine({ item.dest.left, item.dest.top }, { item.dest.right, item.dest.bottom }, m_placeholderBrush.get(), stroke);
                m_context->DrawLine({ item.dest.right, item.dest.top }, { item.dest.left, item.dest.bottom }, m_placeholderBrush.get(), stroke);
            }
        }

        HRESULT hr = m_context->EndDraw();
        if (IsDeviceLost(hr)) return false;
        if (FAILED(hr) && hr != D2DERR_WRONG_RESOURCE_DOMAIN) winrt::check_hresult(hr);

        hr = m_swapChain->Present(waitForVBlank ? 1 : 0, 0);
        if (IsDeviceLost(hr)) return false;
        return true;
    }
}
