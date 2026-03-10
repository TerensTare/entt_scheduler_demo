
#pragma once

#include <queue>

#include "circular.hpp"
#include "fire_and_forget.hpp"
#include "function_ref.hpp"
#include "pinned.hpp"
#include "task.hpp"

// TODO: it might help returning `dt` from `co_await scheduler.schedule()`
// ^ or it can be an event as well

struct scheduler;

// A special instance of event, that resumes on the scheduler firing it
struct exit_awaiter final
{
    scheduler *sched;
    std::coroutine_handle<> hnd;
    exit_awaiter *next = nullptr;

    static constexpr bool await_ready() noexcept { return false; }

    inline std::coroutine_handle<> await_suspend(std::coroutine_handle<> hnd);

    static constexpr void await_resume() noexcept {}
};

struct scheduler final : pinned
{
    scheduler() = default;

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

        // just checking; normally shouldn't happen
        while (auto awt = exit_list.pop())
        {
            awt->hnd.destroy();
        }
    }

    // for use by events only, `co_await scheduler.schedule()` instead for coroutines
    inline void schedule(std::coroutine_handle<> hnd) { submitted.push(hnd); }

    // this really makes sense for events not coroutines, ie. scheduling a listener the same frame that the event happens,
    // so that's why there is no `co_await` version.
    inline void schedule_now(std::coroutine_handle<> hnd) { ready.push(hnd); }
    inline void sleep(std::coroutine_handle<> hnd, int ticks) { sleeping.push({hnd, time_now + ticks}); }

    // for internal use only
    inline void when_done(exit_awaiter &other) noexcept { exit_list.push(other); }

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

    // for use by coroutines
    // Await for the scheduler to complete then resume the coroutine
    [[nodiscard]]
    inline auto when_done() noexcept
    {
        return exit_awaiter{this};
    }

    // for use when the scheduler is created, the scheduler will stop when the timer expires
    inline fire_and_forget timeout(int ticks)
    {
        co_await sleep(ticks);
        is_done = true;
    }

    inline void update()
    {
        ++time_now;

        if (is_done)
            return;

        // invariant: nothing is resumed here so `!is_done` is constant
        while (!sleeping.empty())
        {
            auto [hnd, when] = sleeping.top();
            if (time_now < when)
                break;

            sleeping.pop();
            submitted.push(hnd);
        }

        std::swap(ready, submitted);
        // TODO: should we check for `is_done` or run everything in this frame?
        while (!is_done && !ready.empty())
        {
            auto hnd = ready.front();
            ready.pop();
            hnd.resume();
        }

        // just making sure
        // NOTE: this does not take events into account (but should it?)
        is_done = is_done || (submitted.empty() && sleeping.empty());
        if (is_done)
            complete();
    }

    // TODO: do we really need this?
    // NOTE: this function won't stop other active coroutines in `sched` from running
    // Use this function when you care to know when the "waited" scheduler (`this`) finishes
    // Use `offload` if you don't care
    [[nodiscard]]
    inline task<> wait_on(scheduler &sched) noexcept
    {
        while (!is_done)
        {
            co_await sched.schedule();
            update();
        }
    }

    // TODO: do we really need this?
    // TODO: need a better name; you can't await the return of this so why call it "wait_*"?
    // use this function if you don't care when the "waited" scheduler finishes
    // Use `wait_for_other` otherwise
    inline fire_and_forget offload(scheduler &other) noexcept
    {
        co_await other.wait_on(*this);
    }

    // Spawn a new scheduler an make it `wait_on(*this)`.
    // If running from main, wrap this in a `fire_and_forget` coroutine first
    // Or make a `scheduler child;` yourself, submit your coroutines there and call `this.wait_for_other(child)`
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
        if (is_done || ready.empty())
            return std::noop_coroutine();

        auto top = ready.front();
        ready.pop();
        return top;
    }

    [[nodiscard]] inline bool done() const noexcept { return is_done; }

    // Mark the scheduler as done. The coroutines waiting on `when_done` will run and nothing else scheduled will run.
    inline void complete() noexcept
    {
        is_done = true;
        while (auto awt = exit_list.pop())
        {
            awt->hnd.resume();
        }
    }

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

    bool is_done = false;
    int time_now = 0;
    std::queue<std::coroutine_handle<>> ready, submitted;
    std::priority_queue<waiting, std::vector<waiting>, std::greater<>> sleeping;
    circular<exit_awaiter> exit_list;
};

inline std::coroutine_handle<> exit_awaiter::await_suspend(std::coroutine_handle<> hnd)
{
    this->hnd = hnd;
    sched->when_done(*this);
    return sched->pick_next();
}
