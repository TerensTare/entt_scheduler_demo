
#pragma once

#include <queue>

#include "fire_and_forget.hpp"
#include "task.hpp"
#include "function_ref.hpp"

// TODO: it might help returning `dt` from `co_await scheduler.schedule()`
struct scheduler final
{
    scheduler() = default;

    scheduler(scheduler const &) = delete;
    scheduler &operator=(scheduler const &) = delete;

    scheduler(scheduler &&) = delete;
    scheduler &operator=(scheduler &&) = delete;

    inline ~scheduler()
    {
        while (!ready.empty())
        {
            auto hnd = ready.front();
            ready.pop();
            hnd.destroy();
        }

        while (!submitted.empty())
        {
            auto hnd = submitted.front();
            submitted.pop();
            hnd.destroy();
        }

        while (!sleeping.empty())
        {
            auto [hnd, _] = sleeping.top();
            sleeping.pop();
            hnd.destroy();
        }
    }

    // for use by events only, `co_await scheduler.schedule()` instead for coroutines
    inline void schedule(std::coroutine_handle<> hnd) { submitted.push(hnd); }

    // this really makes sense for events, ie. scheduling a listener the same frame
    // that the event happens, so that's why there is no `co_await` version.
    // Since there's not really a use case for coroutines themselves (co_await this would just schedule to run later in the frame),
    // ^ there is no `co_await` version.
    inline void schedule_now(std::coroutine_handle<> hnd) { ready.push(hnd); }
    inline void sleep(std::coroutine_handle<> hnd, int ticks) { sleeping.push({hnd, time_now + ticks}); }

    // for use by coroutines
    [[nodiscard]]
    inline auto schedule() noexcept
    {
        struct awaiter final
        {
            scheduler *sched;

            static constexpr bool await_ready() noexcept { return false; }

            inline auto await_suspend(std::coroutine_handle<> hnd)
            {
                sched->schedule(hnd);
                return sched->pick_next();
            }

            static constexpr void await_resume() noexcept {}
        };

        return awaiter{this};
    }

    // for use by coroutines
    [[nodiscard]]
    inline auto sleep(int ticks) noexcept
    {
        struct awaiter final
        {
            scheduler *sched;
            int ticks;

            static constexpr bool await_ready() noexcept { return false; }

            inline auto await_suspend(std::coroutine_handle<> hnd)
            {
                sched->sleep(hnd, ticks);
                return sched->pick_next();
            }

            static constexpr void await_resume() noexcept {}
        };

        return awaiter{this, ticks};
    }

    // for use when the scheduler is created, the scheduler will stop when the timer expires
    inline fire_and_forget timeout(int ticks)
    {
        co_await sleep(ticks);
        done = true;
    }

    inline void update()
    {
        ++time_now;

        while (!sleeping.empty())
        {
            auto [hnd, when] = sleeping.top();
            if (time_now < when)
                break;

            sleeping.pop();
            submitted.push(hnd);
        }

        std::swap(ready, submitted);
        // TODO: you are double checking here, is there a nicer way?
        while (!done && !ready.empty())
        {
            auto hnd = ready.front();
            ready.pop();
            hnd.resume();
        }

        // just making sure
        // NOTE: this does not take events into account (but should it?)
        done = done || (submitted.empty() && sleeping.empty());
    }

    // TODO: do we really need this?
    // Use this function when you care to know when the "waited" scheduler finishes
    // Use `wait_for_other` otherwise
    [[nodiscard]]
    inline task<> wait_on(scheduler &sched) noexcept
    {
        while (!done)
        {
            co_await sched.schedule();
            update();
        }
    }

    // TODO: do we really need this?
    // TODO: need a better name; you can't await the return of this so why call it "wait_*"?
    // use this function if you don't care when the "waited" scheduler finishes
    // Use `wait_for_other` otherwise
    inline fire_and_forget wait_for_other(scheduler &other) noexcept
    {
        co_await other.wait_on(*this);
    }

    // Spawn a new scheduler an make it `wait_on(*this)`.
    // If running from main, wrap this in a `fire_and_forget` coroutine first
    // Or make a scheduler yourself, submit your corutines there and call `this.wait_for_other(child)`
    [[nodiscard]]
    inline task<> scope(function_ref<void(scheduler &)> inner_scope)
    {
        scheduler inner;
        inner_scope(inner);
        co_await inner.wait_on(*this);
    }

    // for use only by awaitables (eg. events)
    [[nodiscard("Has side effects")]]
    inline std::coroutine_handle<> pick_next()
    {
        if (done || ready.empty())
            return std::noop_coroutine();

        auto top = ready.front();
        ready.pop();
        return top;
    }

    // TODO: does this need to be an event?
    bool done = false;

private:
    struct waiting final
    {
        std::coroutine_handle<> hnd;
        int when_ready;

        friend constexpr bool operator>(waiting const &lhs, waiting const &rhs) noexcept
        {
            return lhs.when_ready > rhs.when_ready;
        }
    };

    int time_now = 0;
    std::queue<std::coroutine_handle<>> ready, submitted;
    std::priority_queue<waiting, std::vector<waiting>, std::greater<>> sleeping;
};
