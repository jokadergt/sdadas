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
#include <mmsystem.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
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
static int corner = 24;
static std::string currentFont = "SegoeUI";
static HBRUSH background, cardBrush;
static COLORREF canvasColor = RGB(0,0,0), inkColor = RGB(240,240,240), cardColor = RGB(14,14,14), selectionColor = RGB(28,28,28);
static bool darkTheme = true;
static bool bubbleSounds = true;
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
static std::vector<HWND> cursorWidgets;
static HWND cursorQuery, cursorResults, cursorPreviewBox;
static ULONG_PTR imagingToken = 0;

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

static void bubbleClick() {
    if (bubbleSounds) PlaySoundW(MAKEINTRESOURCEW(3), instance, SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
}
static void roundControl(HWND h) {
    RECT r{}; GetClientRect(h, &r);
    if (r.right <= 0 || r.bottom <= 0) return;
    HRGN region = CreateRoundRectRgn(0, 0, r.right + 1, r.bottom + 1, corner, corner);
    if (!SetWindowRgn(h, region, TRUE)) DeleteObject(region);
}
static bool drawButton(LPARAM lp) {
    auto* item = (DRAWITEMSTRUCT*)lp;
    if (!item || item->CtlType != ODT_BUTTON) return false;
    HDC dc = item->hDC; RECT r = item->rcItem;
    FillRect(dc, &r, background);
    bool enabled = !(item->itemState & ODS_DISABLED);
    bool primary = item->CtlID == 10 || item->CtlID == IDOK;
    COLORREF fill = primary && enabled ? RGB(96,205,255) : (item->itemState & ODS_SELECTED) ? selectionColor : cardColor;
    HBRUSH brush = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, selectionColor);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen), oldFont = SelectObject(dc, font);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, corner, corner);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, !enabled ? RGB(135,135,135) : primary ? RGB(16,25,30) : inkColor);
    auto label = text(item->hwndItem); InflateRect(&r, -10, -3);
    DrawTextW(dc, label.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if ((item->itemState & ODS_FOCUS) && GetKeyState(VK_LBUTTON) >= 0 && (SendMessageW(item->hwndItem, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0) { InflateRect(&r, -2, -2); DrawFocusRect(dc, &r); }
    SelectObject(dc, oldFont); SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(brush); DeleteObject(pen);
    return true;
}
static LRESULT CALLBACK RoundedControlProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_LBUTTONDOWN) SendMessageW(h, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    if (msg == WM_SIZE) roundControl(h);
    if (msg == WM_KEYDOWN && wp == VK_RETURN && GetDlgCtrlID(h) == 4001) {
        HWND parent = GetParent(h);
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(4002, BN_CLICKED), (LPARAM)GetDlgItem(parent, 4002));
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, RoundedControlProc, 1);
    bool toggle = (GetWindowLongPtrW(h, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX;
    wchar_t className[32]{}; GetClassNameW(h, className, 32);
    toggle = toggle && _wcsicmp(className, L"BUTTON") == 0;
    if (toggle && msg == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(h, &ps); RECT r{}; GetClientRect(h, &r);
        FillRect(dc, &r, background);
        HBRUSH card = CreateSolidBrush(cardColor); HGDIOBJ oldBrush = SelectObject(dc, card), oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, 0, 0, r.right, r.bottom, corner, corner);
        bool checked = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
        int x = r.right - 52, y = (r.bottom - 20) / 2;
        HBRUSH track = CreateSolidBrush(checked ? RGB(96,205,255) : selectionColor); SelectObject(dc, track);
        RoundRect(dc, x, y, x + 38, y + 20, 20, 20);
        HBRUSH thumb = CreateSolidBrush(checked ? RGB(22,30,36) : inkColor); SelectObject(dc, thumb);
        int tx = x + (checked ? 21 : 3); Ellipse(dc, tx, y + 3, tx + 14, y + 17);
        HGDIOBJ oldFont = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, inkColor);
        RECT label{12, 0, x - 12, r.bottom}, measure = label; auto value = text(h);
        DrawTextW(dc, value.c_str(), -1, &measure, DT_WORDBREAK | DT_CALCRECT);
        label.top = std::max(0L, (r.bottom - measure.bottom) / 2);
        DrawTextW(dc, value.c_str(), -1, &label, DT_WORDBREAK);
        if (GetFocus() == h && (SendMessageW(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0) { RECT focus{6,6,r.right-6,r.bottom-6}; DrawFocusRect(dc, &focus); }
        SelectObject(dc, oldFont); SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
        DeleteObject(card); DeleteObject(track); DeleteObject(thumb); EndPaint(h, &ps); return 0;
    }
    LRESULT result = DefSubclassProc(h, msg, wp, lp);
    if (toggle && (msg == BM_SETCHECK || msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ENABLE)) InvalidateRect(h, nullptr, TRUE);
    return result;
}

static void message(const std::string& value) { MessageBoxW(windowHandle, wide(value).c_str(), L"cayb trap", MB_OK | MB_ICONERROR); }
static HWND control(HWND parent, const wchar_t* klass, const std::wstring& value, DWORD style, int id = 0) {
    if (_wcsicmp(klass, L"BUTTON") == 0 && ((style & BS_TYPEMASK) == BS_PUSHBUTTON || (style & BS_TYPEMASK) == BS_DEFPUSHBUTTON))
        style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    HWND h = CreateWindowExW(0, klass, value.c_str(), WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, instance, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    SetWindowTheme(h, darkTheme ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    SetWindowSubclass(h, RoundedControlProc, 1, 0);
    if (_wcsicmp(klass, L"COMBOBOX") == 0) {
        COMBOBOXINFO info{sizeof(info)};
        if (GetComboBoxInfo(h, &info) && info.hwndList) SetWindowSubclass(info.hwndList, RoundedControlProc, 1, 0);
    }
    return h;
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
    if (!bridgeAlive || !engineInput) { message("Conexão indisponível. Use Reconectar."); return; }
    std::string wire = request.dump() + "\n";
    setBusy(true);
    size_t offset = 0;
    while (offset < wire.size()) {
        DWORD written = 0;
        if (!WriteFile(engineInput, wire.data() + offset, (DWORD)(wire.size() - offset), &written, nullptr) || !written) {
            setBusy(false); message("A conexão com o aplicativo foi encerrada."); return;
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
        if (kind == "command" || kind == "info" || !f.data.value("enabled",true)) continue;
        json value;
        if (kind == "bool") value = SendMessageW(f.control, BM_GETCHECK, 0, 0) == BST_CHECKED;
        else if (kind == "json") value = f.data["editedValue"];
        else if (kind == "enum") {
            LRESULT index = SendMessageW(f.control, CB_GETCURSEL, 0, 0);
            if (index < 0 || index >= (LRESULT)f.data["choices"].size()) continue;
            value = f.data["choices"][(size_t)index];
        } else value = utf8(text(f.control));
        if (value != f.data["value"])
            result.push_back({{"page", page}, {"path", f.data["path"]}, {"value", value}});
    }
    return result;
}


static bool cursorPage() {
    return snapshot.contains("pages") && !snapshot["pages"].empty() && snapshot["pages"][pageIndex]["id"] == "Cursors";
}
static json catalogData() {
    return snapshot.value("cursorCatalog", json{{"query",""},{"selected",""},{"results",json::array()},{"cursorPreview",""},{"pointerPreview",""},{"installedName",""}});
}
static void layoutCursors() {
    if (cursorWidgets.empty()) return;
    RECT r{}; GetClientRect(panel, &r); int width = r.right - 24, available = r.bottom;
    MoveWindow(cursorQuery, 12, 8, width - 128, 34, TRUE);
    MoveWindow(GetDlgItem(panel,4002), width - 104, 8, 116, 34, TRUE);
    MoveWindow(GetDlgItem(panel,4008),12,50,width,36,TRUE);
    int leftWidth = width * 59 / 100, height = std::max(160, available - 182);
    MoveWindow(cursorResults,12,94,leftWidth,height,TRUE);
    MoveWindow(cursorPreviewBox,leftWidth+24,94,width-leftWidth-12,height,TRUE);
    MoveWindow(GetDlgItem(panel,4009),12,available-78,width,26,TRUE);
    int buttonWidth=(width-16)/3;
    MoveWindow(GetDlgItem(panel,4004),12,available-44,buttonWidth,34,TRUE);
    MoveWindow(GetDlgItem(panel,4005),20+buttonWidth,available-44,buttonWidth,34,TRUE);
    MoveWindow(GetDlgItem(panel,4006),28+buttonWidth*2,available-44,buttonWidth,34,TRUE);
    SCROLLINFO info{sizeof(info)};info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;info.nMax=0;info.nPage=available;
    SetScrollInfo(panel,SB_VERT,&info,TRUE);
}
static LRESULT CALLBACK CursorPreviewProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==WM_PAINT) {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);FillRect(dc,&r,cardBrush);
        auto data=catalogData(); auto items=data["results"];std::string selected=data["selected"];
        std::wstring label=L"Selecione um resultado\npara carregar a prévia";
        for(const auto& item:items) if(item["slug"]==selected) label=wide(item["name"].get<std::string>());
        SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,inkColor);
        RECT title{10,12,r.right-10,72};DrawTextW(dc,label.c_str(),-1,&title,DT_CENTER|DT_WORDBREAK|DT_END_ELLIPSIS);
        std::string paths[]={data.value("cursorPreview",std::string{}),data.value("pointerPreview",std::string{})};
        for(int n=0;n<2;n++) {
            int x=10+n*(r.right-20)/2, y=88;
            if(!paths[n].empty() && imagingToken) {
                Gdiplus::Graphics graphics(dc);Gdiplus::Image image(wide(paths[n]).c_str());
                if(image.GetLastStatus()==Gdiplus::Ok) graphics.DrawImage(&image,x+std::max(0,(int)((r.right-20)/2-64)/2),y,64,64);
            }
            RECT caption{x,158,x+(r.right-20)/2,190};DrawTextW(dc,n==0?L"Cursor":L"Ponteiro",-1,&caption,DT_CENTER|DT_SINGLELINE);
        }
        RECT info{10,204,r.right-10,r.bottom-10};DrawTextW(dc,L"PNG estático para Roblox\nFonte: Sweezy Cursors",-1,&info,DT_CENTER|DT_WORDBREAK);
        EndPaint(h,&ps);return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
static void renderCursors() {
    auto data=catalogData();
    auto add=[](const wchar_t* cls,const std::wstring& caption,DWORD style,int id){HWND h=control(panel,cls,caption,style,id);cursorWidgets.push_back(h);return h;};
    SetWindowTextW(heading,L"Cursores para Roblox");
    cursorQuery=add(L"EDIT",wide(data.value("query",std::string{})),WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,4001);
    SendMessageW(cursorQuery,EM_SETCUEBANNER,TRUE,(LPARAM)L"Ex.: Minecraft, anime, cat…");SendMessageW(cursorQuery,EM_SETLIMITTEXT,80,0);
    add(L"BUTTON",L"Pesquisar",WS_TABSTOP,4002);
    add(L"STATIC",L"Pesquise no Sweezy Cursors. Selecione uma prévia e clique em Usar no Roblox.",SS_LEFT,4008);
    cursorResults=add(L"LISTBOX",L"",WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,4003);
    SendMessageW(cursorResults,LB_SETITEMHEIGHT,0,60);
    int index=0;
    for(const auto& item:data["results"]) {
        auto name=wide(item["name"].get<std::string>());SendMessageW(cursorResults,LB_ADDSTRING,0,(LPARAM)name.c_str());
        if(item["slug"]==data["selected"]) SendMessageW(cursorResults,LB_SETCURSEL,index,0);
        index++;
    }
    cursorPreviewBox=add(L"CaybCursorPreview",L"",0,4007);
    auto installed=data.value("installedName",std::string{});
    add(L"STATIC",installed.empty()?L"Nenhum cursor Sweezy selecionado.":L"Selecionado: "+wide(installed),SS_LEFT,4009);
    add(L"BUTTON",L"Usar no Roblox",WS_TABSTOP,4004);
    add(L"BUTTON",L"Restaurar padrão",WS_TABSTOP,4005);
    add(L"BUTTON",L"Ver no Sweezy",WS_TABSTOP,4006);
    EnableWindow(GetDlgItem(panel,4004),!data.value("selected",std::string{}).empty());
    layoutCursors();
}
static bool drawCursorRow(LPARAM lp) {
    auto* item=(DRAWITEMSTRUCT*)lp;if(item->CtlType!=ODT_LISTBOX||item->CtlID!=4003)return false;
    auto data=catalogData();if(item->itemID>=data["results"].size())return true;
    auto value=data["results"][item->itemID];RECT r=item->rcItem;FillRect(item->hDC,&r,background);
    HBRUSH brush=CreateSolidBrush((item->itemState&ODS_SELECTED)?selectionColor:cardColor);
    auto old=SelectObject(item->hDC,brush);auto pen=SelectObject(item->hDC,GetStockObject(NULL_PEN));
    RoundRect(item->hDC,r.left,r.top+2,r.right,r.bottom-2,corner,corner);SelectObject(item->hDC,old);SelectObject(item->hDC,pen);DeleteObject(brush);
    SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,inkColor);SelectObject(item->hDC,font);
    RECT name{r.left+10,r.top+6,r.right-10,r.top+42};auto label=wide(value["name"].get<std::string>());
    DrawTextW(item->hDC,label.c_str(),-1,&name,DT_WORDBREAK|DT_END_ELLIPSIS);
    return true;
}
static void layoutFields() {
    if(cursorPage()){layoutCursors();return;}
    RECT area{}; GetClientRect(panel, &area);
    int width = area.right - 26;
    for (auto& f : fields) {
        int y = f.y - scrollPosition;
        std::string kind = f.data["kind"];
        if (kind == "command") { MoveWindow(f.button, 12, y, width - 24, 38, TRUE); continue; }
        if (kind == "info") { MoveWindow(f.label,12,y,width-24,76,TRUE);continue; }
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
    for(HWND h:cursorWidgets) { DestroyWindow(h); }
    cursorWidgets.clear();
    for (auto& f : fields) { if (f.label) DestroyWindow(f.label); if (f.control) DestroyWindow(f.control); if (f.button) DestroyWindow(f.button); }
    fields.clear();
    if (resetScroll) scrollPosition = 0;
    if (!snapshot.value("installed", false) || snapshot["pages"].empty()) {
        SetWindowTextW(heading, L"Conclua a instalação para continuar"); rebuilding = false; return;
    }
    if(cursorPage()){renderCursors();rebuilding=false;return;}
    auto page = snapshot["pages"][pageIndex];
    SetWindowTextW(heading, wide(page["title"].get<std::string>()).c_str());
    int y = 8, index = 0;
    for (auto data : page["fields"]) {
        // The editor below replaces the old WPF editor navigation command.
        if (data["path"] == "OpenFastFlagEditorCommand") continue;
        Field f; f.data = data; f.y = y;
        auto label = wide(data["label"].get<std::string>());
        std::string kind = data["kind"];
        if (kind == "info") f.label = control(panel,L"STATIC",label,SS_LEFT);
        else if (kind == "command") f.button = control(panel, L"BUTTON", label, WS_TABSTOP | BS_PUSHBUTTON, 1000 + index);
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
                    auto s = wide(data.contains("choiceLabels") ? data["choiceLabels"][n].get<std::string>() : choice.get<std::string>());
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
        if(f.control && !data.value("enabled",true)) EnableWindow(f.control,FALSE);
        fields.push_back(f); y += kind == "info" ? 90 : kind == "command" ? 50 : kind == "bool" ? 64 : 84; ++index;
    }
    contentHeight = y; layoutFields(); rebuilding = false;
}

struct JsonEditor { HWND edit = nullptr; bool done = false, accepted = false; json value; };
static LRESULT CALLBACK EditorProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = (JsonEditor*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_DRAWITEM && drawButton(lp)) return TRUE;
    if (msg == WM_CREATE) {
        state = (JsonEditor*)((CREATESTRUCTW*)lp)->lpCreateParams; SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)state);
        DWORD corners = 2; DwmSetWindowAttribute(h, 33, &corners, sizeof(corners));
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
        bubbleClick();
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
        setBusy(false); SetWindowTextW(status, L"Arquivo obrigatório ausente. Extraia o ZIP completo.");
        SetWindowTextW(heading, L"Instalação incompleta"); return;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readOut = nullptr, writeOut = nullptr, readIn = nullptr, writeIn = nullptr;
    if (!CreatePipe(&readOut, &writeOut, &sa, 0)) { message("Não foi possível iniciar a comunicação com o aplicativo."); setBusy(false); return; }
    if (!CreatePipe(&readIn, &writeIn, &sa, 0)) { CloseHandle(readOut); CloseHandle(writeOut); setBusy(false); return; }
    SetHandleInformation(readOut, HANDLE_FLAG_INHERIT, 0); SetHandleInformation(writeIn, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)}; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = readIn; si.hStdOutput = writeOut;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    auto command = quote(enginePath) + L" -nativebridge";
    BOOL started = CreateProcessW(enginePath.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, appDir.c_str(), &si, &pi);
    CloseHandle(readIn); CloseHandle(writeOut);
    if (!started) { CloseHandle(readOut); CloseHandle(writeIn); setBusy(false); message("Não foi possível iniciar o aplicativo. Confira se o pacote foi extraído por completo."); return; }
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
                catch (...) { *reply = {{"ok", false}, {"error", "O aplicativo retornou uma resposta inválida."}}; }
                if (!PostMessageW(windowHandle, EngineReplyMessage, currentGeneration, (LPARAM)reply)) delete reply;
            }
        }
        CloseHandle(readOut);
        auto* reply = new json{{"ok", false}, {"disconnected", true}, {"error", "Conexão encerrada. Use Reconectar."}};
        if (!PostMessageW(windowHandle, EngineReplyMessage, currentGeneration, (LPARAM)reply)) delete reply;
    }).detach();
}

