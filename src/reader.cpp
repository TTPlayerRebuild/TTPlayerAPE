#include "stream_io.h"
#include <shlwapi.h>
namespace ttp::ape {
using Info = APE::IAPEInfo;
// Bound allocation-related header fields before the SDK builds its seek table.
static void preflight(Input &in) {
    require(in.seekable, STG_E_INVALIDFUNCTION);
    in.at(0);
    BYTE tag[10]{};
    in.exact(tag, 10);
    uint64_t offset = 0;
    if (!memcmp(tag, "ID3", 3)) {
        require(!(tag[6] & 128) && !(tag[7] & 128) && !(tag[8] & 128) && !(tag[9] & 128));
        offset = 10 + (uint32_t(tag[6]) << 21) + (uint32_t(tag[7]) << 14) +
                 (uint32_t(tag[8]) << 7) + tag[9] + ((tag[3] == 4 && (tag[5] & 16)) ? 10 : 0);
    }
    require(offset < in.size);
    in.at(offset);
    Bytes scan(size_t(std::min<uint64_t>(in.size - offset, 1024 * 1024 + 80)));
    in.exact(scan.data(), DWORD(scan.size()));
    size_t i = 0;
    for (; i + 8 <= scan.size(); ++i)
        if (!memcmp(scan.data() + i, "MAC ", 4) || !memcmp(scan.data() + i, "MACF", 4))
            break;
    require(i + 8 <= scan.size());
    offset += i;
    auto version = le16(scan.data() + i + 4);
    require(version >= 3000 && (version <= 3990 || version == 4110));
    if (version >= 3980) {
        auto d = read_at(in.stream.p, offset, 52);
        auto descriptor = le32(d.data() + 8), header = le32(d.data() + 12),
             table = le32(d.data() + 16);
        require(descriptor >= 52 && descriptor <= 1024 * 1024 && header >= 24 &&
                header <= 1024 * 1024 && table <= 16 * 1024 * 1024 && table % 4 == 0);
        require(offset + descriptor + uint64_t(header) + table <= in.size);
        auto h = read_at(in.stream.p, offset + descriptor, 24);
        auto blocks = le32(h.data() + 4), last = le32(h.data() + 8), frames = le32(h.data() + 12);
        auto bits = le16(h.data() + 16), channels = le16(h.data() + 18);
        auto rate = le32(h.data() + 20);
        require(blocks && blocks <= 4 * 1024 * 1024 && last <= blocks && frames <= table / 4);
        require((bits == 8 || bits == 16 || bits == 24 || bits == 32) && channels >= 1 &&
                channels <= 32 && rate >= 1 && rate <= 1536000);
        require(uint64_t(blocks) * channels * (bits / 8) <= 256 * 1024 * 1024);
    } else {
        auto h = read_at(in.stream.p, offset, 32);
        auto channels = le16(h.data() + 10);
        auto rate = le32(h.data() + 12), frames = le32(h.data() + 24);
        auto flags = le16(h.data() + 8);
        require(channels >= 1 && channels <= 2 && rate && rate <= 1536000 && frames &&
                frames <= 4 * 1024 * 1024);
        uint64_t extra = offset + 32 + ((flags & APE_FORMAT_FLAG_HAS_PEAK_LEVEL) ? 4 : 0);
        uint64_t entries = frames;
        if (flags & APE_FORMAT_FLAG_HAS_SEEK_ELEMENTS) {
            auto b = read_at(in.stream.p, extra, 4);
            entries = le32(b.data());
        }
        require(entries >= frames && entries <= 4 * 1024 * 1024 && entries <= in.size / 4);
    }
    in.at(0);
}
static std::wstring trim(std::wstring s) {
    auto a = s.find_first_not_of(L" \t\r\n");
    if (a == s.npos)
        return {};
    auto b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
static uint64_t block_number(const std::wstring &s) {
    require(!s.empty());
    uint64_t n = 0;
    for (auto c : s) {
        require(c >= L'0' && c <= L'9' && n <= (uint64_t(INT64_MAX) - uint64_t(c - L'0')) / 10);
        n = n * 10 + c - L'0';
    }
    return n;
}

class ApeReader final : public Reader, public Metadata, public Thumbnail {
    LONG references_{1};
    std::unique_ptr<StreamIO> source_, image_;
    std::unique_ptr<Info> info_;
    std::unique_ptr<APE::IAPEDecompress> decoder_;
    ComPtr<Metadata> metadata_;
    ComPtr<Thumbnail> pictures_;
    WAVEFORMATEX format_{};
    DWORD flags_{}, rate_{}, version_{}, bitrate_{};
    uint64_t start_{}, end_{}, position_{};
    HRESULT decode_failure_{S_OK};
    bool opened_{}, apl_{};
    StreamIO &audio() { return image_ ? *image_ : *source_; }
    // Original 601023CE releases the input stream before the tag object. The
    // host's deferred writer may reopen the file and needs its read lock gone.
    void close() {
        decoder_.reset();
        info_.reset();
        image_.reset();
        source_.reset();
        pictures_.reset();
        metadata_.reset();
        opened_ = false;
    }
    void parse_apl() {
        auto &in = source_->input;
        in.at(0);
        BYTE head[24]{};
        auto n = DWORD(std::min<uint64_t>(in.size, sizeof(head)));
        in.exact(head, n);
        in.at(0);
        bool utf16 = n >= 2 && head[0] == 0xff && head[1] == 0xfe;
        size_t bom = n >= 3 && head[0] == 0xef && head[1] == 0xbb && head[2] == 0xbf ? 3 : 0;
        if (!utf16 && (n < 20 + bom || memcmp(head + bom, "[Monkey's Audio Image", 20)))
            return;
        require(in.size <= 1024 * 1024 && !in.path.empty());
        auto bytes = read_at(in.stream.p, 0, size_t(in.size));
        // APL files may carry binary APEv2/ID3v1 tags after the link text.
        // Exclude them before deciding whether the path is UTF-8 or ANSI.
        if (bytes.size() >= 128 && !memcmp(bytes.data() + bytes.size() - 128, "TAG", 3))
            bytes.resize(bytes.size() - 128);
        if (bytes.size() >= 32 && !memcmp(bytes.data() + bytes.size() - 32, "APETAGEX", 8)) {
            auto tag_size = le32(bytes.data() + bytes.size() - 20);
            require(tag_size >= 32 && tag_size <= bytes.size());
            bytes.resize(bytes.size() - tag_size);
            if (bytes.size() >= 32 && !memcmp(bytes.data() + bytes.size() - 32, "APETAGEX", 8))
                bytes.resize(bytes.size() - 32);
        }
        require(bytes.size() > (utf16 ? 2 : bom));
        std::wstring body, file;
        if (utf16) {
            require(bytes.size() % 2 == 0);
            body.resize((bytes.size() - 2) / 2);
            memcpy(body.data(), bytes.data() + 2, bytes.size() - 2);
        } else {
            auto data = reinterpret_cast<const char *>(bytes.data() + bom);
            int size = int(bytes.size() - bom);
            UINT cp = CP_UTF8;
            int count = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, data, size, nullptr, 0);
            if (!count) {
                cp = CP_ACP;
                count = MultiByteToWideChar(cp, 0, data, size, nullptr, 0);
            }
            require(count > 0);
            body.resize(count);
            MultiByteToWideChar(cp, 0, data, size, body.data(), count);
        }
        require(body.find(L"[Monkey's Audio Image Link File]") == 0);
        bool have_start = false, have_end = false;
        size_t at = 0;
        while (at < body.size()) {
            auto next = body.find(L'\n', at);
            auto line = trim(body.substr(at, next == body.npos ? body.size() - at : next - at));
            auto eq = line.find(L'=');
            if (eq != line.npos) {
                auto key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
                if (key == L"Image File") {
                    require(file.empty());
                    file = value;
                } else if (key == L"Start Block") {
                    require(!have_start);
                    start_ = block_number(value);
                    have_start = true;
                } else if (key == L"Finish Block") {
                    require(!have_end);
                    end_ = block_number(value);
                    have_end = true;
                }
            }
            if ((!file.empty() && have_start && have_end) || next == body.npos)
                break;
            at = next + 1;
        }
        require(!file.empty() && file.size() < 32700 && have_start && have_end && start_ <= end_);
        if (PathIsRelativeW(file.c_str())) {
            auto slash = in.path.find_last_of(L"/\\");
            require(slash != in.path.npos);
            file = in.path.substr(0, slash + 1) + file;
        }
        ComPtr<IStream> stream;
        check(SHCreateStreamOnFileEx(file.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE,
                                     nullptr, stream.put()));
        image_ = std::make_unique<StreamIO>(stream.p);
        apl_ = true;
    }
    void ensure_decoder() {
        require(opened_ && (flags_ & 1), E_UNEXPECTED);
        check(decode_failure_);
        if (decoder_)
            return;
        auto &io = audio();
        io.clear_error();
        io.input.at(0);
        int error = 0;
        auto p = CreateIAPEInfo(&error, &io);
        std::unique_ptr<Info> info(p);
        io.verify(error);
        require(!!info);
        // The full-range decoder uses 64-bit positions internally. APL range is
        // enforced here instead of truncating it to the SDK factory's int args.
        decoder_.reset(CreateIAPEDecompressEx2(info.release(), -1, -1, &error));
        io.verify(error);
        require(!!decoder_);
        decoder_->SetNumberOfThreads(1);
        if (start_ + position_)
            io.verify(decoder_->Seek(static_cast<APE::int64>(start_ + position_)));
    }
    template <class F> HRESULT metadata_call(F &&f, bool writing = false) {
        return protect([&]() -> HRESULT {
            require(opened_ && metadata_, E_NOINTERFACE);
            decoder_.reset();
            info_.reset();
            if (writing)
                check(decode_failure_);
            return f();
        });
    }
    template <class F> HRESULT picture_call(F &&f) {
        return protect([&]() -> HRESULT {
            require(opened_ && pictures_, E_NOINTERFACE);
            decoder_.reset();
            info_.reset();
            return f();
        });
    }

  public:
    ~ApeReader() { close(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (same(iid, IID_IUnknown) || same(iid, iid_reader))
            *out = static_cast<Reader *>(this);
        else if (same(iid, iid_metadata) && metadata_)
            *out = static_cast<Metadata *>(this);
        else if (same(iid, iid_thumbnail) && pictures_)
            *out = static_cast<Thumbnail *>(this);
        else
            return E_NOINTERFACE;
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
    HRESULT STDMETHODCALLTYPE Open(IStream *stream, DWORD flags) override {
        close();
        flags_ = flags;
        start_ = end_ = position_ = 0;
        apl_ = false;
        decode_failure_ = S_OK;
        auto hr = protect([&] {
            source_ = std::make_unique<StreamIO>(stream);
            parse_apl();
            auto &io = audio();
            preflight(io.input);
            int error = 0;
            info_.reset(CreateIAPEInfo(&error, &io));
            io.verify(error);
            require(!!info_);
            auto get = [&](Info::APE_INFO_FIELDS field) { return info_->GetInfo(field); };
            auto total = get(Info::APE_INFO_TOTAL_BLOCKS);
            require(total >= 0);
            if (apl_)
                require(end_ <= uint64_t(total));
            else
                end_ = uint64_t(total);
            rate_ = DWORD(get(Info::APE_INFO_SAMPLE_RATE));
            require(rate_);
            auto bits = get(Info::APE_INFO_BITS_PER_SAMPLE),
                 channels = get(Info::APE_INFO_CHANNELS), align = get(Info::APE_INFO_BLOCK_ALIGN),
                 frame = get(Info::APE_INFO_BLOCKS_PER_FRAME);
            require((bits == 8 || bits == 16 || bits == 24 || bits == 32) && channels >= 1 &&
                    channels <= 32 && align == channels * bits / 8 && frame > 0 &&
                    frame * align <= 256 * 1024 * 1024);
            format_ = {};
            format_.wFormatTag = (get(Info::APE_INFO_FORMAT_FLAGS) & APE_FORMAT_FLAG_FLOATING_POINT)
                                     ? WAVE_FORMAT_IEEE_FLOAT
                                     : WAVE_FORMAT_PCM;
            format_.nChannels = WORD(channels);
            format_.nSamplesPerSec = rate_;
            format_.wBitsPerSample = WORD(bits);
            format_.nBlockAlign = WORD(align);
            format_.nAvgBytesPerSec = rate_ * DWORD(align);
            require(format_.wFormatTag != WAVE_FORMAT_IEEE_FLOAT || bits == 32);
            version_ = DWORD(get(Info::APE_INFO_FILE_VERSION));
            bitrate_ = dword(
                uint64_t(std::max<APE::int64>(0, get(Info::APE_INFO_AVERAGE_BITRATE))) * 1000);
            info_.reset();
            if (auto host = standard_content()) {
                source_->input.at(0);
                ULONGLONG bytes{};
                auto result = host(stream, 4, metadata_.put(), &bytes);
                if (FAILED(result))
                    metadata_.reset();
                if (metadata_)
                    metadata_->QueryInterface(iid_thumbnail,
                                              reinterpret_cast<void **>(pictures_.put()));
            }
            opened_ = true;
            if (flags & 1)
                ensure_decoder();
            return S_OK;
        });
        if (FAILED(hr))
            close();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Capabilities(DWORD *p) override {
        if (!p)
            return E_POINTER;
        if (!opened_)
            return E_UNEXPECTED;
        *p = source_->input.capabilities(false);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Duration(DWORD *p) override {
        if (!p)
            return E_POINTER;
        if (!opened_)
            return E_UNEXPECTED;
        *p = milliseconds(end_ - start_, rate_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Format(WAVEFORMATEX **p) override {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        if (!opened_)
            return E_UNEXPECTED;
        *p = static_cast<WAVEFORMATEX *>(CoTaskMemAlloc(sizeof(format_)));
        if (!*p)
            return E_OUTOFMEMORY;
        **p = format_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferSize(DWORD *p) override {
        if (!p)
            return E_POINTER;
        if (!opened_)
            return E_UNEXPECTED;
        *p = 1024 * format_.nBlockAlign;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CodecName(wchar_t **p) override {
        return protect([&] {
            if (!opened_)
                return E_UNEXPECTED;
            auto version = std::to_wstring(version_ / 1000) + L"." +
                           std::to_wstring(1000 + version_ % 1000).substr(1);
            if (version_ % 10 == 0)
                version.pop_back();
            return text(L"APE|Monkey's Audio " + version, p);
        });
    }
    HRESULT STDMETHODCALLTYPE Bitrate(DWORD *p) override {
        if (!p)
            return E_POINTER;
        if (!opened_)
            return E_UNEXPECTED;
        *p = bitrate_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BitDepth(WORD *p) override {
        if (!p)
            return E_POINTER;
        if (!opened_)
            return E_UNEXPECTED;
        *p = format_.wBitsPerSample;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCallback(IUnknown *p) override {
        return source_ ? source_->input.callback(p) : E_UNEXPECTED;
    }
    HRESULT STDMETHODCALLTYPE Start() override { return opened_ ? S_OK : E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE Stop() override { return opened_ ? S_OK : E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE Read(Buffer *buffer) override {
        return protect([&]() -> HRESULT {
            require(buffer, E_POINTER);
            check(buffer->SetLength(0));
            require(opened_ && (flags_ & 1), E_UNEXPECTED);
            check(decode_failure_);
            DWORD capacity = 0, unused = 0;
            BYTE *data = nullptr;
            check(buffer->Capacity(&capacity));
            check(buffer->Data(&data, &unused));
            require(capacity >= format_.nBlockAlign, E_INVALIDARG);
            require(data, E_POINTER);
            if (position_ >= end_ - start_)
                return S_FALSE;
            ensure_decoder();
            auto count =
                std::min<uint64_t>(capacity / format_.nBlockAlign, end_ - start_ - position_);
            APE::int64 got = 0;
            auto hr = protect([&] {
                auto result = decoder_->GetData(data, static_cast<APE::int64>(count), &got);
                audio().verify(result);
                require(got > 0 && uint64_t(got) <= count);
                return S_OK;
            });
            if (FAILED(hr)) {
                decode_failure_ = hr;
                return hr;
            }
            check(buffer->SetLength(DWORD(got) * format_.nBlockAlign));
            position_ += got;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Seek(DWORD *ms) override {
        return protect([&] {
            require(ms, E_POINTER);
            require(opened_ && (flags_ & 1), E_UNEXPECTED);
            DWORD duration = milliseconds(end_ - start_, rate_);
            uint64_t pos = *ms == 0          ? 0
                           : *ms >= duration ? end_ - start_
                                             : uint64_t(*ms) * rate_ / 1000;
            // Recreate after an error so corrupt buffered frames cannot poison a new seek.
            decoder_.reset();
            decode_failure_ = S_OK;
            position_ = pos;
            if (pos < end_ - start_) {
                auto hr = protect([&] {
                    ensure_decoder();
                    return S_OK;
                });
                if (FAILED(hr)) {
                    decode_failure_ = hr;
                    return hr;
                }
            }
            *ms = milliseconds(pos, rate_);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Count(DWORD *p) override {
        return metadata_call([&] { return metadata_->Count(p); });
    }
    HRESULT STDMETHODCALLTYPE At(DWORD i, wchar_t **k, wchar_t **v) override {
        return metadata_call([&] { return metadata_->At(i, k, v); });
    }
    HRESULT STDMETHODCALLTYPE Get(const char *k, wchar_t **v) override {
        return metadata_call([&] { return metadata_->Get(k, v); });
    }
    HRESULT STDMETHODCALLTYPE Set(const char *k, const wchar_t *v) override {
        return metadata_call([&] { return metadata_->Set(k, v); }, true);
    }
    HRESULT STDMETHODCALLTYPE PictureCount(DWORD *p) override {
        return picture_call([&] { return pictures_->PictureCount(p); });
    }
    HRESULT STDMETHODCALLTYPE MaximumBytes(DWORD *p) override {
        return picture_call([&] { return pictures_->MaximumBytes(p); });
    }
    HRESULT STDMETHODCALLTYPE MaximumCount(DWORD *p) override {
        return picture_call([&] { return pictures_->MaximumCount(p); });
    }
    HRESULT STDMETHODCALLTYPE PictureAt(DWORD i, const Picture **p) override {
        return picture_call([&] { return pictures_->PictureAt(i, p); });
    }
    HRESULT STDMETHODCALLTYPE ReplacePicture(DWORD i, const Picture *p, DWORD m) override {
        return picture_call([&] { return pictures_->ReplacePicture(i, p, m); });
    }
    HRESULT STDMETHODCALLTYPE AddPicture(const Picture *p) override {
        return picture_call([&] { return pictures_->AddPicture(p); });
    }
    HRESULT STDMETHODCALLTYPE RemovePicture(DWORD i) override {
        return picture_call([&] { return pictures_->RemovePicture(i); });
    }
};
HRESULT make_reader(void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return protect([&] {
        *out = static_cast<Reader *>(new ApeReader);
        return S_OK;
    });
}
} // namespace ttp::ape
