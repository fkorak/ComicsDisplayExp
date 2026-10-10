#include "pch.h"
#include "App.xaml.h"
#include "FileAssociations.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

namespace winrt::ComicsDisplayExp::implementation
{
    namespace
    {
        // Maintenance commands, run without showing a window (used by the installer):
        //   --register-comics    make the app the default for CBZ / CBR / CB7 / CBT
        //   --register-archives  add the app to "Open with" for ZIP / RAR / 7Z
        //   --unregister         remove all file associations (uninstall)
        // Returns false if the arguments contain no such command.
        bool RunMaintenanceCommands(std::vector<std::wstring> const& args)
        {
            bool ranCommand = false;
            for (auto const& arg : args)
            {
                if (arg == L"--register-comics")
                {
                    cdx::FileAssociations::RegisterAsComicViewer();
                    // Another app is the user's protected choice: let them confirm in Settings.
                    if (!cdx::FileAssociations::IsDefaultComicViewer()) cdx::FileAssociations::OpenDefaultAppsSettings();
                    ranCommand = true;
                }
                else if (arg == L"--register-archives")
                {
                    cdx::FileAssociations::SetOpenWithForArchives(true);
                    ranCommand = true;
                }
                else if (arg == L"--unregister")
                {
                    cdx::FileAssociations::UnregisterAll();
                    ranCommand = true;
                }
            }
            return ranCommand;
        }
    }

    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        std::vector<std::wstring> args;
        int argc = 0;
        if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc))
        {
            for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
            LocalFree(argv);
        }

        if (RunMaintenanceCommands(args))
        {
            Exit();
            return;
        }

        auto mainWindow = make<MainWindow>();
        window = mainWindow;
        window.Activate();

        // Otherwise the first argument (if any) is the comic to open.
        if (!args.empty())
        {
            get_self<MainWindow>(mainWindow)->OpenPath(args[0]);
        }
    }
}
