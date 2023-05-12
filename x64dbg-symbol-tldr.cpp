#include "pch.h"

#include "resource.h"

#define PLUGIN_NAME "Symbol tl;dr"
#define PLUGIN_VERSION 101
#define PLUGIN_VERSION_STR "1.0.1"

#ifndef DLL_EXPORT
#define DLL_EXPORT __declspec(dllexport)
#endif

namespace {

constexpr size_t kCollapsedSymbolDesiredLength = 80;

enum {
    MENU_ABOUT = 1,
};

HINSTANCE g_hDllInst;
int g_pluginHandle;

class SymbolInfoWrapper {
   public:
    SymbolInfoWrapper() = default;
    ~SymbolInfoWrapper() { free(); }

    SymbolInfoWrapper(const SymbolInfoWrapper&) = delete;
    SymbolInfoWrapper& operator=(const SymbolInfoWrapper&) = delete;

    SYMBOLINFO* put() {
        free();
        memset(&info, 0, sizeof(info));
        return &info;
    }

    const SYMBOLINFO* get() const { return &info; }
    const SYMBOLINFO* operator->() const { return &info; }

   private:
    void free() {
        if (info.freeDecorated) {
            BridgeFree(info.decoratedSymbol);
        }
        if (info.freeUndecorated) {
            BridgeFree(info.undecoratedSymbol);
        }
    }

    SYMBOLINFO info{};
};

std::string CollapseLabelTemplates(std::string_view label) {
    static const boost::regex collapsePattern(R"(<(<\.\.\.>|[^<>])*>)");
    std::string result;
    boost::regex_replace(std::back_inserter(result), label.begin(), label.end(),
                         collapsePattern, "<...>");

    return result != label ? result : std::string();
}

std::string CollapseLabelNamespaces(std::string_view label) {
    static const boost::regex collapsePattern(
        R"(([a-zA-Z_](<\.\.\.>|[a-zA-Z0-9_])*::)+)");
    std::string result;
    boost::regex_replace(std::back_inserter(result), label.begin(), label.end(),
                         collapsePattern, "[...]::");

    return result != label ? result : std::string();
}

void AddInfoLineForCollapsedLabel(std::string label) {
    if (label.size() <= kCollapsedSymbolDesiredLength) {
        return;
    }

    GuiAddInfoLine("-- collapsed symbol --");

    while (true) {
        std::string labelCollapsed = CollapseLabelTemplates(label);
        if (labelCollapsed.empty()) {
            break;
        }

        label = std::move(labelCollapsed);
        if (label.size() <= kCollapsedSymbolDesiredLength) {
            GuiAddInfoLine(label.c_str());
            return;
        }
    }

    if (std::string labelCollapsed = CollapseLabelNamespaces(label);
        !labelCollapsed.empty()) {
        label = std::move(labelCollapsed);
        if (label.size() <= kCollapsedSymbolDesiredLength) {
            GuiAddInfoLine(label.c_str());
            return;
        }
    }

    GuiAddInfoLine(label.c_str());
}

void AddInfoLinesForWordWrappedLabel(std::string_view label) {
    GuiAddInfoLine("-- formatted symbol --");

    // Matches delimiters or consecutive non-delimiters.
    // https://stackoverflow.com/a/27706957
    // Exception examples: fn(unsigned int *), T<1337>.
    static const boost::regex delimiters(
        R"((\([a-zA-Z0-9_*& ]+\)|<[a-zA-Z0-9_*& ]+>|[,<>()]|[^,<>()]+))");

    boost::regex_iterator<std::string_view::iterator> rit(
        label.begin(), label.end(), delimiters);
    boost::regex_iterator<std::string_view::iterator> rend;

    std::string line;
    bool lineReady = false;
    size_t indentLevel = 0;

    static const boost::regex qualifiers(R"( *((const|volatile|[*&]) *)+)");
    static const boost::regex methodModifiers(R"( *const *)");

    for (; rit != rend; ++rit) {
        auto s = rit->str();

        if (lineReady) {
            if (s == ")" || s == ">") {
                line += s;
                if (indentLevel > 0) {
                    indentLevel--;
                }
                continue;
            }

            // The qualifiers check is to keep "x<y> const &" together. The
            // methodModifiers check is to keep "method(...) const" together.
            if (s == "," ||
                (line.back() == '>' && boost::regex_match(s, qualifiers)) ||
                (line.back() == ')' &&
                 boost::regex_match(s, methodModifiers))) {
                line += s;
                continue;
            }

            GuiAddInfoLine(line.c_str());
            line.clear();
            lineReady = false;
        }

        if (line.empty()) {
            auto firstNonSpace = s.find_first_not_of(' ');
            if (firstNonSpace == s.npos) {
                continue;
            }

            if (indentLevel > 0) {
                line.assign(indentLevel * 2, ' ');
            }

            line += s.substr(firstNonSpace);
        } else {
            line += s;
        }

        if (s == ",") {
            lineReady = true;
            continue;
        }

        if (s == ")" || s == ">") {
            lineReady = true;
            if (indentLevel > 0) {
                indentLevel--;
            }
            continue;
        }

        if (s == "(" || s == "<") {
            lineReady = true;
            indentLevel++;
            continue;
        }
    }

    if (!line.empty()) {
        GuiAddInfoLine(line.c_str());
    }
}

void OpenUrl(HWND hWnd, PCWSTR url) {
    if ((INT_PTR)ShellExecute(hWnd, L"open", url, nullptr, nullptr,
                              SW_SHOWNORMAL) <= 32) {
        MessageBox(hWnd, L"Failed to open link", nullptr, MB_ICONHAND);
    }
}

void About(HWND hWnd) {
    PCWSTR content =
        TEXT(PLUGIN_NAME) L" v" TEXT(PLUGIN_VERSION_STR) L"\n"
        L"By m417z\n"
        L"\n"
        L"Source code:\n"
        L"<A HREF=\"https://github.com/m417z/x64dbg-symbol-tldr\">https://github.com/m417z/x64dbg-symbol-tldr</a>";

    TASKDIALOGCONFIG taskDialogConfig{
        .cbSize = sizeof(taskDialogConfig),
        .hwndParent = hWnd,
        .hInstance = g_hDllInst,
        .dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION,
        .pszWindowTitle = L"About",
        .pszMainIcon = TD_INFORMATION_ICON,
        .pszContent = content,
        .pfCallback = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                         LONG_PTR lpRefData) -> HRESULT {
            switch (msg) {
                case TDN_HYPERLINK_CLICKED:
                    OpenUrl(hwnd, (PCWSTR)lParam);
                    break;
            }

            return S_OK;
        },
    };

