// Worker threads shared by the renderers (3D rasterization and the host's large picture
// of the 2D engines).
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

namespace ntr {

// run(f) calls f(band) for every band, the calling thread doing band 0, and returns when
// all are done.
class BandPool {
public:
    explicit BandPool(int bands);
    int bands() const { return bands_; }
    template <class F>
    void run(F f) {
        if (bands_ == 1) {
            f(0);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            job_ = f;
            pending_ = bands_ - 1;
            generation_++;
        }
        start_.notify_all();
        f(0);
        std::unique_lock lock(mutex_);
        done_.wait(lock, [this] { return pending_ == 0; });
    }

private:
    void work(int band);

    int bands_;
    std::mutex mutex_;
    std::condition_variable start_, done_;
    std::function<void(int)> job_;
    uint64_t generation_ = 0;
    int pending_ = 0;
};

// NTR_3D_THREADS overrides the thread count (1 = render on the emulation thread only).
BandPool& band_pool();

}  // namespace ntr
