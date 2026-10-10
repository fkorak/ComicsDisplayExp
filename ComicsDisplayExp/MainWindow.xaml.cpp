#include "pch.h"
#include "MainWindow.xaml.h"
#include "FileAssociations.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <wil/cppwinrt_helpers.h>
#include <cmath>
#include <filesystem>
#include <format>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Windowing;
using namespace winrt::Microsoft::UI::Dispatching;
using VirtualKey = winrt::Windows::System::VirtualKey;

namespace winrt::ComicsDisplayExp::implementation
{
    namespace
    {
        constexpr wchar_t kAppName[] = L"ComicsDisplayExp";

        bool IsKeyDown(VirtualKey key)
        {
            auto state = Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(key);
            return (state & Windows::UI::Core::CoreVirtualKeyStates::Down) == Windows::UI::Core::CoreVirtualKeyStates::Down;
        }

        const wchar_t* FitName(cdx::FitMode fit)
        {
            switch (fit)
            {
            case cdx::FitMode::Width: return L"Fit width";
            case cdx::FitMode::Height: return L"Fit height";
            case cdx::FitMode::Actual: return L"Actual size";
            default: return L"Fit page";
            }
        }
    }

    void MainWindow::InitializeComponent()
    {
        MainWindowT::InitializeComponent();

        m_dispatcher = DispatcherQueue();
        m_settings.Load();

        m_device = std::make_shared<cdx::GraphicsDevice>();
        m_loader = std::make_unique<cdx::PageLoader>(m_device, [this](uint64_t, size_t) { OnPageLoaded(); });
        m_viewer = std::make_unique<cdx::Viewer>(*m_loader);
        m_viewer->Options() = m_settings.view;
        m_viewer->SetSmoothScrolling(m_settings.smoothScrolling);
        m_renderer = std::make_unique<cdx::Renderer>(m_device);
        m_renderer->Attach(Panel());
        m_renderer->SetSharpening(static_cast<cdx::Sharpening>(m_settings.sharpening));

        // Per-comic reading state; loading also drops entries untouched for months.
        // Keep file associations pointing at this exe, wherever it lives now.
        cdx::FileAssociations::RefreshIfRegistered();
        UpdateDefaultViewerLink();

        m_history.Load();
        m_historyTimer = m_dispatcher.CreateTimer();
        m_historyTimer.Interval(std::chrono::seconds(3));
        m_historyTimer.IsRepeating(false);
        m_historyTimer.Tick([this](auto&&, auto&&) {
            if (m_closing) return;
            RecordBookState();
            m_history.SaveIfDirty();
        });

        m_messageTimer = m_dispatcher.CreateTimer();
        m_messageTimer.Interval(std::chrono::milliseconds(2500));
        m_messageTimer.Tick([this](auto&&, auto&&) {
            m_messageTimer.Stop();
            MessageBar().Visibility(Visibility::Collapsed);
        });

        // Our own XAML title bar (hamburger menu + toolbar) replaces the system one.
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        // Light / dark / follow Windows (☰ → Theme); System mode tracks Windows live.
        Root().ActualThemeChanged([this](auto&&, auto&&) { UpdateThemeColors(); });
        ApplyTheme();
        AppWindow().TitleBar().PreferredHeightOption(TitleBarHeightOption::Tall);
        RestoreWindowPlacement();

        Activated([this](auto&&, WindowActivatedEventArgs const& args) {
            if (args.WindowActivationState() != WindowActivationState::Deactivated)
            {
                InputHost().Focus(FocusState::Programmatic);
            }
        });
        Closed([this](auto&&, auto&&) { OnClosed(); });

        UpdateChrome();
    }

    // ------------------------------------------------------------------ window

    void MainWindow::RestoreWindowPlacement()
    {
        auto appWindow = AppWindow();
        if (m_settings.hasWindowRect)
        {
            Windows::Graphics::RectInt32 rect{ m_settings.windowX, m_settings.windowY, m_settings.windowWidth, m_settings.windowHeight };
            if (DisplayArea::GetFromRect(rect, DisplayAreaFallback::None))
            {
                appWindow.MoveAndResize(rect);
            }
        }
        else
        {
            appWindow.Resize({ 1100, 1300 });
        }
        if (m_settings.maximized)
        {
            if (auto presenter = appWindow.Presenter().try_as<OverlappedPresenter>()) presenter.Maximize();
        }
    }

    void MainWindow::OnClosed()
    {
        m_closing = true;
        m_historyTimer.Stop();
        RecordBookState();
        m_history.SaveIfDirty();

        // Destroying a window with a custom title bar while it is in the fullscreen
        // presenter fail-fasts inside Microsoft.UI.Input.dll; leave fullscreen first.
        auto appWindow = AppWindow();
        if (IsFullscreen()) appWindow.SetPresenter(AppWindowPresenterKind::Overlapped);

        if (auto presenter = appWindow.Presenter().try_as<OverlappedPresenter>())
        {
            m_settings.maximized = presenter.State() == OverlappedPresenterState::Maximized;
            if (presenter.State() == OverlappedPresenterState::Restored)
            {
                auto position = appWindow.Position();
                auto size = appWindow.Size();
                m_settings.windowX = position.X;
                m_settings.windowY = position.Y;
                m_settings.windowWidth = size.Width;
                m_settings.windowHeight = size.Height;
                m_settings.hasWindowRect = true;
            }
        }
        StopScrollAnimation();
        m_settings.view = m_viewer->Options();
        m_settings.smoothScrolling = m_viewer->SmoothScrolling();
        m_settings.sharpening = static_cast<int>(m_renderer->GetSharpening());
        m_settings.Save();
        // The viewer and loader stay alive until the window is destroyed: XAML still
        // delivers a few events (resize, flyout closed) after Closed, and every
        // handler bails out early once m_closing is set.
    }

    bool MainWindow::IsFullscreen()
    {
        auto appWindow = AppWindow();
        return appWindow && appWindow.Presenter().Kind() == AppWindowPresenterKind::FullScreen;
    }

