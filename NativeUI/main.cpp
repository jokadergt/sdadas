#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <thread>
#include <stdexcept>
#include <memory>
#include "vendor/json.hpp"

using json = nlohmann::json;
static HWND windowHandle, nav, panel, heading, status, saveButton, installButton;
static HINSTANCE instance;
static HFONT font, titleFont;
static HANDLE privateFont = nullptr;
static HBRUSH background, cardBrush;
static COLORREF canvasColor = RGB(32,32,32), inkColor = RGB(240,240,240), cardColor = RGB(48,48,48), selectionColor = RGB(52,52,52);
static bool darkTheme = true;
static HANDLE engineInput = nullptr, engineProcess = nullptr;
static json snapshot = {{"installed", false}, {"dirty", false}, {"pages", json::array()}};
static WPARAM generation = 0;
static int pageIndex = 0, requestedPage = 0, scrollPosition = 0, contentHeight = 0;
static bool busy = true, rebuilding = false, bridgeAlive = false, engineMissing = false;
static std::wstring enginePath, appDir;
static constexpr UINT EngineReplyMessage = WM_APP + 1;
static constexpr UINT RestartMessage = WM_APP + 2;
struct Field { json data; HWND label = nullptr, control = nullptr, button = nullptr; int y = 0; };
static std::vector<Field> fields;

