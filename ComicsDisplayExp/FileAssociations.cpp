#include "pch.h"
#include "FileAssociations.h"

#include <ShlObj.h>

namespace cdx::FileAssociations
{
    namespace
    {
        constexpr wchar_t kAppName[] = L"ComicsDisplayExp";
        constexpr wchar_t kExeName[] = L"ComicsDisplayExp.exe";
        constexpr wchar_t kComicProgId[] = L"ComicsDisplayExp.Comic";
        constexpr wchar_t kArchiveProgId[] = L"ComicsDisplayExp.Archive";
        constexpr wchar_t kClasses[] = L"Software\\Classes\\";
        constexpr wchar_t kAppKey[] = L"Software\\ComicsDisplayExp";
        constexpr wchar_t kCapabilities[] = L"Software\\ComicsDisplayExp\\Capabilities";
        constexpr wchar_t kPreviousDefaults[] = L"Software\\ComicsDisplayExp\\PreviousDefaults";

        constexpr const wchar_t* kComicTypes[] = { L".cbz", L".cbr", L".cb7", L".cbt" };
        constexpr const wchar_t* kArchiveTypes[] = { L".zip", L".rar", L".7z" };

        std::wstring ExePath()
        {
            std::wstring path(MAX_PATH, L'\0');
            for (;;)
            {
                DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if (len < path.size())
                {
                    path.resize(len);
                    return path;
                }
                path.resize(path.size() * 2);
            }
        }

        std::wstring OpenCommand() { return L"\"" + ExePath() + L"\" \"%1\""; }

        void SetString(std::wstring const& subKey, const wchar_t* name, std::wstring const& value)
        {
            RegSetKeyValueW(HKEY_CURRENT_USER, subKey.c_str(), name, REG_SZ, value.c_str(),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        }

        std::optional<std::wstring> GetString(std::wstring const& subKey, const wchar_t* name)
        {
            DWORD bytes = 0;
            if (RegGetValueW(HKEY_CURRENT_USER, subKey.c_str(), name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            std::wstring value(bytes / sizeof(wchar_t), L'\0');
            if (RegGetValueW(HKEY_CURRENT_USER, subKey.c_str(), name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            value.resize(wcsnlen(value.c_str(), value.size()));
            return value;
        }

        bool KeyExists(std::wstring const& subKey)
        {
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
            RegCloseKey(key);
            return true;
        }

        // Removes a key only if nothing is left in it, so other apps' data is never touched.
        void DeleteKeyIfEmpty(std::wstring const& subKey)
        {
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return;
            DWORD subKeys = 0, values = 0;
            LSTATUS status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subKeys, nullptr, nullptr, &values,
                                              nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(key);
            if (status == ERROR_SUCCESS && subKeys == 0 && values == 0) RegDeleteKeyW(HKEY_CURRENT_USER, subKey.c_str());
        }

        void WriteProgId(const wchar_t* progId, const wchar_t* typeName)
        {
            std::wstring key = std::wstring(kClasses) + progId;
            SetString(key, nullptr, typeName);
            SetString(key, L"FriendlyTypeName", typeName);
            SetString(key + L"\\DefaultIcon", nullptr, L"\"" + ExePath() + L"\",0");
            SetString(key + L"\\shell", nullptr, L"open");
            SetString(key + L"\\shell\\open", L"FriendlyAppName", kAppName);
            SetString(key + L"\\shell\\open\\command", nullptr, OpenCommand());
        }

        // "Open with" list entry for a type: never changes its default.
        void AddOpenWith(const wchar_t* extension, const wchar_t* progId)
        {
            std::wstring key = std::wstring(kClasses) + extension + L"\\OpenWithProgids";
            RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), progId, REG_NONE, nullptr, 0);
        }

        void RemoveOpenWith(const wchar_t* extension, const wchar_t* progId)
        {
            std::wstring typeKey = std::wstring(kClasses) + extension;
            RegDeleteKeyValueW(HKEY_CURRENT_USER, (typeKey + L"\\OpenWithProgids").c_str(), progId);
            DeleteKeyIfEmpty(typeKey + L"\\OpenWithProgids");
            DeleteKeyIfEmpty(typeKey);
        }

        // Applications\ComicsDisplayExp.exe: how the app is listed in "Open with" dialogs.
        void WriteApplicationKey(bool includeArchives)
        {
            std::wstring key = std::wstring(kClasses) + L"Applications\\" + kExeName;
            SetString(key, L"FriendlyAppName", kAppName);
            SetString(key + L"\\shell\\open\\command", nullptr, OpenCommand());
            RegDeleteTreeW(HKEY_CURRENT_USER, (key + L"\\SupportedTypes").c_str());
            for (auto type : kComicTypes) RegSetKeyValueW(HKEY_CURRENT_USER, (key + L"\\SupportedTypes").c_str(), type, REG_SZ, L"", sizeof(wchar_t));
            if (includeArchives)
            {
                for (auto type : kArchiveTypes) RegSetKeyValueW(HKEY_CURRENT_USER, (key + L"\\SupportedTypes").c_str(), type, REG_SZ, L"", sizeof(wchar_t));
            }
        }

        // RegisteredApplications + Capabilities: makes the app show up in Settings > Default apps.
        void WriteCapabilities(bool comics, bool archives)
        {
            SetString(kCapabilities, L"ApplicationName", kAppName);
            SetString(kCapabilities, L"ApplicationDescription", L"Fast comic book viewer for CBZ, CBR, CB7 and CBT files.");
            std::wstring assoc = std::wstring(kCapabilities) + L"\\FileAssociations";
            RegDeleteTreeW(HKEY_CURRENT_USER, assoc.c_str());
            if (comics) for (auto type : kComicTypes) SetString(assoc, type, kComicProgId);
            if (archives) for (auto type : kArchiveTypes) SetString(assoc, type, kArchiveProgId);
            SetString(L"Software\\RegisteredApplications", kAppName, kCapabilities);
        }

        bool ComicsRegistered() { return KeyExists(std::wstring(kClasses) + kComicProgId); }

        void NotifyShell() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); }
    }