    void MainWindow::ToggleFullscreen()
    {
        AppWindow().SetPresenter(IsFullscreen() ? AppWindowPresenterKind::Overlapped : AppWindowPresenterKind::FullScreen);
        ApplyFullscreenLayout();
    }

    void MainWindow::ApplyFullscreenLayout()
    {
        // In fullscreen the page takes the whole screen and the title bar only
        // slides in (over the page) when the mouse touches the top edge.
        bool fullscreen = IsFullscreen();
        Controls::Grid::SetRow(ViewerArea(), fullscreen ? 0 : 1);
        Controls::Grid::SetRowSpan(ViewerArea(), fullscreen ? 2 : 1);
        FullscreenGlyph().Glyph(fullscreen ? L"" : L"");
        MenuFullscreen().Text(fullscreen ? L"Exit fullscreen" : L"Fullscreen");
        SetTitleBarRevealed(false);
        QueueToolbarLayout(); // caption buttons come and go
    }

    void MainWindow::OnTitleBarSizeChanged(IInspectable const&, SizeChangedEventArgs const&)
    {
        if (m_closing) return;
        if (!m_xamlRootChanged && AppTitleBar().XamlRoot())
        {
            // DPI changes (e.g. moving to another monitor) change the caption inset in DIPs.
            m_xamlRootChanged = AppTitleBar().XamlRoot().Changed([this](auto&&, auto&&) { QueueToolbarLayout(); });
        }
        UpdateToolbarLayout();
    }

    void MainWindow::QueueToolbarLayout()
    {
        // Runs after the pending layout pass, so positions are current.
        m_dispatcher.TryEnqueue(DispatcherQueuePriority::Low, [this] { UpdateToolbarLayout(); });
    }

    void MainWindow::UpdateToolbarLayout()
    {
        if (m_closing) return;
        auto bar = AppTitleBar();
        auto toolbar = BarToolbar();
        auto xamlRoot = bar.XamlRoot();
        if (!xamlRoot || bar.ActualWidth() <= 0) return;

        // Room between the toolbar's left edge and the caption buttons. The inset comes
        // straight from AppWindow (physical pixels -> DIPs), so the buttons can never slide
        // underneath minimize / maximize / close at any scale factor.
        constexpr double kMinDragWidth = 48; // keep a strip to drag the window by
        constexpr double kSpacing = 2;       // StackPanel spacing
        constexpr double kGroupGap = 12;     // between navigation and view toggles
        double captionInset = IsFullscreen() ? 0.0 : AppWindow().TitleBar().RightInset() / xamlRoot.RasterizationScale();
        CaptionColumn().Width(GridLength{ captionInset, GridUnitType::Pixel });
        double left = toolbar.TransformToVisual(bar).TransformPoint({ 0, 0 }).X;
        double available = std::max(0.0, bar.ActualWidth() - left - captionInset - kMinDragWidth);

        // Collapse order when space runs out: least important first. Everything hidden
        // here is still reachable through the hamburger menu or the keyboard.
        FrameworkElement items[] = { FirstButton(), LastButton(), FitButton(), MangaToggle(), PrevButton(),
                                     NextButton(), SpreadToggle(), PageButton(), FullscreenButton() };
        constexpr bool isNav[] = { true, true, false, false, true, true, false, true, false };
        static_assert(std::size(items) == std::tuple_size_v<decltype(m_toolbarItemWidths)>);

        std::array<bool, std::size(items)> show{};
        for (size_t i = 0; i < std::size(items); ++i)
        {
            if (items[i].Visibility() == Visibility::Visible)
            {
                items[i].Measure({ std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() });
                m_toolbarItemWidths[i] = items[i].DesiredSize().Width;
            }
            show[i] = !isNav[i] || m_toolbarHasBook;
        }

        auto groupWidth = [&](bool nav) {
            double width = 0;
            int count = 0;
            for (size_t i = 0; i < std::size(items); ++i)
            {
                if (show[i] && isNav[i] == nav)
                {
                    width += m_toolbarItemWidths[i];
                    ++count;
                }
            }
            return count ? width + kSpacing * (count - 1) : 0.0;
        };
        auto needed = [&] {
            double nav = groupWidth(true), view = groupWidth(false);
            return nav + view + (nav > 0 && view > 0 ? kGroupGap : 0.0);
        };
        for (size_t i = 0; i < std::size(items) && needed() > available; ++i) show[i] = false;

        for (size_t i = 0; i < std::size(items); ++i)
        {
            items[i].Visibility(show[i] ? Visibility::Visible : Visibility::Collapsed);
        }
        double navWidth = groupWidth(true), viewWidth = groupWidth(false);
        NavGroup().Visibility(navWidth > 0 ? Visibility::Visible : Visibility::Collapsed);
        ViewGroup().Visibility(viewWidth > 0 ? Visibility::Visible : Visibility::Collapsed);

        // Centre the page navigation in the toolbar when there is room; the book name
        // gets the space to its left and trims with an ellipsis.
        double free = std::max(0.0, available - navWidth - viewWidth);
        double nameWidth = std::clamp(available / 2 - navWidth / 2, 0.0, free);
        auto columns = toolbar.ColumnDefinitions();
        columns.GetAt(0).Width(GridLength{ nameWidth, GridUnitType::Pixel });
        columns.GetAt(2).Width(GridLength{ free - nameWidth, GridUnitType::Pixel });
        BookTitle().Visibility(nameWidth >= 56 ? Visibility::Visible : Visibility::Collapsed); // no lone "T…"
        toolbar.Width(available);

        // Hit-test regions depend on the new layout, so update them after it has run.
        m_dispatcher.TryEnqueue(DispatcherQueuePriority::Low, [this] { UpdatePassthroughRegions(); });
    }