static std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
    if (!size) throw std::runtime_error("Texto UTF-8 inválido");
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), size);
    return result;
}
static std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), size, nullptr, nullptr);
    return result;
}
static std::wstring text(HWND h) {
    std::wstring result(GetWindowTextLengthW(h) + 1, 0);
    GetWindowTextW(h, result.data(), (int)result.size());
    result.resize(wcslen(result.c_str())); return result;
}
static void message(const std::string& value) { MessageBoxW(windowHandle, wide(value).c_str(), L"cayb trap", MB_OK | MB_ICONERROR); }
static HWND control(HWND parent, const wchar_t* klass, const std::wstring& value, DWORD style, int id = 0) {
    HWND h = CreateWindowExW(0, klass, value.c_str(), WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, instance, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    SetWindowTheme(h, darkTheme ? L"DarkMode_Explorer" : L"Explorer", nullptr); return h;
}
static void setBusy(bool value) {
    busy = value;
    EnableWindow(nav, !value); EnableWindow(panel, !value);
    EnableWindow(saveButton, !value && snapshot.value("installed", false));
    EnableWindow(installButton, !value || engineMissing);
}
static std::wstring quote(const std::wstring& value) {
    // CommandLineToArgvW-compatible escaping, including paths ending in backslashes.
    std::wstring result = L"\""; size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\'); return result + L'"';
}

static void send(json request) {
    if (!bridgeAlive || !engineInput) { message("Motor indisponível. Use Instalar / reconectar."); return; }
    std::string wire = request.dump() + "\n";
    setBusy(true);
    size_t offset = 0;
    while (offset < wire.size()) {
        DWORD written = 0;
        if (!WriteFile(engineInput, wire.data() + offset, (DWORD)(wire.size() - offset), &written, nullptr) || !written) {
            setBusy(false); message("A conexão com o motor foi encerrada."); return;
        }
        offset += written;
    }
}
static json changes() {
    json result = json::array();
    if (!snapshot.contains("pages") || snapshot["pages"].empty()) return result;
    std::string page = snapshot["pages"][pageIndex]["id"];
    for (const auto& f : fields) {
        std::string kind = f.data["kind"];
        if (kind == "command") continue;
        json value;
        if (kind == "bool") value = SendMessageW(f.control, BM_GETCHECK, 0, 0) == BST_CHECKED;
        else if (kind == "json") value = f.data["editedValue"];
        else value = utf8(text(f.control));
        if (value != f.data["value"])
            result.push_back({{"page", page}, {"path", f.data["path"]}, {"value", value}});
    }
    return result;
}

static void layoutFields() {
    RECT area{}; GetClientRect(panel, &area);
    int width = area.right - 26;
    for (auto& f : fields) {
        int y = f.y - scrollPosition;
        std::string kind = f.data["kind"];
        if (kind == "command") { MoveWindow(f.button, 12, y, width - 24, 38, TRUE); continue; }
        if (kind == "bool") {
            MoveWindow(f.control, 12, y, width - 24, 52, TRUE);
        } else {
            MoveWindow(f.label, 12, y, width - 24, 28, TRUE);
            if (kind == "json") MoveWindow(f.button, 12, y + 30, width - 24, 32, TRUE);
            else MoveWindow(f.control, 12, y + 30, width - 24, kind == "enum" ? 240 : 30, TRUE);
        }
    }
    SCROLLINFO si{sizeof(si)}; si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMax = contentHeight; si.nPage = area.bottom; si.nPos = scrollPosition;
    SetScrollInfo(panel, SB_VERT, &si, TRUE);
    InvalidateRect(panel, nullptr, TRUE);
}
static void renderPage(bool resetScroll = true) {
    rebuilding = true;
    for (auto& f : fields) { if (f.label) DestroyWindow(f.label); if (f.control) DestroyWindow(f.control); if (f.button) DestroyWindow(f.button); }
    fields.clear();
    if (resetScroll) scrollPosition = 0;
    if (!snapshot.value("installed", false) || snapshot["pages"].empty()) {
        SetWindowTextW(heading, L"Instale o motor original para continuar"); rebuilding = false; return;
    }
    auto page = snapshot["pages"][pageIndex];
    SetWindowTextW(heading, wide(page["title"].get<std::string>()).c_str());
    int y = 8, index = 0;
    for (auto data : page["fields"]) {
        // The editor below replaces the old WPF editor navigation command.
        if (data["path"] == "OpenFastFlagEditorCommand") continue;
        Field f; f.data = data; f.y = y;
        auto label = wide(data["label"].get<std::string>());
        std::string kind = data["kind"];
        if (kind == "command") f.button = control(panel, L"BUTTON", label, WS_TABSTOP | BS_PUSHBUTTON, 1000 + index);
        else if (kind == "bool") {
            f.control = control(panel, L"BUTTON", label, WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE, 2000 + index);
            SendMessageW(f.control, BM_SETCHECK, data["value"].get<bool>() ? BST_CHECKED : BST_UNCHECKED, 0);
        } else {
            f.label = control(panel, L"STATIC", label, SS_LEFT);
            if (kind == "json") {
                f.data["editedValue"] = data["value"];
                f.button = control(panel, L"BUTTON", L"Editar JSON…", WS_TABSTOP | BS_PUSHBUTTON, 3000 + index);
            } else if (kind == "enum") {
                f.control = control(panel, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 2000 + index);
                int selected = -1, n = 0;
                for (const auto& choice : data["choices"]) {
                    auto s = wide(choice.get<std::string>());
                    SendMessageW(f.control, CB_ADDSTRING, 0, (LPARAM)s.c_str());
                    if (choice == data["value"]) selected = n;
                    ++n;
                }
                SendMessageW(f.control, CB_SETCURSEL, selected, 0);
            } else {
                f.control = control(panel, L"EDIT", wide(data["value"].get<std::string>()), WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 2000 + index);
                SendMessageW(f.control, EM_SETLIMITTEXT, 32767, 0);
            }
        }
        fields.push_back(f); y += kind == "command" ? 50 : kind == "bool" ? 64 : 84; ++index;
    }
    contentHeight = y; layoutFields(); rebuilding = false;
}

struct JsonEditor { HWND edit = nullptr; bool done = false, accepted = false; json value; };
static LRESULT CALLBACK EditorProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = (JsonEditor*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_CREATE) {
        state = (JsonEditor*)((CREATESTRUCTW*)lp)->lpCreateParams; SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)state);
        state->edit = control(h, L"EDIT", wide(state->value.dump(2)), WS_TABSTOP | WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN, 1);
        SendMessageW(state->edit, EM_SETLIMITTEXT, 8 * 1024 * 1024, 0);
        control(h, L"BUTTON", L"Aplicar", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
        control(h, L"BUTTON", L"Cancelar", WS_TABSTOP, IDCANCEL);
    } else if (msg == WM_SIZE && state) {
        RECT r{}; GetClientRect(h, &r);
        MoveWindow(state->edit, 12, 12, r.right - 24, r.bottom - 66, TRUE);
        MoveWindow(GetDlgItem(h, IDOK), r.right - 224, r.bottom - 42, 100, 30, TRUE);
        MoveWindow(GetDlgItem(h, IDCANCEL), r.right - 112, r.bottom - 42, 100, 30, TRUE);
    } else if (msg == WM_COMMAND && state && HIWORD(wp) == BN_CLICKED) {
        if (LOWORD(wp) == IDOK) {
            try {
                auto parsed = json::parse(utf8(text(state->edit)));
                if ((state->value.is_array() && !parsed.is_array()) || (state->value.is_object() && !parsed.is_object()))
                    throw std::runtime_error("Mantenha o tipo de estrutura JSON original.");
                state->value = parsed; state->accepted = true; DestroyWindow(h);
            } catch (const std::exception& ex) { message(ex.what()); }
        } else if (LOWORD(wp) == IDCANCEL) DestroyWindow(h);
    } else if (msg == WM_CLOSE) { DestroyWindow(h); return 0; }
    else if (msg == WM_DESTROY && state) state->done = true;
    return DefWindowProcW(h, msg, wp, lp);
}
static void editJson(int index) {
    JsonEditor state; state.value = fields[index].data["editedValue"];
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CaybJsonEditor", L"Editor JSON — cayb trap", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 760, 540, windowHandle, nullptr, instance, &state);
    EnableWindow(windowHandle, FALSE);
    MSG msg{};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(windowHandle, TRUE); SetForegroundWindow(windowHandle);
    if (state.accepted) { fields[index].data["editedValue"] = state.value; SetWindowTextW(status, L"Alterações pendentes — clique em Salvar"); }
}

