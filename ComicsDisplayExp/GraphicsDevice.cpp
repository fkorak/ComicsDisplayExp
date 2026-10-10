#include "pch.h"
#include "GraphicsDevice.h"

namespace cdx
{
    GraphicsDevice::GraphicsDevice()
    {
        static constexpr D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,
        };
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                       ARRAYSIZE(levels), D3D11_SDK_VERSION, m_d3d.put(), nullptr, nullptr);
        if (FAILED(hr))
        {
            // No usable GPU (e.g. remote session without acceleration): fall back to WARP.
            winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, m_d3d.put(), nullptr, nullptr));
        }

        if (auto mt = m_d3d.try_as<ID3D11Multithread>()) mt->SetMultithreadProtected(TRUE);
        m_dxgi = m_d3d.as<IDXGIDevice>();

        D2D1_FACTORY_OPTIONS options{};
#ifdef _DEBUG
        options.debugLevel = D2D1_DEBUG_LEVEL_WARNING;
#endif
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &options,
                                               m_d2dFactory.put_void()));
        winrt::check_hresult(m_d2dFactory->CreateDevice(m_dxgi.get(), m_d2d.put()));

        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                              __uuidof(IWICImagingFactory), m_wic.put_void()));
    }

    winrt::com_ptr<ID2D1DeviceContext> GraphicsDevice::CreateContext() const
    {
        winrt::com_ptr<ID2D1DeviceContext> context;
        winrt::check_hresult(m_d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context.put()));
        return context;
    }
}
