
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <dwmapi.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")

namespace
{
    constexpr UINT WM_APP_WORKFLOW_DONE = WM_APP + 10;

    constexpr int ID_CHOOSE = 1001;
    constexpr int ID_INSTALL = 1002;
    constexpr int ID_DROP_ZONE = 1003;

    constexpr int IDR_SIGN_SCRIPT = 101;
    constexpr int IDR_SIGNTOOL = 102;

    // Hidden application settings. They are not displayed in the UI.
    constexpr wchar_t kPublisher[] =
        L"CN=85434AF5-74BD-4E8F-90F0-13F9EA1270DE";
    constexpr wchar_t kPfxPassword[] =
        L"SupremeMapsDownloader2026!";

    constexpr COLORREF kBlue = RGB(0, 120, 212);
    constexpr COLORREF kBlueHover = RGB(18, 132, 226);
    constexpr COLORREF kBlueDark = RGB(0, 94, 170);
    constexpr COLORREF kBlueDisabled = RGB(45, 78, 106);

    constexpr COLORREF kBackground = RGB(14, 15, 17);
    constexpr COLORREF kPanel = RGB(23, 24, 28);
    constexpr COLORREF kPanelShadow = RGB(8, 9, 11);
    constexpr COLORREF kPanelBorder = RGB(54, 56, 64);
    constexpr COLORREF kPanelBorderHover = RGB(78, 81, 91);

    constexpr COLORREF kGrayButton = RGB(42, 44, 50);
    constexpr COLORREF kGrayButtonHover = RGB(53, 55, 63);
    constexpr COLORREF kGrayButtonPressed = RGB(34, 36, 41);
    constexpr COLORREF kGrayButtonDisabled = RGB(32, 33, 37);

    constexpr COLORREF kWhite = RGB(255, 255, 255);
    constexpr COLORREF kText = RGB(244, 244, 247);
    constexpr COLORREF kMutedText = RGB(151, 154, 164);

    HWND g_main{};
    HWND g_dropZone{};
    HWND g_chooseButton{};
    HWND g_installButton{};

    HFONT g_mainFont{};
    HFONT g_dropFont{};
    HFONT g_smallFont{};
    HBRUSH g_backgroundBrush{};

    std::wstring g_selectedPackage;
    std::wstring g_dropStatus = L"Drop your package here";
    bool g_busy = false;

    struct WorkflowResult
    {
        DWORD exitCode{};
        std::wstring output;
        std::wstring result;
        std::wstring certificatePath;
    };

    void BrowseForPackage();

    std::wstring Quote(const std::wstring& value)
    {
        std::wstring escaped = value;
        size_t position = 0;

        while ((position = escaped.find(L'"', position)) != std::wstring::npos)
        {
            escaped.insert(position, L"\\");
            position += 2;
        }

        return L"\"" + escaped + L"\"";
    }