    void MainWindow::UpdatePassthroughRegions()
    {
        // The whole bar drags the window (SetTitleBar); the buttons are cut out of it.
        if (m_closing) return;
        auto xamlRoot = AppTitleBar().XamlRoot();
        if (!xamlRoot) return;
        double scale = xamlRoot.RasterizationScale();

        std::vector<Windows::Graphics::RectInt32> rects;
        if (AppTitleBar().Visibility() == Visibility::Visible)
        {
            for (FrameworkElement element : { HamburgerButton().as<FrameworkElement>(), NavGroup().as<FrameworkElement>(), ViewGroup().as<FrameworkElement>() })
            {
                if (element.Visibility() != Visibility::Visible || element.ActualWidth() <= 0) continue;
                auto bounds = element.TransformToVisual(nullptr).TransformBounds(
                    { 0, 0, static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight()) });
                rects.push_back({ static_cast<int32_t>(std::floor(bounds.X * scale)), static_cast<int32_t>(std::floor(bounds.Y * scale)),
                                  static_cast<int32_t>(std::ceil(bounds.Width * scale)), static_cast<int32_t>(std::ceil(bounds.Height * scale)) });
            }
        }
        auto source = Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(AppWindow().Id());
        source.SetRegionRects(Microsoft::UI::Input::NonClientRegionKind::Passthrough, rects);
    }

    void MainWindow::ApplyTheme()
    {
        // Element theme on the root drives every ThemeResource in the window; Default
        // follows the Windows app mode (App.xaml requests no theme of its own).
        int theme = m_settings.theme;
        Root().RequestedTheme(theme == 1 ? ElementTheme::Light : theme == 2 ? ElementTheme::Dark : ElementTheme::Default);
        AppWindow().TitleBar().PreferredTheme(theme == 1 ? TitleBarTheme::Light
                                              : theme == 2 ? TitleBarTheme::Dark
                                                           : TitleBarTheme::UseDefaultAppMode);
        UpdateThemeColors();
    }

    void MainWindow::UpdateThemeColors()
    {
        // The swap chain is outside XAML, so its backdrop colour is set by hand.
        bool dark = Root().ActualTheme() == ElementTheme::Dark;
        m_pageBackground = dark ? D2D1::ColorF(0x0F0F0F) : D2D1::ColorF(0xE8E8E8);
        if (m_titleBarRevealed) SetTitleBarRevealed(true);
        Render();
    }

    void MainWindow::SetTitleBarRevealed(bool revealed)
    {
        bool fullscreen = IsFullscreen();
        m_titleBarRevealed = fullscreen && revealed;
        AppTitleBar().Visibility(!fullscreen || m_titleBarRevealed ? Visibility::Visible : Visibility::Collapsed);
        if (m_titleBarRevealed)
        {
            bool dark = Root().ActualTheme() == ElementTheme::Dark;
            AppTitleBar().Background(Media::SolidColorBrush(dark ? Windows::UI::Color{ 0xF0, 0x1C, 0x1C, 0x1C } : Windows::UI::Color{ 0xF0, 0xF3, 0xF3, 0xF3 }));
        }
        else
        {
            AppTitleBar().ClearValue(Controls::Panel::BackgroundProperty());
        }
        QueueToolbarLayout(); // caption inset and drag regions differ in fullscreen
    }

    void MainWindow::ShowMessage(std::wstring const& text)
    {
        MessageText().Text(text);
        MessageBar().Visibility(Visibility::Visible);
        m_messageTimer.Stop();
        m_messageTimer.Start();
    }

    // --------------------------------------------------------------- rendering

    void MainWindow::OnPageLoaded()
    {
        // Called on decoder threads; coalesce into one redraw on the UI thread.
        if (m_redrawQueued.exchange(true)) return;
        m_dispatcher.TryEnqueue(DispatcherQueuePriority::High, [this] {
            m_redrawQueued = false;
            if (!m_closing && m_waitingForPages) Render();
        });
    }

    void MainWindow::Render()
    {
        if (m_closing) return;

        cdx::Frame frame;
        bool ready = true;
        if (m_viewer->HasBook())
        {
            ready = m_viewer->BuildFrame(frame);
        }
        m_waitingForPages = !ready;
        frame.background = m_pageBackground; // around the pages; follows the theme

        // While the next page is still decoding, the previous frame stays on screen.
        // Animation frames are paced by CompositionTarget::Rendering, so they don't wait for vblank.
        bool animating = static_cast<bool>(m_renderingToken);
        if (ready && !m_renderer->Render(frame, !animating))
        {
            HandleDeviceLost();
        }
        UpdateChrome();

        // A scroll input only set a target; ease towards it once per display frame.
        if (!animating && m_viewer->IsScrollAnimating())
        {
            m_lastFrameTime = {};
            m_renderingToken = Media::CompositionTarget::Rendering({ this, &MainWindow::OnRenderingFrame });
        }
    }

    void MainWindow::OnRenderingFrame(IInspectable const&, IInspectable const& args)
    {
        if (m_closing) return;
        auto now = args.as<Media::RenderingEventArgs>().RenderingTime();
        float seconds = m_lastFrameTime.count() ? std::chrono::duration<float>(now - m_lastFrameTime).count() : 1.0f / 60.0f;
        m_lastFrameTime = now;

        bool moving = m_viewer->StepScroll(std::clamp(seconds, 0.001f, 0.05f));
        if (!moving) StopScrollAnimation(); // the final frame below then waits for vblank as usual
        Render();
    }

    void MainWindow::StopScrollAnimation()
    {
        if (m_renderingToken)
        {
            Media::CompositionTarget::Rendering(m_renderingToken);
            m_renderingToken = {};
        }
    }

    void MainWindow::HandleDeviceLost()
    {
        m_device = std::make_shared<cdx::GraphicsDevice>();
        m_renderer->SetDevice(m_device);
        m_loader->SetDevice(m_device);
        UpdateViewport();
        m_dispatcher.TryEnqueue([this] { Render(); });
    }

    void MainWindow::UpdateViewport()
    {
        m_renderer->Resize(m_panelWidth, m_panelHeight, Panel().CompositionScaleX(), Panel().CompositionScaleY());
        m_viewer->SetViewport(m_renderer->PixelWidth(), m_renderer->PixelHeight());
    }

    void MainWindow::OnPanelSizeChanged(IInspectable const&, SizeChangedEventArgs const& e)
    {
        if (m_closing) return;
        m_panelWidth = e.NewSize().Width;
        m_panelHeight = e.NewSize().Height;
        UpdateViewport();
        Render();
    }

    void MainWindow::OnCompositionScaleChanged(Controls::SwapChainPanel const&, IInspectable const&)
    {
        if (m_closing) return;
        UpdateViewport();
        Render();
    }

    void MainWindow::UpdateChrome()
    {
        bool hasBook = m_viewer && m_viewer->HasBook();
        WelcomePanel().Visibility(hasBook ? Visibility::Collapsed : Visibility::Visible);
        InfoBar().Visibility(hasBook && m_settings.showInfo ? Visibility::Visible : Visibility::Collapsed);

        // Page / fit / zoom changed: save the comic's state once things settle for a moment.
        if (hasBook && m_viewer->CaptureState() != m_lastRecordedState)
        {
            m_historyTimer.Stop();
            m_historyTimer.Start();
        }

        auto const& options = m_viewer->Options();
        SpreadToggle().IsChecked(options.spread);
        MangaToggle().IsChecked(options.manga);
        if (hasBook != m_toolbarHasBook)
        {
            m_toolbarHasBook = hasBook;
            QueueToolbarLayout(); // page navigation appears / disappears
        }

        std::wstring title = kAppName;
        std::wstring pageLabel;
        if (hasBook)
        {
            size_t page = m_viewer->CurrentPage() + 1;
            size_t count = m_viewer->PageCount();
            std::wstring pages = m_viewer->VisibleCount() == 2 ? std::format(L"{}-{}", page, page + 1) : std::to_wstring(page);
            pageLabel = std::format(L"{} / {}", pages, count);

            std::wstring info = std::format(L"{} / {}   ·   {}", pages, count, FitName(options.fit));
            if (std::abs(m_viewer->Zoom() - 1.0f) > 0.001f) info += std::format(L" {:.0f}%", m_viewer->Zoom() * 100.0f);
            if (options.spread) info += L"   ·   Spread";
            if (options.manga) info += L"   ·   Manga";
            if (m_viewer->Rotation() != 0) info += std::format(L"   ·   {}°", m_viewer->Rotation() * 90);
            if (m_waitingForPages) info += L"   ·   loading…";
            if (info != m_lastInfo)
            {
                m_lastInfo = info;
                InfoText().Text(info);
            }
            title = std::format(L"{} — {} / {} — {}", std::filesystem::path(m_bookPath).filename().wstring(), pages, count, kAppName);
        }
        if (title != m_lastTitle)
        {
            m_lastTitle = title;
            BookTitle().Text(hasBook ? std::filesystem::path(m_bookPath).filename().wstring() : std::wstring(kAppName));
            Controls::ToolTipService::SetToolTip(BookTitle(), hasBook ? box_value(m_bookPath) : nullptr);
            if (hasBook) PageText().Text(pageLabel);
            Title(title); // taskbar / Alt+Tab
        }
    }

    // ----------------------------------------------------------------- opening

    void MainWindow::OpenPath(std::wstring path)
    {
        // Canonical absolute form, so sibling lookup and saved positions match.
        if (DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr))
        {
            std::wstring full(size, L'\0');
            size = GetFullPathNameW(path.c_str(), size, full.data(), nullptr);
            full.resize(size);
            if (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
            path = std::move(full);
        }
        OpenPathAsync(std::move(path));
    }

    winrt::fire_and_forget MainWindow::OpenPathAsync(std::wstring path)
    {
        auto strong = get_strong();
        uint64_t request = ++m_openRequest;

        std::shared_ptr<cdx::ComicSource> source;
        size_t startPage = 0;
        std::wstring error;

        // Indexing an archive touches the disk; keep the UI responsive meanwhile.
        co_await winrt::resume_background();
        try
        {
            source = cdx::OpenComic(path, &startPage);
        }
        catch (winrt::hresult_error const& e)
        {
            error = e.message();
        }
        catch (...)
        {
            error = L"Cannot open " + path;
        }
        co_await wil::resume_foreground(m_dispatcher);

        if (m_closing || request != m_openRequest) co_return; // superseded by a newer request
        if (!source)
        {
            ShowMessage(error);
            co_return;
        }

        RecordBookState(); // the book being left
        m_history.SaveIfDirty();

        // Opening a single image shows its folder as the book.
        bool isImage = cdx::IsImageFileName(path);
        std::wstring bookPath = isImage ? std::filesystem::path(path).parent_path().wstring() : path;
        auto saved = m_history.Find(bookPath);

        m_bookPath = bookPath;
        m_settings.lastFolder = std::filesystem::path(path).parent_path().wstring();
        m_settings.AddRecent(path);
        m_loader->SetSource(source);
        m_viewer->SetBook(source, startPage);

        // Comics read before come back exactly as they were left (page, fit, zoom, spread,
        // manga, rotation); new ones use the current view options.
        bool resumed = false;
        if (saved)
        {
            cdx::BookState state = *saved;
            if (isImage) state.page = startPage; // the image that was opened wins over the saved page
            m_viewer->ApplyState(state);
            resumed = !isImage && state.page > 0 && state.page < source->PageCount();
        }
        RecordBookState(); // stamps "last opened"

        if (resumed) ShowMessage(std::format(L"Resumed at page {} of {}", m_viewer->CurrentPage() + 1, source->PageCount()));
        else MessageBar().Visibility(Visibility::Collapsed);

        Render();
    }

    void MainWindow::RecordBookState()
    {
        if (m_viewer && m_viewer->HasBook() && !m_bookPath.empty())
        {
            m_lastRecordedState = m_viewer->CaptureState();
            m_history.Record(m_bookPath, m_lastRecordedState);
        }
    }

    void MainWindow::OpenSibling(int direction)
    {
        if (m_bookPath.empty()) return;
        auto siblings = cdx::ListSiblingComics(m_bookPath);
        auto it = std::find_if(siblings.begin(), siblings.end(), [&](std::wstring const& s) {
            return CompareStringOrdinal(s.c_str(), -1, m_bookPath.c_str(), -1, TRUE) == CSTR_EQUAL;
        });
        if (it == siblings.end())
        {
            ShowMessage(L"No other comics in this folder");
            return;
        }
        auto index = (it - siblings.begin()) + direction;
        if (index < 0 || index >= static_cast<ptrdiff_t>(siblings.size()))
        {
            ShowMessage(direction > 0 ? L"This is the last comic in the folder" : L"This is the first comic in the folder");
            return;
        }
        OpenPath(siblings[static_cast<size_t>(index)]);
    }

    void MainWindow::ShowOpenDialog(bool pickFolder)
    {
        auto dialog = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
        FILEOPENDIALOGOPTIONS options{};
        check_hresult(dialog->GetOptions(&options));
        check_hresult(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (pickFolder ? FOS_PICKFOLDERS : 0)));
        if (!pickFolder)
        {
            COMDLG_FILTERSPEC filters[] = {
                { L"Comics and images", L"*.cbz;*.cbr;*.cb7;*.cbt;*.zip;*.rar;*.7z;*.tar;*.jpg;*.jpeg;*.png;*.gif;*.bmp;*.webp;*.avif;*.jxl;*.tif;*.tiff;*.heic" },
                { L"All files", L"*.*" },
            };
            check_hresult(dialog->SetFileTypes(ARRAYSIZE(filters), filters));
        }
        if (!m_settings.lastFolder.empty())
        {
            com_ptr<IShellItem> folder;
            if (SUCCEEDED(SHCreateItemFromParsingName(m_settings.lastFolder.c_str(), nullptr, IID_PPV_ARGS(folder.put()))))
            {
                dialog->SetFolder(folder.get());
            }
        }

        HWND hwnd = nullptr;
        check_hresult(this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd));
        if (FAILED(dialog->Show(hwnd))) return; // cancelled

        com_ptr<IShellItem> item;
        check_hresult(dialog->GetResult(item.put()));
        wil::unique_cotaskmem_string path;
        check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
        OpenPath(path.get());
    }

    // ------------------------------------------------------------------- input

    void MainWindow::NextPage()
    {
        if (!m_viewer->NextPage() && m_viewer->HasBook())
        {
            ShowMessage(L"Last page  ·  Ctrl+→ opens the next comic");
        }
    }

    void MainWindow::PrevPage()
    {
        m_viewer->PrevPage();
    }

    // OEM keys (+ - [ ] /) have no named VirtualKey value.
