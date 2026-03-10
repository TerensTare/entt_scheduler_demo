
#pragma once

#include <optional>

#include "circular.hpp"
#include "scheduler.hpp"

namespace detail
{
    template <typename T, typename U>
    struct compressed_pair final
    {
        static constexpr bool is_compressed = false;

        T first;
        U second;
    };

    template <typename T>
    struct compressed_pair<T, void> final
    {
        static constexpr bool is_compressed = true;

        T first;
    };

    template <typename T>
    using optional = std::conditional_t<std::is_void_v<T>, void, std::optional<T>>;
}

template <typename T = void>
struct event final : pinned
{
    event() = default;

    inline ~event()
    {
        // Destroy any waiting coroutine.
        // Their handles are only accessible here, since they are not scheduled, so it's our responsibility to destroy.
        while (auto awt = waiting_and_value.first.pop())
        {
            awt->hnd.destroy();
        }
    }

    // void overload
    inline void fire()
        requires(std::is_void_v<T>)
    {
        fire_impl();
    }

    // non-void overload
    template <typename U = T>
        requires(!std::is_void_v<T> && std::constructible_from<T, U &&>)
    inline void fire(U &&u)
    {
        waiting_and_value.second.emplace(static_cast<U &&>(u));
        fire_impl();
    }

    struct awaiter final
    {
        event *evt;
        scheduler *sched;
        std::coroutine_handle<> hnd;
        awaiter *next = nullptr;

        static constexpr bool await_ready() noexcept { return false; }

        inline auto await_suspend(std::coroutine_handle<> hnd) noexcept
        {
            // keep track of the waiting coroutine
            this->hnd = hnd;
            // push this to the awaiter stack
            evt->waiting_and_value.first.push(*this);
            // and pick the next coroutine from the scheduler
            return sched->pick_next();
        }

        constexpr auto await_resume() const noexcept -> decltype(auto)
        {
            if constexpr (!evt->waiting_and_value.is_compressed)
                return evt->waiting_and_value.second.value();
        }
    };
    // Pause the coroutine until the event is fired. Resume the coroutine on `sched`.
    // TODO: need a better name
    [[nodiscard]]
    inline auto resume_on(scheduler &sched) noexcept
    {
        return awaiter{this, &sched};
    }

private:
    inline void fire_impl()
    {
        while (auto awt = waiting_and_value.first.pop())
        {
            // TODO: this might be called from the main loop, in which case the listeners
            // get resumed next frame
            awt->sched->schedule_now(awt->hnd);
        }
    }

    // circular queue implemented with an implicit linked list, and a value (if any)
    detail::compressed_pair<circular<awaiter>, detail::optional<T>> waiting_and_value;
};

// Helper that completes a scheduler when an event happens so that you can spawn them on scopes like so:
// ```cpp
// co_await sched.scope([&](auto &inner) {
//      inner.timeout(100);
//      complete_on(inner, key_press);
//      // other coroutines here...
// });
// ```
template <typename T>
inline fire_and_forget complete_on(scheduler &sched, event<T> &evt)
{
    if (!sched.done())
    {
        (void)co_await evt.resume_on(sched);
        sched.complete();
    }
}