#pragma once

#include "Viewer.h"

namespace cdx
{
    // Persistent preferences in %LOCALAPPDATA%\ComicsDisplayExp\settings.ini.
    class Settings
    {
    public:
        Settings();

        void Load();
        void Save() const;


        // Most recently opened comics, newest first.
        void AddRecent(std::wstring const& path);
        std::vector<std::wstring> recent;

        ViewOptions view;
        bool showInfo = true;
        bool smoothScrolling = true;
        int sharpening = 0; // cdx::Sharpening
        int theme = 0;      // 0 = follow Windows, 1 = light, 2 = dark
        std::wstring lastFolder;

        bool hasWindowRect = false;
        int windowX = 0, windowY = 0, windowWidth = 0, windowHeight = 0;
        bool maximized = true; // first launch opens maximized

    private:
        std::wstring m_file;
    };
}