#pragma warning(push)
#pragma warning(disable : 4063)
    void MainWindow::OnPreviewKeyDown(IInspectable const&, KeyRoutedEventArgs const& e)
    {
        if (m_closing) return;
        bool ctrl = IsKeyDown(VirtualKey::Control);
        bool shift = IsKeyDown(VirtualKey::Shift);
        bool manga = m_viewer->Options().manga;
        auto& viewer = *m_viewer;
        bool handled = true;

        switch (e.Key())
        {
        case VirtualKey::Right:
            if (ctrl) OpenSibling(+1);
            else if (manga) PrevPage();
            else NextPage();
            break;
        case VirtualKey::Left:
            if (ctrl) OpenSibling(-1);
            else if (manga) NextPage();
            else PrevPage();
            break;
        case VirtualKey::PageDown: NextPage(); break;
        case VirtualKey::PageUp:
        case VirtualKey::Back: PrevPage(); break;
        case VirtualKey::Space:
            if (shift) viewer.ScrollOrPrev();
            else if (!viewer.ScrollOrNext() && viewer.HasBook()) ShowMessage(L"Last page  ·  Ctrl+→ opens the next comic");
            break;
        case VirtualKey::Down:
            if (!viewer.ScrollOrNext(0.25f) && viewer.HasBook()) ShowMessage(L"Last page  ·  Ctrl+→ opens the next comic");
            break;
        case VirtualKey::Up: viewer.ScrollOrPrev(0.25f); break;
        case VirtualKey::Home: viewer.GoTo(0); break;
        case VirtualKey::End: viewer.GoTo(viewer.PageCount() ? viewer.PageCount() - 1 : 0); break;

        case VirtualKey::F: viewer.SetFit(cdx::FitMode::Page); break;
        case VirtualKey::W: viewer.SetFit(cdx::FitMode::Width); break;
        case VirtualKey::H: viewer.SetFit(cdx::FitMode::Height); break;
        case VirtualKey::A: viewer.SetFit(cdx::FitMode::Actual); break;
        case VirtualKey::Number1:
        case VirtualKey::NumberPad1:
            if (viewer.Options().spread) viewer.ToggleSpread();
            break;
        case VirtualKey::Number2:
        case VirtualKey::NumberPad2:
            if (!viewer.Options().spread) viewer.ToggleSpread();
            break;
        case VirtualKey::C:
            viewer.Options().coverAlone = !viewer.Options().coverAlone;
            ShowMessage(viewer.Options().coverAlone ? L"Cover shown alone" : L"Cover paired with page 2");
            break;
        case VirtualKey::M:
            viewer.ToggleManga();
            ShowMessage(viewer.Options().manga ? L"Manga mode: right to left" : L"Left to right");
            break;
        case VirtualKey::R: viewer.Rotate(shift ? -1 : 1); break;
        case VirtualKey::Add:
        case static_cast<VirtualKey>(VK_OEM_PLUS): viewer.ZoomBy(1.25f); break;
        case VirtualKey::Subtract:
        case static_cast<VirtualKey>(VK_OEM_MINUS): viewer.ZoomBy(0.8f); break;
        case VirtualKey::Number0:
        case VirtualKey::NumberPad0: viewer.ResetZoom(); break;
        case static_cast<VirtualKey>(VK_OEM_4): OpenSibling(-1); break; // [
        case static_cast<VirtualKey>(VK_OEM_6): OpenSibling(+1); break; // ]

        case VirtualKey::I: m_settings.showInfo = !m_settings.showInfo; break;
        case VirtualKey::T:
        {
            static constexpr const wchar_t* kNames[] = { L"Theme: follow Windows", L"Theme: light", L"Theme: dark" };
            m_settings.theme = (m_settings.theme + 1) % 3;
            ApplyTheme();
            ShowMessage(kNames[m_settings.theme]);
            break;
        }
        case VirtualKey::F1:
        case static_cast<VirtualKey>(VK_OEM_2): // ? /
            HelpPanel().Visibility(HelpPanel().Visibility() == Visibility::Visible ? Visibility::Collapsed : Visibility::Visible);
            break;
        case VirtualKey::F11:
        case VirtualKey::Enter: ToggleFullscreen(); break;
        case VirtualKey::F10:
        case VirtualKey::Application:
            if (IsFullscreen()) SetTitleBarRevealed(true);
            ShowMainMenu();
            e.Handled(true);
            return;
        case VirtualKey::Escape:
            if (HelpPanel().Visibility() == Visibility::Visible) HelpPanel().Visibility(Visibility::Collapsed);
            else if (IsFullscreen()) ToggleFullscreen();
            else Close();
            e.Handled(true);
            return;
        case VirtualKey::O:
            if (ctrl) ShowOpenDialog(shift);
            else handled = false;
            break;
        default:
            handled = false;
            break;
        }

        e.Handled(handled);
        if (handled) Render();
    }
