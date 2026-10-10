#pragma once

#include "GraphicsDevice.h"
#include "PageLoader.h"

namespace cdx
{
    struct FrameItem
    {
        PageImagePtr image;  // may be a failed page (drawn as a placeholder)
        D2D1_RECT_F dest{};  // in content space
    };

    struct Frame
    {
        D2D1_COLOR_F background{ 0.06f, 0.06f, 0.06f, 1.0f };
        D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity(); // content space -> pixels
        // The same mapping split in two: transform == Scale(scale) * postTransform, where
        // postTransform only rotates by quarter turns and translates (no resampling).
        float scale = 1.0f;
        D2D1_MATRIX_3X2_F postTransform = D2D1::Matrix3x2F::Identity();
        std::vector<FrameItem> items;
    };

    // Sharpening applied after scaling, at screen resolution.
    enum class Sharpening : int { Off = 0, Light = 1, Medium = 2, Strong = 3 };

    // Owns the composition swap chain behind the SwapChainPanel and draws frames
    // with Direct2D. All methods must be called on the UI thread.
    class Renderer
    {
    public:
        explicit Renderer(std::shared_ptr<GraphicsDevice> device);

        void Attach(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel);
        void SetDevice(std::shared_ptr<GraphicsDevice> device);
        void Resize(float widthDips, float heightDips, float scaleX, float scaleY);

        // Returns false if the device was lost; the caller must recreate it.
        // waitForVBlank=false is for frames already paced by the compositor (animations).
        bool Render(Frame const& frame, bool waitForVBlank = true);

        float PixelWidth() const noexcept { return static_cast<float>(m_width); }
        float PixelHeight() const noexcept { return static_cast<float>(m_height); }
        float ScaleX() const noexcept { return m_scaleX; }
        float ScaleY() const noexcept { return m_scaleY; }

        void SetSharpening(Sharpening sharpening);
        Sharpening GetSharpening() const noexcept { return m_sharpening; }

    private:
        void CreateSwapChain();
        void CreateTarget();
        void ApplyScaleTransform();
        ID2D1Bitmap1* SharpenedPage(PageImagePtr const& page, float scaleX, float scaleY);

        std::shared_ptr<GraphicsDevice> m_device;
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel m_panel{ nullptr };
        winrt::com_ptr<IDXGISwapChain1> m_swapChain;
        winrt::com_ptr<ID2D1DeviceContext> m_context;
        winrt::com_ptr<ID2D1Bitmap1> m_target;
        winrt::com_ptr<ID2D1SolidColorBrush> m_placeholderBrush;

        // Sharpening: page -> Scale (high-quality cubic) -> Sharpen, rendered once per
        // page and size into a screen-sized bitmap, so scrolling only blits it.
        struct SharpenedEntry
        {
            PageImagePtr page;
            float scaleX = 0, scaleY = 0;
            winrt::com_ptr<ID2D1Bitmap1> bitmap;
            uint64_t lastUsed = 0;
        };
        Sharpening m_sharpening = Sharpening::Off;
        winrt::com_ptr<ID2D1Effect> m_scaleEffect;
        winrt::com_ptr<ID2D1Effect> m_sharpenEffect;
        std::vector<SharpenedEntry> m_sharpened;
        uint64_t m_frameNumber = 0;
        UINT m_width = 1;
        UINT m_height = 1;
        float m_scaleX = 1.0f;
        float m_scaleY = 1.0f;
    };
}
