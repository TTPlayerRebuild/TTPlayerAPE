#include "common.h"
namespace ttp::ape {
static std::wstring ini_path() {
    std::wstring p(32768, L'\0');
    DWORD n = GetModuleFileNameW(module, p.data(), DWORD(p.size()));
    require(n && n < p.size());
    p.resize(n);
    auto dot = p.find_last_of(L'.');
    if (dot != std::wstring::npos)
        p.resize(dot);
    p += L".ini";
    return p;
}
Settings settings() {
    auto path = ini_path();
    Settings s;
    int level = GetPrivateProfileIntW(L"Compress", L"Level", 2000, path.c_str());
    if (level >= 1000 && level <= 5000 && level % 1000 == 0)
        s.level = level;
    int bits = GetPrivateProfileIntW(L"Compress", L"OutputBits", 0, path.c_str());
    if (bits == 16 || bits == 24 || bits == 32)
        s.output_bits = bits;
    return s;
}
static INT_PTR CALLBACK dialog(HWND h, UINT message, WPARAM w, LPARAM) {
    HRESULT result = protect([&]() -> HRESULT {
        if (message == WM_INITDIALOG) {
            auto s = settings();
            for (auto name : {L"快速", L"标准（默认）", L"高", L"特高", L"极高"})
                SendDlgItemMessageW(h, 100, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
            SendDlgItemMessageW(h, 100, CB_SETCURSEL, s.level / 1000 - 1, 0);
            for (auto name : {L"自动：整数保持原位深；浮点转为 16 位", L"16 位整数", L"24 位整数",
                              L"32 位整数"})
                SendDlgItemMessageW(h, 101, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
            SendDlgItemMessageW(h, 101, CB_SETCURSEL, s.output_bits ? s.output_bits / 8 - 1 : 0, 0);
            return S_OK;
        }
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
            EndDialog(h, IDCANCEL);
            return S_OK;
        }
        if (message == WM_COMMAND && LOWORD(w) == IDOK) {
            int level = int(SendDlgItemMessageW(h, 100, CB_GETCURSEL, 0, 0)),
                bits = int(SendDlgItemMessageW(h, 101, CB_GETCURSEL, 0, 0));
            require(level >= 0 && level <= 4 && bits >= 0 && bits <= 3);
            auto p = ini_path();
            auto l = std::to_wstring((level + 1) * 1000);
            auto b = std::to_wstring(bits ? (bits + 1) * 8 : 0);
            require(
                WritePrivateProfileStringW(L"Compress", L"Level", l.c_str(), p.c_str()) &&
                    WritePrivateProfileStringW(L"Compress", L"OutputBits", b.c_str(), p.c_str()),
                HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
            EndDialog(h, IDOK);
            return S_OK;
        }
        return S_FALSE;
    });
    if (FAILED(result))
        MessageBoxW(h, L"无法保存编码设置，请检查插件目录的写入权限。", L"APE 编码器",
                    MB_OK | MB_ICONERROR);
    return result == S_FALSE ? FALSE : TRUE;
}
HRESULT configure(HWND owner) {
    return protect([&] {
        auto r = DialogBoxParamW(module, MAKEINTRESOURCEW(100), owner, dialog, 0);
        return r == -1 ? HRESULT_FROM_WIN32(GetLastError()) : r == IDOK ? S_OK : S_FALSE;
    });
}
} // namespace ttp::ape