static bool runEngine(const wchar_t* arguments, bool reconnect) {
    if (engineMissing) { message("Extraia todo o ZIP e mantenha a pasta engine ao lado do executável."); return false; }
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
    if(msg==WM_DRAWITEM&&drawCursorRow(lp))return TRUE;
    if (msg == WM_DRAWITEM && drawButton(lp)) return TRUE;
    if (msg == WM_COMMAND && !busy && !rebuilding) {
        int id = LOWORD(wp);
        if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == CBN_SELCHANGE) bubbleClick();

        if(cursorPage()&&id>=4001&&id<=4009){
            if(id==4002&&HIWORD(wp)==BN_CLICKED){SetWindowTextW(status,L"Pesquisando no Sweezy…");send({{"op","cursor.search"},{"query",utf8(text(cursorQuery))}});}
            else if(id==4003&&HIWORD(wp)==LBN_SELCHANGE){
                LRESULT i=SendMessageW(cursorResults,LB_GETCURSEL,0,0);auto data=catalogData();
                if(i>=0&&i<(LRESULT)data["results"].size()){SetWindowTextW(status,L"Carregando prévia…");send({{"op","cursor.preview"},{"slug",data["results"][(size_t)i]["slug"]}});}
            }else if(id==4004&&HIWORD(wp)==BN_CLICKED){auto data=catalogData();SetWindowTextW(status,L"Preparando cursor para Roblox…");send({{"op","cursor.install"},{"slug",data["selected"]}});}
            else if(id==4005&&HIWORD(wp)==BN_CLICKED){if(MessageBoxW(windowHandle,L"Restaurar os cursores padrão na próxima abertura do Roblox?",L"cayb trap",MB_YESNO|MB_ICONQUESTION)==IDYES)send({{"op","cursor.reset"}});}
            else if(id==4006&&HIWORD(wp)==BN_CLICKED){auto data=catalogData();auto slug=data.value("selected",std::string{});auto url=wide(slug.empty()?"https://sweezy-cursors.com/":"https://sweezy-cursors.com/cursor/"+slug+"/");ShellExecuteW(windowHandle,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
            return 0;
        }
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
        DWORD corners = 2; DwmSetWindowAttribute(h, 33, &corners, sizeof(corners));
        layout(); startBridge(); return 0;
    }
    if (msg == WM_GETMINMAXINFO) { auto* info = (MINMAXINFO*)lp; info->ptMinTrackSize = {1000, 620}; return 0; }
    if (msg == WM_SIZE) { if (panel) layout(); return 0; }
    if (msg == WM_MEASUREITEM) { ((MEASUREITEMSTRUCT*)lp)->itemHeight = 46; return TRUE; }
    if (msg == WM_DRAWITEM && drawButton(lp)) return TRUE;
    if (msg == WM_DRAWITEM && wp == 1) {
        auto* item = (DRAWITEMSTRUCT*)lp; if (item->itemID == (UINT)-1) return TRUE;
        bool selected = item->itemState & ODS_SELECTED;
        HBRUSH brush = CreateSolidBrush(selected ? selectionColor : canvasColor); FillRect(item->hDC, &item->rcItem, background);
        HGDIOBJ previousBrush = SelectObject(item->hDC, brush), previousPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
        RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom, corner, corner);
        SelectObject(item->hDC, previousBrush); SelectObject(item->hDC, previousPen); DeleteObject(brush);
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
            if (reply->value("disconnected", false)) { bridgeAlive = false; SetWindowTextW(status, L"Conexão encerrada — use Reconectar"); }
            else {
                message(reply->value("error", "Não foi possível concluir a operação."));
                requestedPage = pageIndex; SendMessageW(nav, LB_SETCURSEL, pageIndex, 0);
                if(cursorPage()) {
                    auto query=text(cursorQuery); renderPage(false); SetWindowTextW(cursorQuery,query.c_str());
                }
                SetWindowTextW(status,L"Operação não concluída. Tente novamente.");
            }
            return 0;
        }
        snapshot = *reply;
        bubbleSounds = snapshot.value("bubbleSounds", true);
        darkTheme = true;
        corner = 8 + 8 * std::clamp(snapshot.value("appCorner", 2), 0, 3);
        auto selectedFont = snapshot.value("appFont", std::string("SegoeUI"));
        if (selectedFont != currentFont) {
            const wchar_t* face = selectedFont == "Arial" ? L"Arial" : selectedFont == "Consolas" ? L"Consolas" : selectedFont == "Minecraft" ? L"Minecraft" : L"Segoe UI";
            HFONT old = font, oldTitle = titleFont;
            font = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,face);
            titleFont = CreateFontW(-26,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,face);
            EnumChildWindows(windowHandle, [](HWND child, LPARAM) -> BOOL { SendMessageW(child,WM_SETFONT,(WPARAM)font,TRUE); return TRUE; },0);
            SendMessageW(heading,WM_SETFONT,(WPARAM)titleFont,TRUE);
            DeleteObject(old); DeleteObject(oldTitle); currentFont = selectedFont;
        }
        canvasColor = darkTheme ? RGB(0,0,0) : RGB(243,243,243);
        inkColor = darkTheme ? RGB(240,240,240) : RGB(24,24,24);
        cardColor = darkTheme ? RGB(14,14,14) : RGB(255,255,255);
        selectionColor = darkTheme ? RGB(28,28,28) : RGB(225,225,225);
        HBRUSH oldBackground = background, oldCard = cardBrush;
        background = CreateSolidBrush(canvasColor); cardBrush = CreateSolidBrush(cardColor);
        SetClassLongPtrW(windowHandle, GCLP_HBRBACKGROUND, (LONG_PTR)background);
        SetClassLongPtrW(panel, GCLP_HBRBACKGROUND, (LONG_PTR)background);
        DeleteObject(oldBackground); DeleteObject(oldCard);
        BOOL dark = darkTheme; DwmSetWindowAttribute(windowHandle, 20, &dark, sizeof(dark));
        EnumChildWindows(windowHandle, [](HWND child, LPARAM) -> BOOL {
            SetWindowTheme(child, darkTheme ? L"DarkMode_Explorer" : L"Explorer", nullptr); roundControl(child); return TRUE;
        }, 0);
        InvalidateRect(windowHandle, nullptr, TRUE);
        rebuilding = true; SendMessageW(nav, LB_RESETCONTENT, 0, 0);
        for (const auto& p : snapshot["pages"]) { auto name = wide(p["title"].get<std::string>()); SendMessageW(nav, LB_ADDSTRING, 0, (LPARAM)name.c_str()); }
        pageIndex = std::min(requestedPage, std::max(0, (int)snapshot["pages"].size() - 1));
        SendMessageW(nav, LB_SETCURSEL, pageIndex, 0); rebuilding = false;
        renderPage(); setBusy(false);
        SetWindowTextW(installButton,snapshot.value("installed",false)?L"Reconectar":L"Instalar cayb trap");
        SetWindowTextW(status, !snapshot.value("installed", false) ? L"Instale o cayb trap para continuar." : snapshot.value("dirty", false) ? L"Alterações pendentes — clique em Salvar" : L"Pronto para usar");
        auto notice = snapshot.value("notice", std::string{});
        if (!notice.empty()) SetWindowTextW(status, wide(notice).c_str());
        return 0;
    }
    if (msg == RestartMessage) {
        if (engineProcess) { CloseHandle(engineProcess); engineProcess = nullptr; }
        startBridge(); return 0;
    }
    if (msg == WM_COMMAND && !rebuilding) {
        int id = LOWORD(wp);
        if ((id != 1 && HIWORD(wp) == BN_CLICKED) || (id == 1 && HIWORD(wp) == LBN_SELCHANGE)) bubbleClick();
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
        else if (!busy && (id == 12 || id == 13)) {
            if (engineProcess) AllowSetForegroundWindow(GetProcessId(engineProcess));
            send({{"op", "launch"}, {"mode", id == 12 ? "Player" : "Studio"}, {"changes", changes()}});
        }
        else if (!busy && id == 1 && HIWORD(wp) == LBN_SELCHANGE) {
            requestedPage = (int)SendMessageW(nav, LB_GETCURSEL, 0, 0);
            send({{"op", "apply"}, {"changes", changes()}});
        }
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) return SendMessageW(panel, msg, wp, lp);
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORLISTBOX) { SetTextColor((HDC)wp, inkColor); SetBkColor((HDC)wp, canvasColor); return (LRESULT)background; }
    if (msg == WM_CLOSE) {
        if (busy && bridgeAlive) { MessageBoxW(h, L"Aguarde a operação terminar antes de fechar.", L"cayb trap", MB_OK); return 0; }
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
    HRSRC fontResource = FindResourceW(instance, MAKEINTRESOURCEW(4), RT_RCDATA);
    if (fontResource) { DWORD count = 0; AddFontMemResourceEx(LockResource(LoadResource(instance,fontResource)), SizeofResource(instance,fontResource), nullptr, &count); }
    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    titleFont = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    Gdiplus::GdiplusStartupInput imaging;
    if(Gdiplus::GdiplusStartup(&imagingToken,&imaging,nullptr)!=Gdiplus::Ok)imagingToken=0;
    background = CreateSolidBrush(canvasColor); cardBrush = CreateSolidBrush(cardColor);
    WNDCLASSW wc{}; wc.hInstance = instance; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = background;
    wc.lpfnWndProc = PanelProc; wc.lpszClassName = L"CaybPanel"; RegisterClassW(&wc);
    wc.lpfnWndProc=CursorPreviewProc;wc.lpszClassName=L"CaybCursorPreview";RegisterClassW(&wc);
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
    if(imagingToken)Gdiplus::GdiplusShutdown(imagingToken);
    return (int)msg.wParam;
}
