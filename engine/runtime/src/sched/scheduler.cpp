#include "dream/runtime/sched/scheduler.h"

#include <chrono>
#include <utility>

namespace dream::sched {

int Scheduler::add(std::string name, Callback cb) {
    events_.push_back(Event{std::move(name), std::move(cb), kNever});
    return static_cast<int>(events_.size() - 1);
}

void Scheduler::request(int id, std::uint64_t cycles) {
    auto& e = events_[static_cast<std::size_t>(id)];
    e.deadline = cycles == kNever ? kNever : now_ + cycles;
}

std::uint64_t Scheduler::next_deadline() const noexcept {
    std::uint64_t best = kNever;
    for (const auto& e : events_)
        if (e.deadline < best)
            best = e.deadline;
    return best;
}

void Scheduler::advance_to(std::uint64_t target) {
    for (;;) {
        std::size_t idx = events_.size();
        std::uint64_t best = kNever;
        for (std::size_t i = 0; i < events_.size(); ++i) {
            if (events_[i].deadline < best) {
                best = events_[i].deadline;
                idx = i;
            }
        }
        if (idx == events_.size() || best > target)
            break;
        const std::uint64_t late = now_ > best ? now_ - best : 0;
        if (now_ < best)
            now_ = best;
        events_[idx].deadline = kNever;  // the callback may re-arm
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