static void startBridge() {
    engineMissing = GetFileAttributesW(enginePath.c_str()) == INVALID_FILE_ATTRIBUTES;
    if (engineMissing) {
        setBusy(false); SetWindowTextW(status, L"Motor C# ausente: compile o pacote completo no Windows.");
        SetWindowTextW(heading, L"Interface C++ pronta; motor ainda não compilado"); return;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readOut = nullptr, writeOut = nullptr, readIn = nullptr, writeIn = nullptr;
    if (!CreatePipe(&readOut, &writeOut, &sa, 0)) { message("Não foi possível abrir o canal do motor."); setBusy(false); return; }
    if (!CreatePipe(&readIn, &writeIn, &sa, 0)) { CloseHandle(readOut); CloseHandle(writeOut); setBusy(false); return; }
    SetHandleInformation(readOut, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(writeIn, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = readIn; si.hStdOutput = writeOut;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    auto command = quote(enginePath) + L" -nativebridge";
    BOOL started = CreateProcessW(enginePath.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, appDir.c_str(), &si, &pi);
    CloseHandle(readIn); CloseHandle(writeOut);
    if (!started) { CloseHandle(readOut); CloseHandle(writeIn); setBusy(false); message("Falha ao iniciar o motor .NET."); return; }
    CloseHandle(pi.hThread); engineProcess = pi.hProcess; engineInput = writeIn; bridgeAlive = true; setBusy(true);
    WPARAM currentGeneration = ++generation;
    std::thread([readOut, currentGeneration]() {
        std::string pending; char buffer[4096]; DWORD count;
        while (ReadFile(readOut, buffer, sizeof(buffer), &count, nullptr) && count) {
            pending.append(buffer, count);
            size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos) {
                auto line = pending.substr(0, newline); pending.erase(0, newline + 1);
                auto* reply = new json;
                try { *reply = json::parse(line); }
                catch (...) { *reply = {{"ok", false}, {"error", "Resposta inválida do motor"}}; }
                if (!PostMessageW(windowHandle, EngineReplyMessage, currentGeneration, (LPARAM)reply)) delete reply;
            }
        }
        CloseHandle(readOut);
        auto* reply = new json{{"ok", false}, {"disconnected", true}, {"error", "Motor desconectado. Use Instalar / reconectar."}};
        if (!PostMessageW(windowHandle, EngineReplyMessage, currentGeneration, (LPARAM)reply)) delete reply;
    }).detach();
}

static bool runEngine(const wchar_t* arguments, bool reconnect) {
    if (engineMissing) { message("Este executável é somente a interface. Compile o motor com build-cayb-trap.ps1 no Windows."); return false; }
    if (busy) return false;
    if (!changes().empty() || snapshot.value("dirty", false)) {
        MessageBoxW(windowHandle, L"Salve as alterações antes de iniciar outra operação.", L"cayb trap", MB_OK | MB_ICONINFORMATION); return false;
    }
    if (reconnect) {
        if (engineInput) { CloseHandle(engineInput); engineInput = nullptr; }
        bridgeAlive = false; ++generation;
    }
    SHELLEXECUTEINFOW info{sizeof(info)}; info.fMask = SEE_MASK_NOCLOSEPROCESS; info.hwnd = windowHandle;
    info.lpFile = enginePath.c_str(); info.lpParameters = arguments; info.lpDirectory = appDir.c_str(); info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) { message("Não foi possível iniciar a operação."); return false; }
    if (reconnect && info.hProcess) {
        HANDLE child = info.hProcess; setBusy(true);
        std::thread([child]() { WaitForSingleObject(child, INFINITE); CloseHandle(child); PostMessageW(windowHandle, RestartMessage, 0, 0); }).detach();
    } else if (info.hProcess) CloseHandle(info.hProcess);
    return true;
}

static LRESULT CALLBACK PanelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND && !busy && !rebuilding) {
        int id = LOWORD(wp);
        if (id >= 3000 && id < 3000 + (int)fields.size()) { editJson(id - 3000); return 0; }
        if (id >= 1000 && id < 1000 + (int)fields.size() && HIWORD(wp) == BN_CLICKED) {
            auto& f = fields[id - 1000];
            if (f.data["path"] == "$uninstall") { if (runEngine(L"-uninstall", true)) DestroyWindow(windowHandle); return 0; }
            if (MessageBoxW(windowHandle, L"Executar esta ação do Bloxstrap? Algumas ações abrem os diálogos originais e podem alterar arquivos imediatamente.", L"cayb trap", MB_YESNO | MB_ICONQUESTION) == IDYES)
                send({{"op", "command"}, {"page", snapshot["pages"][pageIndex]["id"]}, {"path", f.data["path"]}, {"changes", changes()}});
            return 0;
        }
        if (id >= 2000) SetWindowTextW(status, L"Alterações pendentes — clique em Salvar");
    }
    if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL) {
        RECT r{}; GetClientRect(h, &r);
        if (msg == WM_MOUSEWHEEL) scrollPosition -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 80;
        else switch (LOWORD(wp)) {
            case SB_LINEUP: scrollPosition -= 40; break;
            case SB_LINEDOWN: scrollPosition += 40; break;
            case SB_PAGEUP: scrollPosition -= r.bottom; break;
            case SB_PAGEDOWN: scrollPosition += r.bottom; break;
            case SB_THUMBTRACK: { SCROLLINFO si{sizeof(si)}; si.fMask = SIF_TRACKPOS; GetScrollInfo(h, SB_VERT, &si); scrollPosition = si.nTrackPos; break; }
        }
        scrollPosition = std::max(0, std::min(scrollPosition, std::max(0, contentHeight - (int)r.bottom)));
        layoutFields(); return 0;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        SetTextColor((HDC)wp, inkColor); SetBkColor((HDC)wp, canvasColor); return (LRESULT)background;
    }
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        SetTextColor((HDC)wp, inkColor); SetBkColor((HDC)wp, cardColor); return (LRESULT)cardBrush;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void layout() {
    RECT r{}; GetClientRect(windowHandle, &r); int w = r.right, h = r.bottom;
    MoveWindow(nav, 14, 64, 212, h - 158, TRUE); MoveWindow(heading, 250, 20, w - 270, 38, TRUE);
    MoveWindow(panel, 240, 66, w - 250, h - 134, TRUE);
    MoveWindow(GetDlgItem(windowHandle, 12), 14, h - 83, 100, 30, TRUE);
    MoveWindow(GetDlgItem(windowHandle, 13), 120, h - 83, 106, 30, TRUE);
    MoveWindow(installButton, 14, h - 44, 212, 30, TRUE);
    MoveWindow(status, 250, h - 45, std::max(100, w - 495), 30, TRUE);
    MoveWindow(saveButton, w - 222, h - 47, 96, 32, TRUE);
    MoveWindow(GetDlgItem(windowHandle, 11), w - 116, h - 47, 96, 32, TRUE);
    layoutFields();
}
static LRESULT CALLBACK WindowProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        windowHandle = h;
        control(h, L"STATIC", L"cayb trap", SS_LEFT, 30);
        MoveWindow(GetDlgItem(h, 30), 20, 22, 200, 32, TRUE);
        heading = control(h, L"STATIC", L"Conectando ao motor…", SS_LEFT); SendMessageW(heading, WM_SETFONT, (WPARAM)titleFont, TRUE);
        nav = control(h, L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, 1);
        SendMessageW(nav, LB_SETITEMHEIGHT, 0, 46);
        panel = control(h, L"CaybPanel", L"", WS_VSCROLL | WS_CLIPCHILDREN);
        status = control(h, L"STATIC", L"Iniciando…", SS_LEFT);
        saveButton = control(h, L"BUTTON", L"Salvar", WS_TABSTOP | BS_DEFPUSHBUTTON, 10);
        control(h, L"BUTTON", L"Fechar", WS_TABSTOP, 11);
        control(h, L"BUTTON", L"Roblox", WS_TABSTOP, 12);
        control(h, L"BUTTON", L"Studio", WS_TABSTOP, 13);
        installButton = control(h, L"BUTTON", L"Instalar / reconectar", WS_TABSTOP, 14);
        BOOL dark = TRUE; DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));
        layout(); startBridge(); return 0;
    }
    if (msg == WM_GETMINMAXINFO) { auto* info = (MINMAXINFO*)lp; info->ptMinTrackSize = {1000, 620}; return 0; }
    if (msg == WM_SIZE) { if (panel) layout(); return 0; }
    if (msg == WM_MEASUREITEM) { ((MEASUREITEMSTRUCT*)lp)->itemHeight = 46; return TRUE; }
    if (msg == WM_DRAWITEM && wp == 1) {
        auto* item = (DRAWITEMSTRUCT*)lp; if (item->itemID == (UINT)-1) return TRUE;
        bool selected = item->itemState & ODS_SELECTED;
        HBRUSH brush = CreateSolidBrush(selected ? selectionColor : canvasColor); FillRect(item->hDC, &item->rcItem, brush); DeleteObject(brush);
        if (selected) { RECT marker = item->rcItem; marker.right = marker.left + 3; marker.top += 10; marker.bottom -= 10;
            HBRUSH accent = CreateSolidBrush(RGB(96, 205, 255)); FillRect(item->hDC, &marker, accent); DeleteObject(accent); }
        wchar_t label[256]{}; SendMessageW(nav, LB_GETTEXT, item->itemID, (LPARAM)label);
        SetTextColor(item->hDC, inkColor); SetBkMode(item->hDC, TRANSPARENT); SelectObject(item->hDC, font);
        RECT rect = item->rcItem; rect.left += 16; DrawTextW(item->hDC, label, -1, &rect, DT_SINGLELINE | DT_VCENTER); return TRUE;
    }
    if (msg == EngineReplyMessage) {
        std::unique_ptr<json> reply((json*)lp);
        if (wp != generation) return 0;
        setBusy(false);
        if (!reply->value("ok", false)) {
            if (reply->value("disconnected", false)) { bridgeAlive = false; SetWindowTextW(status, L"Motor desconectado — use reconectar"); }
            else { message(reply->value("error", "Erro desconhecido")); requestedPage = pageIndex; SendMessageW(nav, LB_SETCURSEL, pageIndex, 0); }
            return 0;
        }
        snapshot = *reply;
        darkTheme = snapshot.value("theme", "Dark") != "Light";
        canvasColor = darkTheme ? RGB(32,32,32) : RGB(243,243,243);
        inkColor = darkTheme ? RGB(240,240,240) : RGB(24,24,24);
        cardColor = darkTheme ? RGB(48,48,48) : RGB(255,255,255);
        selectionColor = darkTheme ? RGB(52,52,52) : RGB(225,225,225);
        HBRUSH oldBackground = background, oldCard = cardBrush;
        background = CreateSolidBrush(canvasColor); cardBrush = CreateSolidBrush(cardColor);
        SetClassLongPtrW(windowHandle, GCLP_HBRBACKGROUND, (LONG_PTR)background);
        SetClassLongPtrW(panel, GCLP_HBRBACKGROUND, (LONG_PTR)background);
        DeleteObject(oldBackground); DeleteObject(oldCard);
        BOOL dark = darkTheme; DwmSetWindowAttribute(windowHandle, 20, &dark, sizeof(dark));
        EnumChildWindows(windowHandle, [](HWND child, LPARAM) -> BOOL {
            SetWindowTheme(child, darkTheme ? L"DarkMode_Explorer" : L"Explorer", nullptr); return TRUE;
        }, 0);
        InvalidateRect(windowHandle, nullptr, TRUE);
        rebuilding = true; SendMessageW(nav, LB_RESETCONTENT, 0, 0);
        for (const auto& p : snapshot["pages"]) { auto name = wide(p["title"].get<std::string>()); SendMessageW(nav, LB_ADDSTRING, 0, (LPARAM)name.c_str()); }
        pageIndex = std::min(requestedPage, std::max(0, (int)snapshot["pages"].size() - 1));
        SendMessageW(nav, LB_SETCURSEL, pageIndex, 0); rebuilding = false;
        renderPage(); setBusy(false);
        SetWindowTextW(status, !snapshot.value("installed", false) ? L"Motor presente; instalação necessária." : snapshot.value("dirty", false) ? L"Alterações pendentes — clique em Salvar" : L"Conectado ao motor original");
        return 0;
    }
    if (msg == RestartMessage) {
        if (engineProcess) { CloseHandle(engineProcess); engineProcess = nullptr; }
        startBridge(); return 0;
    }
    if (msg == WM_COMMAND && !rebuilding) {
        int id = LOWORD(wp);
        if (id == 11) SendMessageW(h, WM_CLOSE, 0, 0);
        else if (id == 14 && !busy) {
            if (!bridgeAlive && !engineMissing) { startBridge(); }
            else if (snapshot.value("installed", false)) {
                if (engineInput) { CloseHandle(engineInput); engineInput = nullptr; }
                ++generation; bridgeAlive = false; setBusy(true);
                HANDLE old = engineProcess;
                std::thread([old]() { if (old) WaitForSingleObject(old, INFINITE); PostMessageW(windowHandle, RestartMessage, 0, 0); }).detach();
            } else runEngine(L"-nolaunch", true);
        } else if (!busy && id == 10) send({{"op", "save"}, {"changes", changes()}});
        else if (!busy && id == 12) runEngine(L"-player", false);
        else if (!busy && id == 13) runEngine(L"-studio", false);
        else if (!busy && id == 1 && HIWORD(wp) == LBN_SELCHANGE) {
            requestedPage = (int)SendMessageW(nav, LB_GETCURSEL, 0, 0);
            send({{"op", "apply"}, {"changes", changes()}});
        }
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) return SendMessageW(panel, msg, wp, lp);
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORLISTBOX) { SetTextColor((HDC)wp, inkColor); SetBkColor((HDC)wp, canvasColor); return (LRESULT)background; }
    if (msg == WM_CLOSE) {
        if (busy && bridgeAlive) { MessageBoxW(h, L"Aguarde a operação do motor terminar antes de fechar.", L"cayb trap", MB_OK); return 0; }
        if ((!changes().empty() || snapshot.value("dirty", false)) && MessageBoxW(h, L"Fechar sem salvar as alterações pendentes?", L"cayb trap", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
        if (engineInput) { CloseHandle(engineInput); engineInput = nullptr; }
        DestroyWindow(h); return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int show) {
    instance = hInstance;
    SetProcessDPIAware();
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
    appDir = path; appDir = appDir.substr(0, appDir.find_last_of(L"\\/"));
    enginePath = appDir + L"\\engine\\cayb trap.exe";
    if (GetFileAttributesW(enginePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        auto sibling = appDir + L"\\cayb trap.exe";
        if (_wcsicmp(sibling.c_str(), path) != 0) enginePath = sibling;
    }
    HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\CaybTrapCppSettings");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { MessageBoxW(nullptr, L"A interface já está aberta.", L"cayb trap", MB_OK); if (singleton) CloseHandle(singleton); return 0; }
    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    // The supplied Minecraft font stays embedded and private to this process.
    if (HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(2), RT_RCDATA)) {
        HGLOBAL loaded = LoadResource(instance, resource);
        void* data = loaded ? LockResource(loaded) : nullptr;
        DWORD size = SizeofResource(instance, resource), fontCount = 0;
        if (data && size) privateFont = AddFontMemResourceEx(data, size, nullptr, &fontCount);
    }
    const wchar_t* typeface = privateFont ? L"Minecraft" : L"Segoe UI";
    DWORD quality = privateFont ? NONANTIALIASED_QUALITY : CLEARTYPE_QUALITY;
    font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, quality, 0, typeface);
    titleFont = CreateFontW(-26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, quality, 0, typeface);
    background = CreateSolidBrush(canvasColor); cardBrush = CreateSolidBrush(cardColor);
    WNDCLASSW wc{}; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = background;
    wc.lpfnWndProc = PanelProc; wc.lpszClassName = L"CaybPanel"; RegisterClassW(&wc);
    wc.lpfnWndProc = EditorProc; wc.lpszClassName = L"CaybJsonEditor"; RegisterClassW(&wc);
    wc.lpfnWndProc = WindowProc; wc.lpszClassName = L"CaybTrapCpp"; wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"cayb trap — Configurações", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1050, 660, nullptr, nullptr, instance, nullptr);
    if (!h) return 1;
    ShowWindow(h, show); UpdateWindow(h);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (engineProcess) CloseHandle(engineProcess);
    if (singleton) CloseHandle(singleton);
    DeleteObject(font); DeleteObject(titleFont); DeleteObject(background); DeleteObject(cardBrush);
    if (privateFont) RemoveFontMemResourceEx(privateFont);
    return (int)msg.wParam;
}