#pragma warning(pop)

    void MainWindow::OnPointerPressed(IInspectable const&, PointerRoutedEventArgs const& e)
    {
        if (m_closing) return;
        InputHost().Focus(FocusState::Programmatic);
        auto point = e.GetCurrentPoint(Panel());
        auto props = point.Properties();
        e.Handled(true);

        if (props.IsLeftButtonPressed())
        {
            m_dragging = Panel().CapturePointer(e.Pointer());
            m_dragMoved = false;
            m_dragStart = m_dragLast = point.Position();
            return;
        }
        // Plain left / right clicks deliberately do nothing (no accidental page turns).
        if (props.IsXButton1Pressed()) PrevPage();
        else if (props.IsXButton2Pressed()) NextPage();
        else if (props.IsMiddleButtonPressed()) ToggleFullscreen();
        Render();
    }

    void MainWindow::OnPointerMoved(IInspectable const&, PointerRoutedEventArgs const& e)
    {
        if (m_closing) return;
        auto position = e.GetCurrentPoint(Panel()).Position();
        if (!m_dragging)
        {
            // Fullscreen: touching the top edge reveals the title bar and menu.
            if (IsFullscreen())
            {
                if (position.Y <= 4.0f && !m_titleBarRevealed) SetTitleBarRevealed(true);
                else if (m_titleBarRevealed && position.Y > AppTitleBar().ActualHeight() + 24.0f && !MainMenu().IsOpen() &&
                         !FitMenu().IsOpen() && !PageFlyout().IsOpen())
                {
                    SetTitleBarRevealed(false);
                }
            }
            return;
        }
        float dx = position.X - m_dragStart.X;
        float dy = position.Y - m_dragStart.Y;
        if (!m_dragMoved && dx * dx + dy * dy > 36.0f) m_dragMoved = true;
        if (m_dragMoved)
        {
            float moveX = (position.X - m_dragLast.X) * m_renderer->ScaleX();
            float moveY = (position.Y - m_dragLast.Y) * m_renderer->ScaleY();
            if (m_viewer->ScrollBy(-moveX, -moveY)) Render();
        }
        m_dragLast = position;
        e.Handled(true);
    }

    void MainWindow::OnPointerReleased(IInspectable const&, PointerRoutedEventArgs const& e)
    {
        if (!m_dragging) return;
        m_dragging = false;
        Panel().ReleasePointerCapture(e.Pointer());
        e.Handled(true);
    }

    void MainWindow::OnPointerCaptureLost(IInspectable const&, PointerRoutedEventArgs const&)
    {
        m_dragging = false;
    }

    void MainWindow::OnPointerWheelChanged(IInspectable const&, PointerRoutedEventArgs const& e)
    {
        if (m_closing) return;
        auto props = e.GetCurrentPoint(Panel()).Properties();
        int delta = props.MouseWheelDelta();
        e.Handled(true);
        if (delta == 0) return;

        if (props.IsHorizontalMouseWheel())
        {
            m_viewer->ScrollBy(delta / 120.0f * m_renderer->PixelWidth() * 0.2f, 0, true);
        }
        else if (IsKeyDown(VirtualKey::Control))
        {
            m_viewer->ZoomBy(std::pow(1.15f, delta / 120.0f));
        }
        else
        {
            // A small bump before wheel page turns: when scrolling reaches the edge of a
            // page it rests there briefly before the wheel can turn it, so the page end
            // is noticeable without stopping a continuous scroll. Without anything to
            // scroll, one notch turns the page, with a short cooldown against skipping.
            using namespace std::chrono_literals;
            constexpr auto kEdgeHold = 200ms;     // rest at the edge after scrolling into it
            constexpr auto kTurnCooldown = 150ms; // minimum time between wheel page turns
            auto now = std::chrono::steady_clock::now();

            if (m_viewer->ScrollBy(0, -delta / 120.0f * m_renderer->PixelHeight() * 0.2f, true))
            {
                m_wheelAccumulator = 0;
                m_wheelBlockedUntil = now + kEdgeHold; // counts from the last scroll step
            }
            else if (now < m_wheelBlockedUntil)
            {
                m_wheelAccumulator = 0; // resting at the edge (or just turned)
            }
            else
            {
                // A full notch (or a few touchpad ticks) turns the page.
                m_wheelAccumulator += delta;
                bool turned = false;
                if (m_wheelAccumulator <= -WHEEL_DELTA)
                {
                    NextPage();
                    turned = true;
                }
                else if (m_wheelAccumulator >= WHEEL_DELTA)
                {
                    turned = m_viewer->ScrollOrPrev();
                }
                if (turned)
                {
                    m_wheelAccumulator = 0;
                    m_wheelBlockedUntil = now + kTurnCooldown;
                }
            }
        }
        Render();
    }

    void MainWindow::OnDragOver(IInspectable const&, DragEventArgs const& e)
    {
        if (e.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems()))
        {
            e.AcceptedOperation(Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
            e.DragUIOverride().Caption(L"Open");
        }
    }

    winrt::fire_and_forget MainWindow::OnDrop(IInspectable const&, DragEventArgs e)
    {
        auto strong = get_strong();
        if (!e.DataView().Contains(Windows::ApplicationModel::DataTransfer::StandardDataFormats::StorageItems())) co_return;
        auto deferral = e.GetDeferral();
        auto items = co_await e.DataView().GetStorageItemsAsync();
        deferral.Complete();
        if (items.Size() > 0) OpenPath(std::wstring(items.GetAt(0).Path()));
    }

    // --------------------------------------------------------- title bar / menus

    void MainWindow::OnHelpBackdropTapped(IInspectable const&, TappedRoutedEventArgs const& e)
    {
        // A click on the dimmed area around the help card closes it.
        e.Handled(true);
        HelpPanel().Visibility(Visibility::Collapsed);
        InputHost().Focus(FocusState::Programmatic);
    }

    void MainWindow::OnHelpCardTapped(IInspectable const&, TappedRoutedEventArgs const& e)
    {
        e.Handled(true); // clicks on the card itself must not reach the backdrop
    }

    void MainWindow::UpdateDefaultViewerLink()
    {
        // Offered on the welcome screen until the app handles comic files.
        DefaultViewerLink().Visibility(cdx::FileAssociations::IsDefaultComicViewer() ? Visibility::Collapsed : Visibility::Visible);
    }

    void MainWindow::ShowMainMenu()
    {
        MainMenu().ShowAt(HamburgerButton());
    }

    void MainWindow::OnCommand(IInspectable const& sender, RoutedEventArgs const&)
    {
        if (auto element = sender.try_as<FrameworkElement>())
        {
            ExecuteCommand(unbox_value_or<hstring>(element.Tag(), L""));
        }
    }

    // Commands shared by the hamburger menu, the title bar buttons and the welcome screen.
    void MainWindow::ExecuteCommand(std::wstring_view command)
    {
        if (m_closing) return;
        auto& viewer = *m_viewer;
        if (command == L"open") ShowOpenDialog(false);
        else if (command == L"openFolder") ShowOpenDialog(true);
        else if (command == L"nextBook") OpenSibling(+1);
        else if (command == L"prevBook") OpenSibling(-1);
        else if (command == L"firstPage") viewer.GoTo(0);
        else if (command == L"lastPage") viewer.GoTo(viewer.PageCount() ? viewer.PageCount() - 1 : 0);
        else if (command == L"nextPage") NextPage();
        else if (command == L"prevPage") PrevPage();
        else if (command == L"fitPage") viewer.SetFit(cdx::FitMode::Page);
        else if (command == L"fitWidth") viewer.SetFit(cdx::FitMode::Width);
        else if (command == L"fitHeight") viewer.SetFit(cdx::FitMode::Height);
        else if (command == L"fitActual") viewer.SetFit(cdx::FitMode::Actual);
        else if (command == L"spread") viewer.ToggleSpread();
        else if (command == L"cover") viewer.Options().coverAlone = !viewer.Options().coverAlone;
        else if (command == L"manga") viewer.ToggleManga();
        else if (command == L"rotateRight") viewer.Rotate(1);
        else if (command == L"rotateLeft") viewer.Rotate(-1);
        else if (command == L"zoomIn") viewer.ZoomBy(1.25f);
        else if (command == L"zoomOut") viewer.ZoomBy(0.8f);
        else if (command == L"zoomReset") viewer.ResetZoom();
        else if (command == L"info") m_settings.showInfo = !m_settings.showInfo;
        else if (command == L"smoothScroll") m_viewer->SetSmoothScrolling(!m_viewer->SmoothScrolling());
        else if (command.starts_with(L"sharpen") && command.size() == 8) m_renderer->SetSharpening(static_cast<cdx::Sharpening>(command[7] - L'0'));
        else if (command == L"enlarge") viewer.Options().enlargeSmall = !viewer.Options().enlargeSmall;
        else if (command == L"fullscreen") ToggleFullscreen();
        else if (command.starts_with(L"theme") && command.size() == 6)
        {
            m_settings.theme = command[5] - L'0';
            ApplyTheme();
        }
        else if (command == L"assocDefault")
        {
            if (cdx::FileAssociations::IsDefaultComicViewer())
            {
                ShowMessage(L"ComicsDisplayExp already opens CBZ, CBR, CB7 and CBT files");
            }
            else
            {
                cdx::FileAssociations::RegisterAsComicViewer();
                if (cdx::FileAssociations::IsDefaultComicViewer())
                {
                    ShowMessage(L"Done: CBZ, CBR, CB7 and CBT files now open in ComicsDisplayExp");
                }
                else
                {
                    // Another app is the user's protected choice; only they can switch it.
                    cdx::FileAssociations::OpenDefaultAppsSettings();
                    ShowMessage(L"Windows asks you to confirm: in the Settings window, choose ComicsDisplayExp for .cbz and .cbr");
                }
            }
            UpdateDefaultViewerLink();
        }
        else if (command == L"assocArchives")
        {
            bool enable = !cdx::FileAssociations::IsInOpenWithForArchives();
            cdx::FileAssociations::SetOpenWithForArchives(enable);
            ShowMessage(enable ? L"Added to “Open with” for ZIP, RAR and 7Z (their default app is unchanged)"
                               : L"Removed from “Open with” for ZIP, RAR and 7Z");
        }
        else if (command == L"assocRemove")
        {
            cdx::FileAssociations::UnregisterAll();
            ShowMessage(L"All file associations removed");
            UpdateDefaultViewerLink();
        }
        else if (command == L"help")
            HelpPanel().Visibility(HelpPanel().Visibility() == Visibility::Visible ? Visibility::Collapsed : Visibility::Visible);
        else if (command == L"exit")
        {
            Close();
            return;
        }

        InputHost().Focus(FocusState::Programmatic);
        Render();
    }

    void MainWindow::OnMenuOpening(IInspectable const&, IInspectable const&)
    {
        if (m_closing) return;
        SyncMenuState();
    }

    void MainWindow::SyncMenuState()
    {
        auto const& options = m_viewer->Options();
        auto fit = options.fit;
        MenuFitPage().IsChecked(fit == cdx::FitMode::Page);
        MenuFitWidth().IsChecked(fit == cdx::FitMode::Width);
        MenuFitHeight().IsChecked(fit == cdx::FitMode::Height);
        MenuFitActual().IsChecked(fit == cdx::FitMode::Actual);
        BarFitPage().IsChecked(fit == cdx::FitMode::Page);
        BarFitWidth().IsChecked(fit == cdx::FitMode::Width);
        BarFitHeight().IsChecked(fit == cdx::FitMode::Height);
        BarFitActual().IsChecked(fit == cdx::FitMode::Actual);
        MenuSpread().IsChecked(options.spread);
        MenuCover().IsChecked(options.coverAlone);
        MenuManga().IsChecked(options.manga);
        MenuInfo().IsChecked(m_settings.showInfo);
        MenuSmooth().IsChecked(m_viewer->SmoothScrolling());
        auto sharpening = m_renderer->GetSharpening();
        MenuSharpenOff().IsChecked(sharpening == cdx::Sharpening::Off);
        MenuSharpenLight().IsChecked(sharpening == cdx::Sharpening::Light);
        MenuSharpenMedium().IsChecked(sharpening == cdx::Sharpening::Medium);
        MenuSharpenStrong().IsChecked(sharpening == cdx::Sharpening::Strong);
        MenuEnlarge().IsChecked(options.enlargeSmall);
        MenuThemeSystem().IsChecked(m_settings.theme == 0);
        MenuThemeLight().IsChecked(m_settings.theme == 1);
        MenuThemeDark().IsChecked(m_settings.theme == 2);
        MenuDefaultViewer().IsChecked(cdx::FileAssociations::IsDefaultComicViewer());
        MenuOpenWithArchives().IsChecked(cdx::FileAssociations::IsInOpenWithForArchives());

        bool hasBook = m_viewer->HasBook();
        NextBookItem().IsEnabled(hasBook);
        PrevBookItem().IsEnabled(hasBook);
        RebuildRecentMenu();
    }

    void MainWindow::RebuildRecentMenu()
    {
        auto items = RecentMenu().Items();
        items.Clear();
        if (m_settings.recent.empty())
        {
            Controls::MenuFlyoutItem none;
            none.Text(L"No recent comics");
            none.IsEnabled(false);
            items.Append(none);
            return;
        }
        for (auto const& path : m_settings.recent)
        {
            Controls::MenuFlyoutItem item;
            item.Text(std::filesystem::path(path).filename().wstring());
            Controls::ToolTipService::SetToolTip(item, box_value(path));
            item.Click([this, path](auto&&, auto&&) { OpenPath(path); });
            items.Append(item);
        }
        items.Append(Controls::MenuFlyoutSeparator{});
        Controls::MenuFlyoutItem clear;
        clear.Text(L"Clear recent");
        clear.Click([this](auto&&, auto&&) { m_settings.recent.clear(); });
        items.Append(clear);
    }

    void MainWindow::OnFlyoutClosed(IInspectable const&, IInspectable const&)
    {
        if (m_closing) return;
        InputHost().Focus(FocusState::Programmatic);
        if (m_titleBarRevealed) SetTitleBarRevealed(false);
    }

    void MainWindow::OnPageFlyoutOpening(IInspectable const&, IInspectable const&)
    {
        if (!m_viewer->HasBook()) return;
        size_t page = m_viewer->CurrentPage();
        m_syncingSlider = true;
        PageSlider().Maximum(static_cast<double>(m_viewer->PageCount()));
        PageSlider().Value(static_cast<double>(page + 1));
        m_syncingSlider = false;
        PageFlyoutTitle().Text(std::format(L"Page {} of {}", page + 1, m_viewer->PageCount()));
        PageFlyoutName().Text(m_viewer->Book()->PageName(page));
    }

    void MainWindow::OnPageSliderChanged(IInspectable const&, Controls::Primitives::RangeBaseValueChangedEventArgs const& e)
    {
        // Also fires while the XAML is still loading, before the viewer exists.
        if (m_syncingSlider || !m_viewer || !m_viewer->HasBook()) return;
        size_t page = static_cast<size_t>(std::max(1.0, std::round(e.NewValue()))) - 1;
        if (m_viewer->GoTo(page)) Render();
        PageFlyoutTitle().Text(std::format(L"Page {} of {}", page + 1, m_viewer->PageCount()));
        PageFlyoutName().Text(m_viewer->Book()->PageName(std::min(page, m_viewer->PageCount() - 1)));
    }

    void MainWindow::OnTitleBarPointerExited(IInspectable const&, PointerRoutedEventArgs const& e)
    {
        if (m_closing) return;
        if (!m_titleBarRevealed || MainMenu().IsOpen() || FitMenu().IsOpen() || PageFlyout().IsOpen()) return;
        if (e.GetCurrentPoint(AppTitleBar()).Position().Y >= AppTitleBar().ActualHeight()) SetTitleBarRevealed(false);
    }
}
