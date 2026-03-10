

#include <chrono>
#include <thread>

#include "demo/input.hpp"
#include "demo/game.hpp"
#include "demo/scenes/main_menu.hpp"
#include "demo/scenes/world.hpp"

// TODO:
// - show health bar, mana bar
// - showcase effects like poison, burn, etc.
// - message_queue should be for events, have a separate space for "notifications" like hints, etc.

fire_and_forget setup_game(game &game)
{
    // game-related coroutines
    keyboard_plugin(game.all_scheduler, game.user_input);

// push the first scene
main_menu_:
    switch (co_await main_menu::run(game))
    {
    // TODO: strong-type these
    case 0: // "Start"
        co_await world_scene::run(game);
        break;

    case 1: // "Options"
    {
        printf("Sorry, options are not implemented yet. Please try another menu :)");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        goto main_menu_;
    }
    break;

    case 2: // "Quit"
        break;
    }

    game.all_scheduler.complete();
}

int main()
{
    using namespace std::chrono_literals;

    game game{
        .rng{42}, // for more deterministic replays
    };
    setup_game(game);

    while (!game.all_scheduler.done())
    {
        game.ren.clear();
        game.scenes.update();
        game.ren.present();
        std::this_thread::sleep_for(100ms);
    }
}