    std::wstring ToLower(std::wstring value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](wchar_t character)
            {
                return static_cast<wchar_t>(towlower(character));
            });

        return value;
    }

    bool IsSupportedPackage(const std::wstring& path)
    {
        const std::wstring lower = ToLower(path);

        return lower.ends_with(L".msix") ||
               lower.ends_with(L".msixbundle") ||
               lower.ends_with(L".appx") ||
               lower.ends_with(L".appxbundle");
    }

    std::wstring GetFileNameOnly(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");

        if (slash == std::wstring::npos)
            return path;

        return path.substr(slash + 1);
    }

    bool FileExists(const std::wstring& path)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());

        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    std::wstring GetPowerShellPath()
    {
        wchar_t systemDirectory[MAX_PATH]{};

        const UINT length = GetSystemDirectoryW(
            systemDirectory,
            static_cast<UINT>(std::size(systemDirectory)));

        if (length == 0 || length >= std::size(systemDirectory))
            return L"powershell.exe";

        return std::wstring(systemDirectory) +
               L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    }

    std::wstring CreateTemporaryDirectory()
    {
        wchar_t temporaryPath[MAX_PATH]{};

        const DWORD length = GetTempPathW(
            static_cast<DWORD>(std::size(temporaryPath)),
            temporaryPath);

        if (length == 0 || length >= std::size(temporaryPath))
            return {};

        const std::wstring directory =
            std::wstring(temporaryPath) +
            L"MSIXOneFileInstaller-" +
            std::to_wstring(GetCurrentProcessId()) +
            L"-" +
            std::to_wstring(GetTickCount64());

        if (!CreateDirectoryW(directory.c_str(), nullptr))
            return {};

        return directory;
    }

    bool ExtractResourceToFile(int resourceId, const std::wstring& outputPath)
    {
        const HMODULE module = GetModuleHandleW(nullptr);

        const HRSRC resource = FindResourceW(
            module,
            MAKEINTRESOURCEW(resourceId),
            RT_RCDATA);

        if (!resource)
            return false;

        const HGLOBAL loadedResource = LoadResource(module, resource);

        if (!loadedResource)
            return false;

        const DWORD resourceSize = SizeofResource(module, resource);
        const void* resourceData = LockResource(loadedResource);

        if (!resourceData || resourceSize == 0)
            return false;

        const HANDLE file = CreateFileW(
            outputPath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return false;

        DWORD bytesWritten = 0;
        const BOOL written = WriteFile(
            file,
            resourceData,
            resourceSize,
            &bytesWritten,
            nullptr);

        CloseHandle(file);

        return written && bytesWritten == resourceSize;
    }

    void DeleteTemporaryFiles(
        const std::wstring& directory,
        const std::wstring& scriptPath,
        const std::wstring& signToolPath)
    {
        if (!scriptPath.empty())
            DeleteFileW(scriptPath.c_str());

        if (!signToolPath.empty())
            DeleteFileW(signToolPath.c_str());

        if (!directory.empty())
            RemoveDirectoryW(directory.c_str());
    }

    std::wstring ExtractValue(
        const std::wstring& output,
        const std::wstring& key)
    {
        const std::wstring marker = key + L"=";
        size_t position = output.rfind(marker);

        if (position == std::wstring::npos)
            return {};

        position += marker.size();

        const size_t end = output.find_first_of(L"\r\n", position);
        std::wstring value = output.substr(
            position,
            end == std::wstring::npos
                ? std::wstring::npos
                : end - position);

        while (!value.empty() && iswspace(value.back()))
            value.pop_back();

        while (!value.empty() && iswspace(value.front()))
            value.erase(value.begin());

        return value;
    }

    WorkflowResult RunProcessAndCapture(const std::wstring& commandLine)
    {
        WorkflowResult result{};

        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(securityAttributes);
        securityAttributes.bInheritHandle = TRUE;

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;

        if (!CreatePipe(
                &readPipe,
                &writePipe,
                &securityAttributes,
                0))
        {
            result.exitCode = GetLastError();
            result.output = L"Unable to create the output pipe.";
            return result;
        }

        SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        startupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startupInfo.wShowWindow = SW_HIDE;
        startupInfo.hStdOutput = writePipe;
        startupInfo.hStdError = writePipe;
        startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION processInfo{};

        std::vector<wchar_t> mutableCommand(
            commandLine.begin(),
            commandLine.end());

        mutableCommand.push_back(L'\0');

        const BOOL processCreated = CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo);

        CloseHandle(writePipe);

        if (!processCreated)
        {
            result.exitCode = GetLastError();
            result.output =
                L"PowerShell could not be started. Windows error: " +
                std::to_wstring(result.exitCode);

            CloseHandle(readPipe);
            return result;
        }

        std::string outputBytes;
        char buffer[4096];
        DWORD bytesRead = 0;

        while (ReadFile(
                   readPipe,
                   buffer,
                   sizeof(buffer),
                   &bytesRead,
                   nullptr) &&
               bytesRead > 0)
        {
            outputBytes.append(buffer, buffer + bytesRead);
        }

        WaitForSingleObject(processInfo.hProcess, INFINITE);
        GetExitCodeProcess(processInfo.hProcess, &result.exitCode);

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        CloseHandle(readPipe);

        if (!outputBytes.empty())
        {
            const int requiredCharacters = MultiByteToWideChar(
                CP_UTF8,
                0,
                outputBytes.data(),
                static_cast<int>(outputBytes.size()),
                nullptr,
                0);

            if (requiredCharacters > 0)
            {
                result.output.resize(
                    static_cast<size_t>(requiredCharacters));

                MultiByteToWideChar(
                    CP_UTF8,
                    0,
                    outputBytes.data(),
                    static_cast<int>(outputBytes.size()),
                    result.output.data(),
                    requiredCharacters);
            }
        }

        result.result = ExtractValue(result.output, L"RESULT");
        result.certificatePath = ExtractValue(result.output, L"CER");

        return result;
    }

    std::wstring CompactErrorOutput(const std::wstring& output)
    {
        constexpr size_t maximumCharacters = 2600;

        if (output.size() <= maximumCharacters)
            return output;

        return L"...\r\n" +
               output.substr(output.size() - maximumCharacters);
    }

    void RefreshDropZone()
    {
        InvalidateRect(g_dropZone, nullptr, TRUE);
        UpdateWindow(g_dropZone);
    }

    void SetSelectedPackage(const std::wstring& path)
    {
        if (!IsSupportedPackage(path))
        {
            MessageBoxW(
                g_main,
                L"Please select an MSIX, MSIXBundle, APPX or APPXBundle file.",
                L"Unsupported file",
                MB_OK | MB_ICONWARNING);

            return;
        }

        g_selectedPackage = path;
        g_dropStatus = L"Ready to install";

        EnableWindow(g_installButton, TRUE);
        RefreshDropZone();
    }

    void BrowseForPackage()
    {
        if (g_busy)
            return;

        IFileOpenDialog* dialog = nullptr;

        if (FAILED(CoCreateInstance(
                CLSID_FileOpenDialog,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&dialog))))
        {
            MessageBoxW(
                g_main,
                L"The file picker could not be opened.",
                L"Error",
                MB_OK | MB_ICONERROR);

            return;
        }

        const COMDLG_FILTERSPEC filters[] =
        {
            {
                L"Windows app packages",
                L"*.msix;*.msixbundle;*.appx;*.appxbundle"
            },
            {
                L"All files",
                L"*.*"
            }
        };

        dialog->SetFileTypes(
            static_cast<UINT>(std::size(filters)),
            filters);

        dialog->SetTitle(L"Choose a package file");

        if (SUCCEEDED(dialog->Show(g_main)))
        {
            IShellItem* item = nullptr;

            if (SUCCEEDED(dialog->GetResult(&item)))
            {
                PWSTR selectedPath = nullptr;

                if (SUCCEEDED(item->GetDisplayName(
                        SIGDN_FILESYSPATH,
                        &selectedPath)))
                {
                    SetSelectedPackage(selectedPath);
                    CoTaskMemFree(selectedPath);
                }

                item->Release();
            }
        }

        dialog->Release();
    }

    void StartWorkflow()
    {
        if (g_busy)
            return;

        if (g_selectedPackage.empty())
        {
            MessageBoxW(
                g_main,
                L"Choose or drop an MSIX package first.",
                L"No package selected",
                MB_OK | MB_ICONINFORMATION);

            return;
        }

        g_busy = true;
        g_dropStatus = L"Signing and installing...";
        EnableWindow(g_chooseButton, FALSE);
        EnableWindow(g_installButton, FALSE);
        SetWindowTextW(g_installButton, L"Installing...");
        RefreshDropZone();
        InvalidateRect(g_installButton, nullptr, TRUE);

        const std::wstring selectedPackage = g_selectedPackage;

        std::thread(
            [selectedPackage]()
            {
                WorkflowResult* workflowResult =
                    new WorkflowResult();

                const std::wstring temporaryDirectory =
                    CreateTemporaryDirectory();

                const std::wstring scriptPath =
                    temporaryDirectory.empty()
                        ? L""
                        : temporaryDirectory + L"\\SignInstall.ps1";

                const std::wstring signToolPath =
                    temporaryDirectory.empty()
                        ? L""
                        : temporaryDirectory + L"\\signtool.exe";

                if (temporaryDirectory.empty())
                {
                    workflowResult->exitCode = ERROR_PATH_NOT_FOUND;
                    workflowResult->output =
                        L"Unable to create the temporary working directory.";
                }
                else if (!ExtractResourceToFile(
                             IDR_SIGN_SCRIPT,
                             scriptPath))
                {
                    workflowResult->exitCode = ERROR_RESOURCE_DATA_NOT_FOUND;
                    workflowResult->output =
                        L"Unable to extract the embedded signing script.";
                }
                else if (!ExtractResourceToFile(
                             IDR_SIGNTOOL,
                             signToolPath))
                {
                    workflowResult->exitCode = ERROR_RESOURCE_DATA_NOT_FOUND;
                    workflowResult->output =
                        L"Unable to extract the embedded signing tool.";
                }
                else
                {
                    const std::wstring command =
                        Quote(GetPowerShellPath()) +
                        L" -NoLogo -NoProfile -NonInteractive"
                        L" -ExecutionPolicy Bypass -File " +
                        Quote(scriptPath) +
                        L" -Msix " +
                        Quote(selectedPackage) +
                        L" -Publisher " +
                        Quote(kPublisher) +
                        L" -Password " +
                        Quote(kPfxPassword) +
                        L" -SignTool " +
                        Quote(signToolPath);

                    *workflowResult =
                        RunProcessAndCapture(command);
                }

                DeleteTemporaryFiles(
                    temporaryDirectory,
                    scriptPath,
                    signToolPath);

                PostMessageW(
                    g_main,
                    WM_APP_WORKFLOW_DONE,
                    0,
                    reinterpret_cast<LPARAM>(workflowResult));
            })
            .detach();
    }

    void DrawModernButton(
        const DRAWITEMSTRUCT* drawItem,
        bool isInstallButton)
    {
        RECT rectangle = drawItem->rcItem;

        const bool disabled =
            (drawItem->itemState & ODS_DISABLED) != 0;

        const bool pressed =
            (drawItem->itemState & ODS_SELECTED) != 0;

        const bool hot =
            (drawItem->itemState & ODS_HOTLIGHT) != 0;

        COLORREF fillColor{};
        COLORREF borderColor{};
        COLORREF textColor{};

        // Both actions use the same blue color states.
        fillColor = disabled
            ? kBlueDisabled
            : (pressed
                ? kBlueDark
                : (hot ? kBlueHover : kBlue));

        borderColor = fillColor;
        textColor = kWhite;

        HBRUSH brush = CreateSolidBrush(fillColor);
        HPEN pen = CreatePen(
            PS_SOLID,
            1,
            borderColor);

        HGDIOBJ oldBrush =
            SelectObject(drawItem->hDC, brush);

        HGDIOBJ oldPen =
            SelectObject(drawItem->hDC, pen);

        if (isInstallButton)
        {
            // Deliberately square: the primary action remains visually strong.
            Rectangle(
                drawItem->hDC,
                rectangle.left,
                rectangle.top,
                rectangle.right,
                rectangle.bottom);
        }
        else
        {
            // Rounded centered file-selection action.
            RoundRect(
                drawItem->hDC,
                rectangle.left,
                rectangle.top,
                rectangle.right,
                rectangle.bottom,
                14,
                14);
        }

        SelectObject(drawItem->hDC, oldBrush);
        SelectObject(drawItem->hDC, oldPen);

        DeleteObject(brush);
        DeleteObject(pen);

        wchar_t buttonText[128]{};

        GetWindowTextW(
            drawItem->hwndItem,
            buttonText,
            static_cast<int>(std::size(buttonText)));

        SetBkMode(drawItem->hDC, TRANSPARENT);
        SetTextColor(drawItem->hDC, textColor);

        HGDIOBJ oldFont =
            SelectObject(drawItem->hDC, g_mainFont);

        DrawTextW(
            drawItem->hDC,
            buttonText,
            -1,
            &rectangle,
            DT_CENTER |
                DT_VCENTER |
                DT_SINGLELINE |
                DT_NOPREFIX);

        SelectObject(drawItem->hDC, oldFont);

        if ((drawItem->itemState & ODS_FOCUS) != 0)
        {
            RECT focusRectangle = rectangle;
            InflateRect(&focusRectangle, -5, -5);
            DrawFocusRect(drawItem->hDC, &focusRectangle);
        }
    }

    void LayoutControls(HWND window)
    {
        RECT clientRectangle{};
        GetClientRect(window, &clientRectangle);

        const int width =
            clientRectangle.right - clientRectangle.left;

        const int height =
            clientRectangle.bottom - clientRectangle.top;

        constexpr int margin = 20;
        constexpr int gap = 16;
        constexpr int installHeight = 52;

        const int dropHeight =
            height - (margin * 2) - gap - installHeight;

        MoveWindow(
            g_dropZone,
            margin,
            margin,
            width - (margin * 2),
            dropHeight,
            TRUE);

        RECT dropRectangle{};
        GetClientRect(g_dropZone, &dropRectangle);

        constexpr int chooseWidth = 184;
        constexpr int chooseHeight = 46;

        const int chooseX =
            margin +
            ((dropRectangle.right - dropRectangle.left - chooseWidth) / 2);

        const int chooseY =
            margin +
            ((dropRectangle.bottom - dropRectangle.top - chooseHeight) / 2);

        SetWindowPos(
            g_chooseButton,
            HWND_TOP,
            chooseX,
            chooseY,
            chooseWidth,
            chooseHeight,
            SWP_SHOWWINDOW);

        MoveWindow(
            g_installButton,
            margin,
            margin + dropHeight + gap,
            width - (margin * 2),
            installHeight,
            TRUE);
    }

    LRESULT CALLBACK DropZoneWindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        switch (message)
        {
        case WM_LBUTTONUP:
            BrowseForPackage();
            return 0;

        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT paintStructure{};
            HDC deviceContext =
                BeginPaint(window, &paintStructure);

            RECT clientRectangle{};
            GetClientRect(window, &clientRectangle);

            // Soft shadow behind the drop surface.
            HBRUSH shadowBrush =
                CreateSolidBrush(kPanelShadow);

            HPEN shadowPen =
                CreatePen(PS_SOLID, 1, kPanelShadow);

            HGDIOBJ oldShadowBrush =
                SelectObject(deviceContext, shadowBrush);

            HGDIOBJ oldShadowPen =
                SelectObject(deviceContext, shadowPen);

            RoundRect(
                deviceContext,
                4,
                5,
                clientRectangle.right - 1,
                clientRectangle.bottom - 1,
                22,
                22);

            SelectObject(deviceContext, oldShadowBrush);
            SelectObject(deviceContext, oldShadowPen);

            DeleteObject(shadowBrush);
            DeleteObject(shadowPen);

            HBRUSH panelBrush =
                CreateSolidBrush(kPanel);

            HPEN borderPen =
                CreatePen(PS_SOLID, 1, kPanelBorder);

            HGDIOBJ oldPanelBrush =
                SelectObject(deviceContext, panelBrush);

            HGDIOBJ oldBorderPen =
                SelectObject(deviceContext, borderPen);

            RoundRect(
                deviceContext,
                1,
                1,
                clientRectangle.right - 4,
                clientRectangle.bottom - 4,
                22,
                22);

            SelectObject(deviceContext, oldPanelBrush);
            SelectObject(deviceContext, oldBorderPen);

            DeleteObject(panelBrush);
            DeleteObject(borderPen);

            SetBkMode(deviceContext, TRANSPARENT);

            RECT titleRectangle
            {
                28,
                34,
                clientRectangle.right - 28,
                78
            };

            SetTextColor(deviceContext, kText);

            HGDIOBJ oldFont =
                SelectObject(deviceContext, g_dropFont);

            DrawTextW(
                deviceContext,
                g_dropStatus.c_str(),
                -1,
                &titleRectangle,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                    DT_END_ELLIPSIS);

            SelectObject(deviceContext, g_smallFont);
            SetTextColor(deviceContext, kMutedText);

            const int centeredButtonBottom =
                (clientRectangle.bottom + 46) / 2;

            RECT detailRectangle
            {
                34,
                centeredButtonBottom + 24,
                clientRectangle.right - 34,
                clientRectangle.bottom - 24
            };

            std::wstring detailText;

            if (g_selectedPackage.empty())
            {
                detailText =
                    L"MSIX, MSIXBundle, APPX or APPXBundle";
            }
            else
            {
                detailText =
                    GetFileNameOnly(g_selectedPackage);
            }

            DrawTextW(
                deviceContext,
                detailText.c_str(),
                -1,
                &detailRectangle,
                DT_CENTER | DT_TOP | DT_SINGLELINE |
                    DT_END_ELLIPSIS | DT_NOPREFIX);

            SelectObject(deviceContext, oldFont);

            EndPaint(window, &paintStructure);
            return 0;
        }
        }

        return DefWindowProcW(
            window,
            message,
            wParam,
            lParam);
    }

    LRESULT CALLBACK MainWindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
        {
            g_main = window;
            DragAcceptFiles(window, TRUE);

            g_backgroundBrush =
                CreateSolidBrush(kBackground);

            g_mainFont = CreateFontW(
                -17,
                0,
                0,
                0,
                FW_SEMIBOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI Variable Text");

            g_dropFont = CreateFontW(
                -24,
                0,
                0,
                0,
                FW_SEMIBOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI Variable Display");

            g_smallFont = CreateFontW(
                -15,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI Variable Text");

            g_dropZone = CreateWindowExW(
                0,
                L"MSIXOneFileDropZone",
                L"",
                WS_CHILD | WS_VISIBLE,
                0,
                0,
                0,
                0,
                window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_DROP_ZONE)),
                nullptr,
                nullptr);

            g_chooseButton = CreateWindowW(
                L"BUTTON",
                L"Choose File",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0,
                0,
                0,
                0,
                window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_CHOOSE)),
                nullptr,
                nullptr);

            g_installButton = CreateWindowW(
                L"BUTTON",
                L"Install",
                WS_CHILD | WS_VISIBLE |
                    WS_DISABLED | BS_OWNERDRAW,
                0,
                0,
                0,
                0,
                window,
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(ID_INSTALL)),
                nullptr,
                nullptr);

            SendMessageW(
                g_chooseButton,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(g_mainFont),
                TRUE);

            SendMessageW(
                g_installButton,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(g_mainFont),
                TRUE);

            const BOOL useImmersiveDarkMode = TRUE;
            const DWORD borderColor = kPanelBorder;
            const DWORD captionColor = kBackground;
            const DWORD captionTextColor = kWhite;

            DwmSetWindowAttribute(
                window,
                static_cast<DWMWINDOWATTRIBUTE>(20),
                &useImmersiveDarkMode,
                sizeof(useImmersiveDarkMode));

            const DWORD windowCornerPreference = 2;

            DwmSetWindowAttribute(
                window,
                static_cast<DWMWINDOWATTRIBUTE>(33),
                &windowCornerPreference,
                sizeof(windowCornerPreference));

            DwmSetWindowAttribute(
                window,
                static_cast<DWMWINDOWATTRIBUTE>(34),
                &borderColor,
                sizeof(borderColor));

            DwmSetWindowAttribute(
                window,
                static_cast<DWMWINDOWATTRIBUTE>(35),
                &captionColor,
                sizeof(captionColor));

            DwmSetWindowAttribute(
                window,
                static_cast<DWMWINDOWATTRIBUTE>(36),
                &captionTextColor,
                sizeof(captionTextColor));

            LayoutControls(window);
            return 0;
        }

        case WM_SIZE:
            LayoutControls(window);
            return 0;

        case WM_ERASEBKGND:
        {
            RECT clientRectangle{};
            GetClientRect(window, &clientRectangle);

            FillRect(
                reinterpret_cast<HDC>(wParam),
                &clientRectangle,
                g_backgroundBrush);

            return 1;
        }

        case WM_DROPFILES:
        {
            HDROP dropHandle =
                reinterpret_cast<HDROP>(wParam);

            wchar_t path[32768]{};

            if (DragQueryFileW(
                    dropHandle,
                    0,
                    path,
                    static_cast<UINT>(std::size(path))) > 0)
            {
                SetSelectedPackage(path);
            }

            DragFinish(dropHandle);
            return 0;
        }

        case WM_DRAWITEM:
        {
            const DRAWITEMSTRUCT* drawItem =
                reinterpret_cast<DRAWITEMSTRUCT*>(lParam);

            if (drawItem->CtlID == ID_CHOOSE)
            {
                DrawModernButton(drawItem, false);
                return TRUE;
            }

            if (drawItem->CtlID == ID_INSTALL)
            {
                DrawModernButton(drawItem, true);
                return TRUE;
            }

            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case ID_CHOOSE:
                BrowseForPackage();
                return 0;

            case ID_INSTALL:
                StartWorkflow();
                return 0;
            }
            break;

        case WM_APP_WORKFLOW_DONE:
        {
            WorkflowResult* workflowResult =
                reinterpret_cast<WorkflowResult*>(lParam);

            g_busy = false;
            EnableWindow(g_chooseButton, TRUE);
            SetWindowTextW(g_installButton, L"Install");

            if (!g_selectedPackage.empty())
                EnableWindow(g_installButton, TRUE);

            InvalidateRect(g_installButton, nullptr, TRUE);

            if (workflowResult->result == L"INSTALLED" &&
                workflowResult->exitCode == 0)
            {
                g_dropStatus = L"Installed successfully";
                RefreshDropZone();

                MessageBoxW(
                    window,
                    L"The package was signed and installed successfully.",
                    L"Installation complete",
                    MB_OK | MB_ICONINFORMATION);
            }
            else if (workflowResult->result == L"NEEDS_TRUST")
            {
                g_dropStatus = L"Certificate approval required";
                RefreshDropZone();

                MessageBoxW(
                    window,
                    L"The package was signed successfully.\n\n"
                    L"Windows must trust the test certificate once:\n\n"
                    L"1. Choose Install Certificate\n"
                    L"2. Choose Local Machine\n"
                    L"3. Select Place all certificates in the following store\n"
                    L"4. Choose Trusted People\n"
                    L"5. Finish the wizard\n\n"
                    L"Then click Install again.",
                    L"Trust the certificate",
                    MB_OK | MB_ICONINFORMATION);

                if (!workflowResult->certificatePath.empty() &&
                    FileExists(workflowResult->certificatePath))
                {
                    ShellExecuteW(
                        window,
                        L"open",
                        workflowResult->certificatePath.c_str(),
                        nullptr,
                        nullptr,
                        SW_SHOWNORMAL);
                }
            }
            else
            {
                g_dropStatus = L"Installation failed";
                RefreshDropZone();

                const std::wstring errorMessage =
                    L"The package could not be installed.\n\n"
                    L"Exit code: " +
                    std::to_wstring(workflowResult->exitCode) +
                    L"\n\n" +
                    CompactErrorOutput(workflowResult->output);

                MessageBoxW(
                    window,
                    errorMessage.c_str(),
                    L"Installation failed",
                    MB_OK | MB_ICONERROR);
            }

            delete workflowResult;
            return 0;
        }

        case WM_DESTROY:
            if (g_backgroundBrush)
                DeleteObject(g_backgroundBrush);

            if (g_mainFont)
                DeleteObject(g_mainFont);

            if (g_dropFont)
                DeleteObject(g_dropFont);

            if (g_smallFont)
                DeleteObject(g_smallFont);

            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(
            window,
            message,
            wParam,
            lParam);
    }
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX commonControls
    {
        sizeof(commonControls),
        ICC_STANDARD_CLASSES
    };

    InitCommonControlsEx(&commonControls);

    if (FAILED(CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED)))
    {
        return 1;
    }

    WNDCLASSEXW dropZoneClass{};
    dropZoneClass.cbSize = sizeof(dropZoneClass);
    dropZoneClass.hInstance = instance;
    dropZoneClass.lpfnWndProc = DropZoneWindowProcedure;
    dropZoneClass.lpszClassName = L"MSIXOneFileDropZone";
    dropZoneClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    dropZoneClass.hbrBackground = nullptr;

    if (!RegisterClassExW(&dropZoneClass))
    {
        CoUninitialize();
        return 2;
    }

    WNDCLASSEXW mainWindowClass{};
    mainWindowClass.cbSize = sizeof(mainWindowClass);
    mainWindowClass.hInstance = instance;
    mainWindowClass.lpfnWndProc = MainWindowProcedure;
    mainWindowClass.lpszClassName = L"MSIXOneFileInstallerWindow";
    mainWindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainWindowClass.hIcon =
        LoadIconW(instance, MAKEINTRESOURCEW(1));
    mainWindowClass.hIconSm = mainWindowClass.hIcon;
    mainWindowClass.hbrBackground = nullptr;

    if (!RegisterClassExW(&mainWindowClass))
    {
        CoUninitialize();
        return 3;
    }

    constexpr int clientWidth = 520;
    constexpr int clientHeight = 390;

    RECT windowRectangle
    {
        0,
        0,
        clientWidth,
        clientHeight
    };

    const DWORD windowStyle =
        WS_OVERLAPPED |
        WS_CAPTION |
        WS_SYSMENU |
        WS_MINIMIZEBOX;

    AdjustWindowRect(
        &windowRectangle,
        windowStyle,
        FALSE);

    const int windowWidth =
        windowRectangle.right - windowRectangle.left;

    const int windowHeight =
        windowRectangle.bottom - windowRectangle.top;

    const int x =
        (GetSystemMetrics(SM_CXSCREEN) - windowWidth) / 2;

    const int y =
        (GetSystemMetrics(SM_CYSCREEN) - windowHeight) / 2;

    HWND window = CreateWindowExW(
        0,
        mainWindowClass.lpszClassName,
        L"MSIX Installer",
        windowStyle,
        x,
        y,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!window)
    {
        CoUninitialize();
        return 4;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};

    while (GetMessageW(
               &message,
               nullptr,
               0,
               0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CoUninitialize();
    return static_cast<int>(message.wParam);
}
