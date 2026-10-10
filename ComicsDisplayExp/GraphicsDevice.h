#pragma once

namespace cdx
{
    // The D3D11 / Direct2D / WIC objects shared by the renderer (UI thread) and the
    // decode workers. The D2D factory is multi-threaded, so workers can create GPU
    // bitmaps on their own device contexts while the UI thread draws.
    // On device loss the whole object is replaced, never repaired.
    class GraphicsDevice
    {
    public:
        GraphicsDevice();

        ID3D11Device* D3D() const noexcept { return m_d3d.get(); }
        IDXGIDevice* Dxgi() const noexcept { return m_dxgi.get(); }
        ID2D1Factory1* D2DFactory() const noexcept { return m_d2dFactory.get(); }
        ID2D1Device* D2D() const noexcept { return m_d2d.get(); }
        IWICImagingFactory* Wic() const noexcept { return m_wic.get(); }

        winrt::com_ptr<ID2D1DeviceContext> CreateContext() const;
        bool IsLost() const noexcept { return m_d3d->GetDeviceRemovedReason() != S_OK; }

    private:
        winrt::com_ptr<ID3D11Device> m_d3d;
        winrt::com_ptr<IDXGIDevice> m_dxgi;
        winrt::com_ptr<ID2D1Factory1> m_d2dFactory;
        winrt::com_ptr<ID2D1Device> m_d2d;
        winrt::com_ptr<IWICImagingFactory> m_wic;
    };
}
