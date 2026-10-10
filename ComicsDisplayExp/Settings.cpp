#include "pch.h"
#include "Settings.h"

#include <ShlObj.h>
#include <format>

namespace cdx
{
    namespace
    {
        constexpr size_t kMaxRecent = 10;

        int ReadInt(std::wstring const& file, const wchar_t* section, const wchar_t* key, int fallback)
        {
            return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, file.c_str()));
        }

        void WriteInt(std::wstring const& file, const wchar_t* section, const wchar_t* key, int value)
        {
            WritePrivateProfileStringW(section, key, std::to_wstring(value).c_str(), file.c_str());
        }

        std::wstring ReadString(std::wstring const& file, const wchar_t* section, const wchar_t* key)
        {
            std::wstring buffer(MAX_PATH * 4, L'\0');
            DWORD len = GetPrivateProfileStringW(section, key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), file.c_str());
            buffer.resize(len);
            return buffer;
        }
    }

    Settings::Settings()
    {
        wil::unique_cotaskmem_string localAppData;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)))
        {
            std::wstring dir = std::wstring(localAppData.get()) + L"\\ComicsDisplayExp";
            CreateDirectoryW(dir.c_str(), nullptr);
            m_file = dir + L"\\settings.ini";
        }
    }

    void Settings::Load()
    {
        if (m_file.empty()) return;
        view.fit = static_cast<FitMode>(std::clamp(ReadInt(m_file, L"View", L"Fit", 0), 0, 3));
        view.spread = ReadInt(m_file, L"View", L"Spread", 0) != 0;
        view.manga = ReadInt(m_file, L"View", L"Manga", 0) != 0;
        view.coverAlone = ReadInt(m_file, L"View", L"CoverAlone", 1) != 0;
        showInfo = ReadInt(m_file, L"View", L"ShowInfo", 1) != 0;
        smoothScrolling = ReadInt(m_file, L"View", L"SmoothScroll", 1) != 0;
        sharpening = std::clamp(ReadInt(m_file, L"View", L"Sharpen", 0), 0, 3);
        theme = std::clamp(ReadInt(m_file, L"View", L"Theme", 0), 0, 2);
        view.enlargeSmall = ReadInt(m_file, L"View", L"EnlargeSmall", 1) != 0;
        lastFolder = ReadString(m_file, L"Files", L"LastFolder");
        recent.clear();
        for (int i = 0; i < kMaxRecent; ++i)
        {
            auto path = ReadString(m_file, L"Recent", std::format(L"File{}", i).c_str());
            if (!path.empty()) recent.push_back(std::move(path));
        }

        windowWidth = ReadInt(m_file, L"Window", L"Width", 0);
        windowHeight = ReadInt(m_file, L"Window", L"Height", 0);
        windowX = ReadInt(m_file, L"Window", L"X", 0);
        windowY = ReadInt(m_file, L"Window", L"Y", 0);
        maximized = ReadInt(m_file, L"Window", L"Maximized", 1) != 0; // first launch: maximized
        hasWindowRect = windowWidth >= 200 && windowHeight >= 150;
    }

    void Settings::Save() const
    {
        if (m_file.empty()) return;
        WriteInt(m_file, L"View", L"Fit", static_cast<int>(view.fit));
        WriteInt(m_file, L"View", L"Spread", view.spread);
        WriteInt(m_file, L"View", L"Manga", view.manga);
        WriteInt(m_file, L"View", L"CoverAlone", view.coverAlone);
        WriteInt(m_file, L"View", L"ShowInfo", showInfo);
        WriteInt(m_file, L"View", L"SmoothScroll", smoothScrolling);
        WriteInt(m_file, L"View", L"Sharpen", sharpening);
        WriteInt(m_file, L"View", L"Theme", theme);
        WriteInt(m_file, L"View", L"EnlargeSmall", view.enlargeSmall);
        WritePrivateProfileStringW(L"Files", L"LastFolder", lastFolder.c_str(), m_file.c_str());
        WritePrivateProfileStringW(L"Positions", nullptr, nullptr, m_file.c_str()); // superseded by history.tsv
        WritePrivateProfileStringW(L"Recent", nullptr, nullptr, m_file.c_str()); // rewrite the whole section
        for (size_t i = 0; i < recent.size(); ++i)
        {
            WritePrivateProfileStringW(L"Recent", std::format(L"File{}", i).c_str(), recent[i].c_str(), m_file.c_str());
        }
        if (hasWindowRect)
        {
            WriteInt(m_file, L"Window", L"X", windowX);
            WriteInt(m_file, L"Window", L"Y", windowY);
            WriteInt(m_file, L"Window", L"Width", windowWidth);
            WriteInt(m_file, L"Window", L"Height", windowHeight);
            WriteInt(m_file, L"Window", L"Maximized", maximized);
        }
    }

    void Settings::AddRecent(std::wstring const& path)
    {
        std::erase_if(recent, [&](std::wstring const& p) {
            return CompareStringOrdinal(p.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL;
        });
        recent.insert(recent.begin(), path);
        if (recent.size() > kMaxRecent) recent.resize(kMaxRecent);
    }

}
