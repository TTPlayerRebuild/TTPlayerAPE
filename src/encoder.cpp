#include "stream_io.h"
#include <shlwapi.h>
#include <cmath>
#include <ks.h>
#include <ksmedia.h>
namespace ttp::ape {
class ApeEncoder final : public Encoder, public Metadata {
    LONG references_{1};
    enum class State { empty, ready, running, finished, failed } state_{State::empty};
    std::unique_ptr<StreamIO> io_;
    std::unique_ptr<APE::IAPECompress> encoder_;
    WAVEFORMATEX input_{}, output_{};
    Settings settings_;
    bool floating_{};
    HRESULT result_{S_OK};
    std::vector<std::pair<std::string, std::wstring>> tags_;
    void format(const WAVEFORMATEX *p) {
        require(p, E_POINTER);
        input_ = *p;
        WORD tag = p->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE) {
            require(p->cbSize >= 22, E_INVALIDARG);
            auto ex = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(p);
            if (same(ex->SubFormat, KSDATAFORMAT_SUBTYPE_PCM))
                tag = WAVE_FORMAT_PCM;
            else if (same(ex->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
                tag = WAVE_FORMAT_IEEE_FLOAT;
            else
                throw Failure{E_INVALIDARG};
            require(!ex->Samples.wValidBitsPerSample ||
                        ex->Samples.wValidBitsPerSample <= p->wBitsPerSample,
                    E_INVALIDARG);
        }
        require(tag == WAVE_FORMAT_PCM || tag == WAVE_FORMAT_IEEE_FLOAT, E_INVALIDARG);
        floating_ = tag == WAVE_FORMAT_IEEE_FLOAT;
        require(p->nChannels >= 1 && p->nChannels <= 32 && p->nSamplesPerSec >= 1 &&
                    p->nSamplesPerSec <= 1536000,
                E_INVALIDARG);
        require(floating_ ? (p->wBitsPerSample == 32 || p->wBitsPerSample == 64)
                          : (p->wBitsPerSample == 8 || p->wBitsPerSample == 16 ||
                             p->wBitsPerSample == 24 || p->wBitsPerSample == 32),
                E_INVALIDARG);
        require(p->nBlockAlign == p->nChannels * p->wBitsPerSample / 8, E_INVALIDARG);
        settings_ = settings();
        output_ = input_;
        output_.cbSize = 0;
        output_.wFormatTag = WAVE_FORMAT_PCM;
        output_.wBitsPerSample = WORD(settings_.output_bits ? settings_.output_bits
                                                            : (floating_ ? 16 : p->wBitsPerSample));
        output_.nBlockAlign = output_.nChannels * output_.wBitsPerSample / 8;
        output_.nAvgBytesPerSec = output_.nSamplesPerSec * output_.nBlockAlign;
    }
    double sample(const BYTE *p) const {
        if (floating_) {
            if (input_.wBitsPerSample == 64) {
                double n;
                memcpy(&n, p, 8);
                return n;
            }
            float n;
            memcpy(&n, p, 4);
            return n;
        }
        if (input_.wBitsPerSample == 8)
            return (int(*p) - 128) / 128.0;
        if (input_.wBitsPerSample == 16) {
            int16_t n;
            memcpy(&n, p, 2);
            return n / 32768.0;
        }
        if (input_.wBitsPerSample == 24) {
            int32_t n =
                int32_t((uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 24));
            return n / 2147483648.0;
        }
        int32_t n;
        memcpy(&n, p, 4);
        return n / 2147483648.0;
    }
    void convert(const BYTE *p, size_t samples, Bytes &out) const {
        size_t width = output_.wBitsPerSample / 8;
        out.resize(samples * width);
        const double scale = std::ldexp(1.0, output_.wBitsPerSample - 1);
        for (size_t i = 0; i < samples; ++i) {
            double v = sample(p + i * (input_.wBitsPerSample / 8));
            if (std::isnan(v))
                v = 0;
            v = std::max(-1.0, std::min(v, 1.0));
            // Original 60101C60 uses x87 FISTP (nearest, ties to even).
            // Make that result independent of a host's floating point mode.
            double scaled = std::max(-scale, std::min(scale - 1, v * scale));
            double lower = std::floor(scaled), fraction = scaled - lower;
            auto n = static_cast<int64_t>(lower);
            if (fraction > 0.5 || (fraction == 0.5 && (n & 1)))
                ++n;
            if (width == 1)
                out[i] = BYTE(n + 128);
            else
                for (size_t k = 0; k < width; ++k)
                    out[i * width + k] = BYTE(uint64_t(n) >> (k * 8));
        }
    }

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (same(iid, IID_IUnknown) || same(iid, iid_encoder) || same(iid, iid_stream_encoder))
            *out = static_cast<Encoder *>(this);
        else if (same(iid, iid_metadata))
            *out = static_cast<Metadata *>(this);
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
    HRESULT STDMETHODCALLTYPE OpenPath(const wchar_t *path, const WAVEFORMATEX *f) override {
        return protect([&] {
            require(path, E_POINTER);
            require(state_ == State::empty, E_UNEXPECTED);
            format(f);
            ComPtr<IStream> stream;
            check(SHCreateStreamOnFileEx(path, STGM_CREATE | STGM_READWRITE | STGM_SHARE_DENY_WRITE,
                                         0, TRUE, nullptr, stream.put()));
            return OpenStream(stream.p, f);
        });
    }
    HRESULT STDMETHODCALLTYPE OpenStream(IStream *stream, const WAVEFORMATEX *f) override {
        return protect([&] {
            require(state_ == State::empty, E_UNEXPECTED);
            format(f);
            auto io = std::make_unique<StreamIO>(stream);
            require(io->input.seekable, STG_E_INVALIDFUNCTION);
            io_ = std::move(io);
            state_ = State::ready;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Start() override {
        if (state_ != State::ready)
            return E_UNEXPECTED;
        auto hr = protect([&] {
            seek(io_->input.stream.p, 0);
            ULARGE_INTEGER size{};
            check(io_->input.stream->SetSize(size));
            int error = 0;
            encoder_.reset(CreateIAPECompress(&error));
            io_->verify(error);
            require(!!encoder_);
            encoder_->SetNumberOfThreads(1);
            APE::WAVEFORMATEX f;
            static_assert(sizeof(f) == sizeof(output_));
            memcpy(&f, &output_, sizeof(f));
            io_->verify(encoder_->StartEx(io_.get(), &f, false, MAX_AUDIO_BYTES_UNKNOWN,
                                          settings_.level, nullptr,
                                          CREATE_WAV_HEADER_ON_DECOMPRESSION));
            return S_OK;
        });
        state_ = SUCCEEDED(hr) ? State::running : State::failed;
        result_ = hr;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Write(Buffer *buffer, DWORD *reported) override {
        if (reported)
            *reported = 0;
        if (state_ != State::running)
            return state_ == State::failed ? result_ : E_UNEXPECTED;
        auto hr = protect([&] {
            require(buffer, E_POINTER);
            BYTE *data = nullptr;
            DWORD bytes = 0, capacity = 0;
            check(buffer->Data(&data, &bytes));
            check(buffer->Capacity(&capacity));
            require(bytes <= capacity && (!bytes || data), E_INVALIDARG);
            require(bytes % input_.nBlockAlign == 0, E_INVALIDARG);
            Bytes converted;
            DWORD offset = 0;
            while (offset < bytes) {
                DWORD count = std::min<DWORD>(bytes - offset, 16384 * input_.nBlockAlign);
                if (!floating_ && input_.wBitsPerSample == output_.wBitsPerSample)
                    io_->verify(encoder_->AddData(data + offset, count));
                else {
                    convert(data + offset, count / (input_.wBitsPerSample / 8), converted);
                    io_->verify(encoder_->AddData(converted.data(),
                                                  static_cast<APE::int64>(converted.size())));
                }
                offset += count;
                if (reported)
                    *reported = offset;
            }
            return S_OK;
        });
        if (FAILED(hr) && hr != E_INVALIDARG && hr != E_POINTER) {
            state_ = State::failed;
            result_ = hr;
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Finish() override {
        if (state_ == State::finished || state_ == State::failed)
            return result_;
        if (state_ != State::running)
            return E_UNEXPECTED;
        // Never retry finalization from Release, including failures in tag writes.
        state_ = State::failed;
        result_ = protect([&] {
            io_->verify(encoder_->Finish(nullptr, 0, 0));
            encoder_.reset();
            if (!tags_.empty()) {
                auto host = standard_content();
                require(host, E_NOINTERFACE);
                seek(io_->input.stream.p, 0);
                ComPtr<Metadata> metadata;
                ULONGLONG bytes = 0;
                check(host(io_->input.stream.p, 4, metadata.put(), &bytes));
                require(!!metadata, E_NOINTERFACE);
                for (auto &tag : tags_)
                    check(metadata->Set(tag.first.c_str(),
                                        tag.second.empty() ? nullptr : tag.second.c_str()));
                // Original host reopens the path on final Release. Drop our
                // file lock first, matching original encoder destruction.
                // Rebuilt hosts save immediately and report Set/Commit errors.
                check(io_->input.stream->Commit(STGC_DEFAULT));
                io_.reset();
                metadata.reset();
            }
            if (io_)
                check(io_->input.stream->Commit(STGC_DEFAULT));
            return S_OK;
        });
        if (SUCCEEDED(result_))
            state_ = State::finished;
        return result_;
    }
    HRESULT STDMETHODCALLTYPE FileExtension(wchar_t **p) override { return text(L"ape", p); }
    HRESULT STDMETHODCALLTYPE Count(DWORD *p) override {
        if (!p)
            return E_POINTER;
        *p = DWORD(tags_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE At(DWORD i, wchar_t **k, wchar_t **v) override {
        return protect([&] {
            require(k && v, E_POINTER);
            *k = *v = nullptr;
            require(i < tags_.size(), E_INVALIDARG);
            check(text(wide(tags_[i].first), k));
            auto hr = text(tags_[i].second, v);
            if (FAILED(hr)) {
                CoTaskMemFree(*k);
                *k = nullptr;
            }
            return hr;
        });
    }
    HRESULT STDMETHODCALLTYPE Get(const char *k, wchar_t **v) override {
        if (!k || !v)
            return E_POINTER;
        *v = nullptr;
        for (auto &t : tags_)
            if (!_stricmp(k, t.first.c_str()))
                return text(t.second, v);
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Set(const char *k, const wchar_t *v) override {
        return protect([&] {
            require(k && *k, E_INVALIDARG);
            require(state_ != State::finished && state_ != State::failed, E_UNEXPECTED);
            for (auto &t : tags_)
                if (!_stricmp(k, t.first.c_str())) {
                    t.second = v ? v : L"";
                    return S_OK;
                }
            tags_.emplace_back(k, v ? v : L"");
            return S_OK;
        });
    }
};
HRESULT make_encoder(void **out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return protect([&] {
        *out = static_cast<Encoder *>(new ApeEncoder);
        return S_OK;
    });
}
} // namespace ttp::ape
