#include "dream/runtime/sched/scheduler.h"

#include <chrono>
#include <utility>

namespace dream::sched {

int Scheduler::add(std::string name, Callback cb) {
    events_.push_back(Event{std::move(name), std::move(cb), {}});
    deadlines_.push_back(kNever);
    return static_cast<int>(events_.size() - 1);
}

void Scheduler::request(int id, std::uint64_t cycles) {
    deadlines_[static_cast<std::size_t>(id)] = cycles == kNever ? kNever : now_ + cycles;
}

std::uint64_t Scheduler::next_deadline() const noexcept {
    std::uint64_t best = kNever;
    for (const std::uint64_t d : deadlines_)
        if (d < best)
            best = d;
    return best;
}

void Scheduler::advance_to(std::uint64_t target) {
    for (;;) {
        const std::size_t n = deadlines_.size();
        std::size_t idx = n;
        std::uint64_t best = kNever;
        for (std::size_t i = 0; i < n; ++i) {
            if (deadlines_[i] < best) {
                best = deadlines_[i];
                idx = i;
            }
        }
        if (idx == n || best > target)
            break;
        const std::uint64_t late = now_ > best ? now_ - best : 0;
        if (now_ < best)
            now_ = best;
        deadlines_[idx] = kNever;  // the callback may re-arm
        // Callbacks may throw (a closed window stops the run from the vblank event): the guard
        // keeps the nesting depth right whatever leaves the callback.
        struct Depth {
            int& d;
            explicit Depth(int& v) : d(++v) {}
            ~Depth() { --d; }
        } depth(depth_);
        if (profile && depth_ == 1) {
            const auto t0 = std::chrono::steady_clock::now();
            struct Time {
                Stat& st;
                std::chrono::steady_clock::time_point t0;
                ~Time() {
                    st.ns += static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count());
                    ++st.calls;
                }
            } time{events_[idx].stat, t0};
            events_[idx].cb(now_, late);
        } else {
            events_[idx].cb(now_, late);
        }
    }
    if (target > now_)
        now_ = target;
}

}  // namespace dream::sched
