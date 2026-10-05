#pragma once
#include "io.h"
#include "All.h"
#include "IAPEIO.h"
#include "MACLib.h"

namespace ttp::ape {
// MAC workers may call back on another thread. Serialize stream positioning and
// preserve the first HRESULT; the SDK's integer errors cannot carry it.
class StreamIO final : public APE::IAPEIO {
    CRITICAL_SECTION lock_{};
    volatile LONG failure_{S_OK};
    struct Guard {
        CRITICAL_SECTION &c;
        Guard(CRITICAL_SECTION &v) : c(v) { EnterCriticalSection(&c); }
        ~Guard() { LeaveCriticalSection(&c); }
    };
    template <class F> int run(F &&f) noexcept {
        Guard guard(lock_);
        HRESULT hr = protect([&] {
            f();
            return S_OK;
        });
        if (FAILED(hr))
            InterlockedCompareExchange(&failure_, hr, S_OK);
        return FAILED(hr) ? ERROR_IO_READ : 0;
    }

  public:
    Input input;
    explicit StreamIO(IStream *s) {
        InitializeCriticalSection(&lock_);
        auto hr = protect([&] {
            input.open(s);
            return S_OK;
        });
        if (FAILED(hr)) {
            DeleteCriticalSection(&lock_);
            throw Failure{hr};
        }
    }
    ~StreamIO() { DeleteCriticalSection(&lock_); }
    HRESULT error() const { return failure_; }
    void clear_error() { InterlockedExchange(&failure_, S_OK); }
    void verify(APE::int64 code) const {
        check(error());
        require(code == 0, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    }
    int Open(const APE::str_utfn *, bool) override { return ERROR_UNDEFINED; }
    int Close() override { return 0; }
    int Read(void *p, APE::int64 n, APE::int64 *got) override {
        if (got)
            *got = 0;
        return run([&] {
            require(n >= 0 && (p || !n), E_INVALIDARG);
            auto *b = static_cast<BYTE *>(p);
            APE::int64 total = 0;
            while (total < n) {
                auto count = ULONG(std::min<APE::int64>(n - total, 1024 * 1024));
                auto r = input.read(b + total, count);
                total += r;
                if (!r)
                    break;
            }
            if (got)
                *got = total;
        });
    }
    int Write(const void *p, APE::int64 n, APE::int64 *got) override {
        if (got)
            *got = 0;
        return run([&] {
            require(n >= 0 && (p || !n), E_INVALIDARG);
            const auto *b = static_cast<const BYTE *>(p);
            APE::int64 total = 0;
            while (total < n) {
                ULONG count = ULONG(std::min<APE::int64>(n - total, 1024 * 1024)), w = 0;
                check(input.stream->Write(b + total, count, &w));
                require(w == count, STG_E_WRITEFAULT);
                total += w;
            }
            if (got)
                *got = total;
        });
    }
    int Seek(APE::int64 p, APE::SeekMethod method) override {
        return run([&] {
            require(method >= 0 && method <= 2, E_INVALIDARG);
            LARGE_INTEGER n{};
            n.QuadPart = p;
            check(input.stream->Seek(n, DWORD(method), nullptr));
        });
    }
    int Create(const APE::str_utfn *) override { return ERROR_UNDEFINED; }
    int Delete() override { return ERROR_UNDEFINED; }
    int SetEOF() override {
        return run([&] {
            ULARGE_INTEGER n{};
            n.QuadPart = tell(input.stream.p);
            check(input.stream->SetSize(n));
        });
    }
    unsigned char *GetBuffer(int *n) override {
        if (n)
            *n = 0;
        return nullptr;
    }
    APE::int64 GetPosition() override {
        APE::int64 p = -1;
        run([&] {
            auto n = tell(input.stream.p);
            require(n <= INT64_MAX);
            p = static_cast<APE::int64>(n);
        });
        return p;
    }
    APE::int64 GetSize() override {
        APE::int64 p = -1;
        run([&] {
            auto n = stream_size(input.stream.p);
            require(n <= INT64_MAX);
            p = static_cast<APE::int64>(n);
        });
        return p;
    }
};
} // namespace ttp::ape
