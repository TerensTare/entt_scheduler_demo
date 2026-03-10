
#pragma once

#include "demo/game.hpp"

static constexpr char const *main_menu_options[]{
    "Start",
    "Options",
    "Quit",
};

struct main_menu final
{
    inline static task<int> run(game &game)
    {
        main_menu m{game};
        co_await game.scenes.push(m.sched); // this scene is not an overlay
        // NOTE: by this point, `main_menu` is popped from the stack; is this what we want?
        co_return m.cursor;
    }

private:
    scheduler sched;
    int cursor = 0;

    inline explicit main_menu(game &game)
    {
        handle_input(game);
        render(game);
    }

    // wrap-around `val` to the given [0, hi) range
    static constexpr int wrap(int val, int lo, int hi) noexcept
    {
        auto const diff = hi - lo;
        return ((val + diff) % diff) + lo;
    }

    inline fire_and_forget handle_input(game &game)
    {
        while (true)
        {
            auto ch = co_await game.user_input.resume_on(sched);
            switch (game.ui_actions.map(ch))
            {
            case ui_action::up:
                cursor = wrap(cursor - 1, 0, std::size(main_menu_options));
                break;

            case ui_action::down:
                cursor = wrap(cursor + 1, 0, std::size(main_menu_options));
                break;

            case ui_action::pause:
                sched.complete();
                break;
            }
        }
    }

    inline fire_and_forget render(game &game)
    {
        while (true)
        {
            co_await sched.schedule();

            for (size_t i{}; i < std::size(main_menu_options); ++i)
            {
                // TODO: center and leave some space in-between
                game.ren.string(i, main_menu_options[i], 3);
            }

            game.ren.string(cursor, "->");

            game.ren.string(8, "Press WS to move up and down. Press P to select.");
        }
    }
};