    TaskDialogIndirect(&taskDialogConfig, nullptr, nullptr, nullptr);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hDllInst = hModule;
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }

    return TRUE;
}

extern "C" DLL_EXPORT bool pluginit(PLUG_INITSTRUCT* initStruct) {
    initStruct->pluginVersion = PLUGIN_VERSION;
    initStruct->sdkVersion = PLUG_SDKVERSION;
    strcpy_s(initStruct->pluginName, PLUGIN_NAME);
    g_pluginHandle = initStruct->pluginHandle;

    _plugin_logputs(PLUGIN_NAME " v" PLUGIN_VERSION_STR);
    _plugin_logputs("  By m417z");

    return true;
}

extern "C" DLL_EXPORT void plugsetup(PLUG_SETUPSTRUCT* setupStruct) {
    int hMenu = setupStruct->hMenu;

    HRSRC resource =
        FindResource(g_hDllInst, MAKEINTRESOURCE(IDB_ICON), L"PNG");
    if (resource) {
        HGLOBAL memory = LoadResource(g_hDllInst, resource);
        if (memory) {
            PVOID data = LockResource(memory);
            if (data) {
                DWORD size = SizeofResource(g_hDllInst, resource);
                ICONDATA iconData{
                    .data = data,
                    .size = size,
                };

                _plugin_menuseticon(hMenu, &iconData);
            }
        }
    }

    _plugin_menuaddentry(hMenu, MENU_ABOUT, "&About");
}

extern "C" DLL_EXPORT void CBSELCHANGED(CBTYPE, PLUG_CB_SELCHANGED* selInfo) {
    SymbolInfoWrapper info;
    if (!DbgGetSymbolInfoAt(selInfo->VA, info.put()) ||
        !*info->undecoratedSymbol) {
        duint start;
        if (!DbgFunctionGet(selInfo->VA, &start, nullptr) ||
            !DbgGetSymbolInfoAt(start, info.put()) ||
            !*info->undecoratedSymbol) {
            return;
        }
    }

    std::string label = info->undecoratedSymbol;
    AddInfoLineForCollapsedLabel(label);
    AddInfoLinesForWordWrappedLabel(label);
}

extern "C" DLL_EXPORT void CBMENUENTRY(CBTYPE, PLUG_CB_MENUENTRY* info) {
    switch (info->hEntry) {
        case MENU_ABOUT:
            About(GetActiveWindow());
            break;
    }
}
