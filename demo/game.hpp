
#pragma once

#include <cassert>
#include <stack>

#include "event.hpp"

#include "demo/actions.hpp"
#include "demo/random.hpp"
#include "demo/renderer.hpp"

struct scene_stack final
{
    // `driver` should be the scheduler you use for your "global" coroutines such as listening for input, etc.
    // It is highly likely every other scheduler will be depending on `driver`'s "clock".
    inline explicit scene_stack(scheduler &driver)
        : driver{&driver} {}

    inline void update()
    {
        driver->update();
        scenes.top()->update();
    }

    // TODO: should we have a function that pops the top scene first then pushes?

    // RAII - no need to pop
    inline task<> push(scheduler &sched)
    {
        auto const n = scenes.size();
        scenes.push(&sched);

        co_await sched.when_done();

        scenes.pop();
        assert(n == scenes.size() && "You forgot to pop some scenes before this!");
    }

    // HACK: this doesn't keep track of the overlay schedulers anywhere
    // instead it just waits for the top scheduler's "update" to update the overlay
    // meaning overlays are transferred among different scenes if you don't set `done` on the overlay's scheduler
    inline fire_and_forget overlay(scheduler &sched)
    {
        // TODO: should overlays instead be ran from `driver`
        do
        {
            co_await scenes.top()->schedule();
            sched.update();
        } while (!sched.done() && !scenes.top()->done());
    }

private:
    scheduler *driver;
    // TODO: can you actually use just two pointers rather than a full stack? (`top` and `old_top`)
    std::stack<scheduler *> scenes;
};

struct game final
{
    random rng;
    renderer ren;
    scheduler all_scheduler;
    scene_stack scenes{all_scheduler};
    event<char> user_input;
    action_map<ui_action> ui_actions = ui_action_map;
};