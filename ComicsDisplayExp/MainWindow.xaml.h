#pragma once

#include "MainWindow.g.h"

#include "GraphicsDevice.h"
#include "PageLoader.h"
#include "Renderer.h"
#include "ReadingHistory.h"
#include "Settings.h"
#include "Viewer.h"

namespace winrt::ComicsDisplayExp::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow() = default;
        void InitializeComponent();

        void OpenPath(std::wstring path);

        // XAML event handlers
        void OnPreviewKeyDown(IInspectable const& sender, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        void OnPanelSizeChanged(IInspectable const& sender, Microsoft::UI::Xaml::SizeChangedEventArgs const& e);
        void OnCompositionScaleChanged(Microsoft::UI::Xaml::Controls::SwapChainPanel const& sender, IInspectable const& args);
        void OnPointerPressed(IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerMoved(IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerReleased(IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerCaptureLost(IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnPointerWheelChanged(IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnDragOver(IInspectable const& sender, Microsoft::UI::Xaml::DragEventArgs const& e);
        winrt::fire_and_forget OnDrop(IInspectable const& sender, Microsoft::UI::Xaml::DragEventArgs e);

        // Title bar and menus
        void OnCommand(IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnMenuOpening(IInspectable const& sender, IInspectable const& args);
        void OnFlyoutClosed(IInspectable const& sender, IInspectable const& args);
        void OnPageFlyoutOpening(IInspectable const& sender, IInspectable const& args);
        void OnPageSliderChanged(IInspectable const& sender, Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e);
        void OnTitleBarPointerExited(IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnHelpBackdropTapped(IInspectable const& sender, Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& e);
        void OnHelpCardTapped(IInspectable const& sender, Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& e);
        void OnTitleBarSizeChanged(IInspectable const& sender, Microsoft::UI::Xaml::SizeChangedEventArgs const& e);

    private:
        void ExecuteCommand(std::wstring_view command);
        void SyncMenuState();
        void RebuildRecentMenu();
        void ApplyFullscreenLayout();
        void SetTitleBarRevealed(bool revealed);
        void ShowMainMenu();
        void ApplyTheme();
        void ApplyWindowIcon();
        void UpdateThemeColors();
        void UpdateDefaultViewerLink();
        void UpdateToolbarLayout();
        void QueueToolbarLayout();
        void UpdatePassthroughRegions();
        void OnRenderingFrame(IInspectable const& sender, IInspectable const& args);
        void StopScrollAnimation();

        winrt::fire_and_forget OpenPathAsync(std::wstring path);
        void OnPageLoaded();
        void OnClosed();
        void RestoreWindowPlacement();

        void Render();
        void UpdateViewport();
        void UpdateChrome();
        void HandleDeviceLost();

        void NextPage();
        void PrevPage();
        void OpenSibling(int direction);
        void ShowOpenDialog(bool pickFolder);
        void ToggleFullscreen();
        bool IsFullscreen();
        void ShowMessage(std::wstring const& text);
        void RecordBookState();

        std::shared_ptr<cdx::GraphicsDevice> m_device;
        std::unique_ptr<cdx::PageLoader> m_loader;
        std::unique_ptr<cdx::Viewer> m_viewer;
        std::unique_ptr<cdx::Renderer> m_renderer;
        cdx::Settings m_settings;
        cdx::ReadingHistory m_history;
        cdx::BookState m_lastRecordedState{};
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_historyTimer{ nullptr }; // debounced history save

        Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_messageTimer{ nullptr };
        std::atomic<bool> m_redrawQueued{ false };
        bool m_waitingForPages = false;
        bool m_closing = false;

        std::wstring m_bookPath;
        uint64_t m_openRequest = 0;

        float m_panelWidth = 1, m_panelHeight = 1;
        bool m_dragging = false;
        bool m_dragMoved = false;
        Windows::Foundation::Point m_dragStart{};
        Windows::Foundation::Point m_dragLast{};
        int m_wheelAccumulator = 0;
        std::chrono::steady_clock::time_point m_wheelBlockedUntil{}; // wheel page turns blocked until then

        std::wstring m_lastInfo;
        std::wstring m_lastTitle;
        bool m_titleBarRevealed = false; // title bar shown over the page in fullscreen
        bool m_syncingSlider = false;
        D2D1_COLOR_F m_pageBackground{ 0.06f, 0.06f, 0.06f, 1.0f }; // around the pages, per theme
        bool m_toolbarHasBook = false;
        std::array<double, 9> m_toolbarItemWidths{}; // natural widths, cached while visible
        winrt::event_token m_xamlRootChanged{};
        winrt::event_token m_renderingToken{};       // CompositionTarget::Rendering while scrolling animates
        Windows::Foundation::TimeSpan m_lastFrameTime{};
    };
}

namespace winrt::ComicsDisplayExp::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
