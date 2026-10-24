#include "pool.h"

#include <algorithm>
#include <cstdlib>
#include <thread>

namespace ntr {

BandPool::BandPool(int bands) : bands_(bands) {
    // Detached and never destroyed: the pool lives until the process exits.
    for (int i = 1; i < bands_; i++) std::thread([this, i] { work(i); }).detach();
}

void BandPool::work(int band) {
    uint64_t seen = 0;
    for (;;) {
        std::function<void(int)> job;
        {
            std::unique_lock lock(mutex_);
            start_.wait(lock, [&] { return generation_ != seen; });
            seen = generation_;
            job = job_;
        }
        job(band);
        std::lock_guard lock(mutex_);
        if (--pending_ == 0) done_.notify_one();
    }
}

BandPool& band_pool() {
    static BandPool* pool = new BandPool([] {
        int n = int(std::thread::hardware_concurrency());
        if (const char* env = std::getenv("NTR_3D_THREADS")) n = std::atoi(env);
        return std::clamp(n, 1, 16);
    }());
    return *pool;
}

}  // namespace ntr