    void RegisterAsComicViewer()
    {
        WriteProgId(kComicProgId, L"Comic book");
        for (auto type : kComicTypes)
        {
            // Point the type at us, remembering what it pointed to so removal can restore it.
            std::wstring typeKey = std::wstring(kClasses) + type;
            auto previous = GetString(typeKey, nullptr);
            if (previous && *previous != kComicProgId && !GetString(kPreviousDefaults, type))
            {
                SetString(kPreviousDefaults, type, *previous);
            }
            SetString(typeKey, nullptr, kComicProgId);
            AddOpenWith(type, kComicProgId);
        }
        bool archives = IsInOpenWithForArchives();
        WriteApplicationKey(archives);
        WriteCapabilities(true, archives);
        NotifyShell();
    }

    bool IsDefaultComicViewer()
    {
        // What Explorer would actually launch, honouring the user's protected choice.
        std::wstring exe = ExePath();
        for (auto type : { L".cbz", L".cbr" })
        {
            wchar_t found[MAX_PATH * 2]{};
            DWORD size = ARRAYSIZE(found);
            if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, type, L"open", found, &size))) return false;
            if (CompareStringOrdinal(found, -1, exe.c_str(), -1, TRUE) != CSTR_EQUAL) return false;
        }
        return true;
    }

    void SetOpenWithForArchives(bool enabled)
    {
        if (enabled)
        {
            WriteProgId(kArchiveProgId, L"Archive");
            for (auto type : kArchiveTypes) AddOpenWith(type, kArchiveProgId);
        }
        else
        {
            for (auto type : kArchiveTypes) RemoveOpenWith(type, kArchiveProgId);
            RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kClasses) + kArchiveProgId).c_str());
        }
        bool comics = ComicsRegistered();
        if (comics || enabled)
        {
            WriteApplicationKey(enabled);
            WriteCapabilities(comics, enabled);
        }
        else
        {
            UnregisterAll(); // nothing left to offer
            return;
        }
        NotifyShell();
    }

    bool IsInOpenWithForArchives()
    {
        return KeyExists(std::wstring(kClasses) + kArchiveProgId);
    }

    void UnregisterAll()
    {
        for (auto type : kComicTypes)
        {
            std::wstring typeKey = std::wstring(kClasses) + type;
            RemoveOpenWith(type, kComicProgId);
            if (GetString(typeKey, nullptr) == std::wstring(kComicProgId))
            {
                // Restore the previous default, or clear ours.
                if (auto previous = GetString(kPreviousDefaults, type)) SetString(typeKey, nullptr, *previous);
                else RegDeleteKeyValueW(HKEY_CURRENT_USER, typeKey.c_str(), nullptr);
            }
            DeleteKeyIfEmpty(typeKey);
        }
        for (auto type : kArchiveTypes) RemoveOpenWith(type, kArchiveProgId);

        RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kClasses) + kComicProgId).c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kClasses) + kArchiveProgId).c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kClasses) + L"Applications\\" + kExeName).c_str());
        DeleteKeyIfEmpty(std::wstring(kClasses) + L"Applications");
        RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", kAppName);
        DeleteKeyIfEmpty(L"Software\\RegisteredApplications");
        RegDeleteTreeW(HKEY_CURRENT_USER, kAppKey);
        NotifyShell();
    }

    void RefreshIfRegistered()
    {
        bool comics = ComicsRegistered();
        bool archives = IsInOpenWithForArchives();
        if (!comics && !archives) return;

        std::wstring command = OpenCommand();
        auto current = GetString(std::wstring(kClasses) + (comics ? kComicProgId : kArchiveProgId) + L"\\shell\\open\\command", nullptr);
        if (current == command) return; // up to date: nothing to write

        if (comics) WriteProgId(kComicProgId, L"Comic book");
        if (archives) WriteProgId(kArchiveProgId, L"Archive");
        WriteApplicationKey(archives);
        NotifyShell();
    }

    void OpenDefaultAppsSettings()
    {
        // Windows 11 opens this app's own page; older builds fall back to Default apps.
        ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=ComicsDisplayExp", nullptr, nullptr, SW_SHOWNORMAL);
    }
}
