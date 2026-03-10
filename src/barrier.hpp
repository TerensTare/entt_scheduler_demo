
#pragma once

#include "circular.hpp"
#include "scheduler.hpp"

// A reusable asynchronous utility that can organize coroutines to be ran when desired and wait for them to finish.
// Functionally, the given snippet:
// ```cpp
// fire_and_forget barrier_driver(barrier &b, scheduler &sched)
// {
//      while (!sched.done)
//      {
//          co_await b.launch_and_wait(sched); // launch the waiting coroutines and wait for them to complete. Then resume this coroutine on `sched`.
//      }
// }
//
// fire_and_forget my_entity_logic(barrier &turn_barrier, scheduler &sched, other_stuff_here...)
// {
//      while (!sched.done)
//      {
//          // v-- the scheduler can be different from the one used in the barrier's constructor
//          auto t = co_await turn_barrier.next_turn(sched); // resume this coroutine on `sched` when the barrier opens
//          // ^ `t` is a RAII value that tells the barrier that we are done on its destructor, so we shouldn't discard it.
//          // our logic here, it can span across many frames
//      }
// }
//
// int main()
// {
//      scheduler sched;
//      barrier b;
//
//      barrier_driver(b, sched);
//      my_entity_logic(b, sched, other_stuff_here...);
//      // other coroutines that should run every turn
// }
// ```
// is equivalent to:
// ```cpp
// fire_and_forget my_entity_logic(scheduler &sched, other_stuff_here...)
// {
//      // the logic of a single turn
//      // notice how there is no while loop anymore
// }
//
// fire_and_forget all_my_turn(scheduler &sched)
// {
//      while (!sched.done)
//      {
//          co_await sched.scope([](auto &inner) {
//                  my_entity_logic(inner);
//          });
//      }
// }
//
// int main()
// {
//      scheduler sched;
//      all_my_turn(sched); // that's it
// }
// ```
// Meaning you should avoid `barrier` most of the time and favor the `while (!done) co_await scope` solution.
// A good use case for `barrier` is when you can't "name" all your coroutines that need it (eg. you have one coroutine per entity in your game, and naming them one by one is error-prone because you might forget some)
// or you can't re-spawn your coroutines somehow.
struct barrier final : pinned
{
    // Create a new barrier bound to the given scheduler.
    // This means that any time the barrier resumes it will run into the given scheduler.
    // Coroutines waiting on this barrier can be resumed anywhere, see `.next_turn`.
    barrier() = default;

    inline ~barrier()
    {
        if (barrier_coro)
            barrier_coro.destroy();
    }

    struct cycle_awaiter final
    {
        barrier *owner;

        static constexpr auto await_ready() noexcept { return false; }

        inline auto await_suspend(std::coroutine_handle<> hnd) noexcept
        {
            // TODO: assert counter is 0 here
            owner->barrier_coro = hnd;

            uint32_t c{};
            while (auto awt = owner->awt.pop())
            {
                awt->sched->schedule_now(awt->hnd);
                c++;
            }

            owner->counter = c;

            return owner->sched->pick_next();
        }

        static constexpr void await_resume() noexcept {}
    };

    // Schedule the coroutines waiting on this barrier and wait for them to suspend
    [[nodiscard]]
    inline auto launch_and_wait(scheduler &sched) noexcept
    {
        this->sched = &sched;
        return cycle_awaiter{this};
    }

    struct awaiter;

    struct ticket final : pinned
    {
        // NOTE: the local variables of a coro are destroyed before `final_suspend`, so it doesn't matter
        // what that returns, this will be called before `final_suspend`
        inline ~ticket()
        {
            // TODO: counter shouldn't go below 0, but check just to be sure
            if (--owner->counter == 0)
            {
                owner->sched->schedule_now(owner->barrier_coro);
                owner->sched = nullptr; // for safety, to make sure nothing unexpected happens
            }
        }

    private:
        inline explicit ticket(barrier &b) noexcept
            : owner{&b} {}

        barrier *owner;

        friend awaiter;
    };

    struct awaiter final
    {
        barrier *owner;
        scheduler *sched;
        std::coroutine_handle<> hnd;
        awaiter *next = nullptr;

        static constexpr bool await_ready() noexcept { return false; }

        inline auto await_suspend(std::coroutine_handle<> hnd) noexcept
        {
            // NOTE: this is called at a different "time" from when the coroutine is resumed, while the previous cycle has not completed yet.
            // So this counter increment must not be done here.
            this->hnd = hnd;
            owner->awt.push(*this);
            return sched->pick_next();
        }

        [[nodiscard(
            "Discarding this tells the barrier you completed your work\n"
            "Otherwise your \"turn\" completes when this scope exits (This is a RAII type).")]]
        inline ticket await_resume() noexcept
        {
            // NOTE: this is called when the coroutine is resumed, so you might think the counter should be incremented here.
            // The problem is if the coroutine doesn't suspend before finishing its turn, the counter will decrement back to 0 when the turn finishes,
            // meanwhile the other coroutines wouldn't run before the counter goes back to 0.
            // Instead, the correct "solution" is to count all the coroutines resumed from `launch_and_wait` and use that value as a counter.
            return ticket{*owner};
        }
    };
    [[nodiscard]]
    inline auto next_turn(scheduler &sched) noexcept
    {
        return awaiter{this, &sched};
    }

private:
    circular<awaiter> awt; // list of coroutines waiting on this barrier
    uint32_t counter{};
    scheduler *sched = nullptr;
    std::coroutine_handle<> barrier_coro = std::noop_coroutine();
};
