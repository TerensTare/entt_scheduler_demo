
#pragma once

#include "circular.hpp"
#include "scheduler.hpp"

// A counter starting from `N` that can be decrement and waited to reach 0.
struct counter final
{
    explicit constexpr counter(size_t n) noexcept
        : current{n} {}

    counter(counter const &) = delete;
    counter &operator=(counter const &) = delete;

    counter(counter &&) = delete;
    counter &operator=(counter &&) = delete;

    inline void decrement()
    {
        if (--current == 0)
        {
            while (auto top = waiting.pop())
                top->sched->schedule_now(top->hnd);
        }
    }

    constexpr void reset(size_t n) noexcept
    {
        current = n;
        waiting.clear();
    }

    struct awaiter final
    {
        counter *cnt;
        scheduler *sched;
        std::coroutine_handle<> hnd;
        awaiter *next = nullptr;

        constexpr bool await_ready() const noexcept { return cnt->current == 0; }

        inline auto await_suspend(std::coroutine_handle<> hnd) noexcept
        {
            this->hnd = hnd;
            cnt->waiting.push(*this);
            return sched->pick_next();
        }

        static constexpr void await_resume() noexcept {}
    };
    // Wait for the counter to reach 0 and resume the awaiting coroutine on the given scheduler.
    [[nodiscard]]
    constexpr awaiter resume_on(scheduler &sched) noexcept
    {
        return awaiter{this, &sched};
    }

private:
    size_t current;
    circular<awaiter> waiting;

    friend awaiter;
};