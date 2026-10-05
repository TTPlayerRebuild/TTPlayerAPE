#include "common.h"
namespace ttp::ape {
HMODULE module{};
StandardContent standard_content() {
    for (const auto name :
         {L"ttpctrl.dll", L"soundcore.dll", static_cast<const wchar_t *>(nullptr)})
        if (auto h = GetModuleHandleW(name))
            if (auto p = GetProcAddress(h, "CreateStdContent"))
                return reinterpret_cast<StandardContent>(p);
    return nullptr;
}
template <class Interface> class Factory final : public Interface {
    LONG references_{1};
    static constexpr bool encoder = std::is_same_v<Interface, EncoderCreator>;

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, encoder ? cat_encoder : cat_reader))
            return E_NOINTERFACE;
        *out = static_cast<Interface *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = InterlockedDecrement(&references_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Create(void **out) override {
        return encoder ? make_encoder(out) : make_reader(out);
    }
    HRESULT STDMETHODCALLTYPE Name(wchar_t **out) override {
        return text(encoder ? L"Monkey's Audio(APE) 编码器(mac v13.27)" : L"APE Reader", out);
    }
    HRESULT STDMETHODCALLTYPE Extensions(wchar_t **out) {
        return text(L"Monkey's Audio 音频文件(*.ape;*.mac;*.apl)", out);
    }
    HRESULT STDMETHODCALLTYPE Extension(wchar_t **out) { return text(L"ape", out); }
    HRESULT STDMETHODCALLTYPE ConfigAvailable() { return S_OK; }
    HRESULT STDMETHODCALLTYPE Configure(HWND owner) { return configure(owner); }
};
class SoundAddIn final : public AddIn {
    LONG references_{1};

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!same(iid, IID_IUnknown) && !same(iid, iid_addin))
            return E_NOINTERFACE;
        *out = static_cast<AddIn *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = InterlockedDecrement(&references_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Enum(DWORD index, GUID *cat, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (!cat)
            return E_POINTER;
        if (index > 1)
            return E_INVALIDARG;
        return protect([&] {
            if (index) {
                *cat = cat_encoder;
                *out = static_cast<EncoderCreator *>(new Factory<EncoderCreator>);
            } else {
                *cat = cat_reader;
                *out = static_cast<ReaderCreator *>(new Factory<ReaderCreator>);
            }
            return S_OK;
        });
    }
};
} // namespace ttp::ape
extern "C" HRESULT WINAPI ttpGetSoundAddIn(void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return ttp::ape::protect([&] {
        *out = static_cast<ttp::ape::AddIn *>(new ttp::ape::SoundAddIn);
        return S_OK;
    });
}
extern "C" int WINAPI TTPlayerFillWaveFormatEx(WAVEFORMATEX *f, int rate, WORD bits,
                                               WORD channels) {
    if (!f)
        return -1;
    *f = {};
    f->wFormatTag = WAVE_FORMAT_PCM;
    f->nChannels = channels;
    f->nSamplesPerSec = rate;
    f->wBitsPerSample = bits;
    f->nBlockAlign = WORD((bits / 8) * channels);
    f->nAvgBytesPerSec = f->nBlockAlign * rate;
    return 0;
}
extern "C" int WINAPI TTPlayerFillWaveHeader(void *p, DWORD bytes, const WAVEFORMATEX *f,
                                             DWORD tail) {
    if (!p || !f)
        return -1;
    auto b = static_cast<BYTE *>(p);
    memcpy(b, "RIFF", 4);
    DWORD n = bytes + 36 + tail;
    memcpy(b + 4, &n, 4);
    memcpy(b + 8, "WAVEfmt ", 8);
    n = 16;
    memcpy(b + 16, &n, 4);
    memcpy(b + 20, f, 16);
    memcpy(b + 36, "data", 4);
    memcpy(b + 40, &bytes, 4);
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ttp::ape::module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
