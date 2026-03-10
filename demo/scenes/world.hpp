
#pragma once

#include <format>

#include <entt/entity/registry.hpp>

#include "barrier.hpp"

#include "demo/actions.hpp"
#include "demo/game.hpp"
#include "demo/map.hpp"

// TODO:
// - its probably best for scenes to accept the parent `scheduler &` in the constructor, to help emulate a scene stack
// ^ or maybe have some `co_await transit{scene{}, sched}` helpers that help with that...

struct actor_tag final
{
};

struct player_tag final
{
};

struct health final
{
    int max;
    int value = max;
};

auto constexpr map_size = size{40, 10};

// ie. in-game scene
struct world_scene final
{
    inline static task<> run(game &game)
    {
        world_scene w{game};
        co_await game.scenes.push(w.sched);
    }

private:
    inline explicit world_scene(game &game) noexcept
        : map{map::of(map_size)}
    {
        hints.push("Press WASD to move around. Press T to teleport.");

        handle_turn_clock();
        turn_spy(game, sched); // Just a little spy telling you the current turn
        handle_ui_input(game); // handle UI-related interactions, eg. pressing the pause key
        wait_to_hide_controls_hint(game);
        warn_for_sleep(game);
        render(game);

        // entities
        {
            make_player(game, map.random_free(game.rng));
            make_wanderer(game, map.random_free(game.rng));
            make_follower(game, map.random_free(game.rng));
        }
    }

    scheduler sched;
    entt::registry reg;
    map map;
    barrier turn_clock; // used to coordinate coroutines that should run once per turn (eg. enemy AI)
    action_map<player_action> player_actions = player_action_map;
    std::queue<std::string> hints;
    std::string status_bar;

    // generic "factory"
    inline entt::entity make_actor(point initial_pos, tile t)
    {
        auto const id = reg.create();
        reg.emplace<actor_tag>(id);
        reg.emplace<point>(id, initial_pos);
        map.at(initial_pos) = reg.emplace<tile>(id, t);
        return id;
    }

    // randomly move to a nearby tile
    fire_and_forget wanderer_ai(game &game, entt::entity self)
    {
        while (true)
        {
            auto t = co_await turn_clock.next_turn(sched);

            static constexpr point dirs[]{
                {0, -1},
                {1, 0},
                {0, 1},
                {-1, 0},
            };

            point step;
            auto &&pos = reg.get<point>(self);

            // dummy "try a random point and see if you can go there"
            do
            {
                int dir = game.rng(0, 4); // up, right, down, left
                step = dirs[dir];
            } while (!map.try_move(pos, step));
        }
    }

    inline entt::entity make_wanderer(game &game, point initial_pos)
    {
        auto const id = make_actor(initial_pos, tile::wanderer);
        wanderer_ai(game, id);
        return id;
    }

    // follow the player
    fire_and_forget follower_ai(game &game, entt::entity self)
    {
        // invariant: this shouldn't change
        auto player_id = reg.storage<player_tag>()[0];

        while (true)
        {
            auto t = co_await turn_clock.next_turn(sched);

            static constexpr point dirs[]{
                {0, -1},
                {1, 0},
                {0, 1},
                {-1, 0},
            };

            point step;
            auto &&pos = reg.get<point>(self);
            auto &&player_pos = reg.get<point>(player_id);

            // dummy "try a random point and see if you can go there"
            do
            {
                // get the distance from the player
                point const dist{
                    .x = player_pos.x - pos.x,
                    .y = player_pos.y - pos.y,
                };

                auto cmp = abs(dist.x) <=> abs(dist.y);
                bool move_x;
                // move along the farthest axis from the player
                if (cmp > 0)
                    move_x = true;
                else if (cmp < 0)
                    move_x = false;
                // if equal, pick a random axis
                else
                    move_x = game.rng.bit();

                if (move_x)
                {
                    step.x = 1 - 2 * (int)signbit(dist.x); // -1 if negative dist.x, 1 otherwise
                    step.y = 0;
                }
                else
                {
                    step.x = 0;
                    step.y = 1 - 2 * (int)signbit(dist.y); // -1 if negative dist.y, 1 otherwise
                }
            } while (!map.try_move(pos, step));
        }
    }

    inline entt::entity make_follower(game &game, point initial_pos)
    {
        auto const id = make_actor(initial_pos, tile::follower);
        follower_ai(game, id);
        return id;
    }

    inline entt::entity make_player(game &game, point initial_pos)
    {
        auto const id = make_actor(initial_pos, tile::player);
        reg.emplace<player_tag>(id);
        reg.emplace<health>(id, 5);
        handle_player_input(game, id);
        return id;
    }

    inline fire_and_forget render(game &game)
    {
        co_await sched.schedule(); // `fire_and_forget` is eager, and the entities are not spawned yet at this point, so we wait

        while (true)
        {
            co_await sched.schedule();

            // TODO: do not show this on status bar, but somewhere else (since there might be many actors)
            // TODO: also show speed
            // TODO: probably best to order these by turn
            // TODO: probably best to restrict to top 3 or top 5
            for (auto [id, t, h] : reg.view<tile const, health const>().each())
            {
                // TODO: add a `symbol x count` text on for ease of access
                std::format_to(
                    std::back_inserter(status_bar),
                    "|\033[41m{:<{}}\033[0m{:<{}}| {} x {}\n",
                    "", h.value,         // "remaining health" bar
                    "", h.max - h.value, // "maximum health - remaining"
                    (char)t,
                    h.value // number of health bars
                );
            }

            for (auto p : cartesian(map.w, map.h))
            {
                game.ren.rune(p, (char)map.at(p));
            }

            auto j = map.h;
            j += 2; // two empty lines after map

            // HACK: do something better
            for (size_t i{}, n = hints.size(); i < n; ++i)
            {
                auto msg = std::move(hints.front());
                hints.pop();
                j += game.ren.string(j, msg);
                hints.push(std::move(msg));
            }

            j += 2;

            j += game.ren.string(j, status_bar); // TODO: this should be 1 line
            status_bar.clear();
        }
    }

    // Launches coroutines waiting on `turn_clock` when appropriate
    fire_and_forget handle_turn_clock()
    {
        co_await sched.schedule(); // first just wait for one tick
        // This ensures that the entity coroutines are spawned and they can listen for "next turn"

        while (true)
        {
            co_await turn_clock.launch_and_wait(sched); // next turn then starts after everybody took an action
        }
    }

    fire_and_forget handle_player_input(game &game, entt::entity player)
    {
        while (true)
        {
            // TODO: there should be a sequential wait
            // TODO: show an energy bar for everybody
            auto t = co_await turn_clock.next_turn(sched); // wait for your turn first

            auto &&pos = reg.get<point>(player);
            point step{};

            do
            {
                auto ch = co_await game.user_input.resume_on(sched);
                switch (player_actions.map(ch))
                {
                case player_action::up:
                    step = {0, -1};
                    break;
                case player_action::left:
                    step = {-1, 0};
                    break;
                case player_action::down:
                    step = {0, 1};
                    break;
                case player_action::right:
                    step = {1, 0};
                    break;

                case player_action::teleport:
                {
                    int rand_idx = -1;
                    auto const dest = map.random_free(game.rng);

                    step.x = dest.x - pos.x;
                    step.y = dest.y - pos.y;
                }
                break;
                }
                // invariant: trying to move to yourself fails, so even if the key is not a "movement" key this should be fine
            } while (!map.try_move(pos, step));
        }
    }

    inline fire_and_forget sleepy_warning(scheduler &sched, int ticks)
    {
        co_await sched.sleep(ticks);
        hints.push("\033[33mC'mon man, do something...\033[0m");
    }

    fire_and_forget warn_for_sleep(game &game)
    {
        // repeat the warning as long as the game is running
        while (true)
        {
            auto const hints_count = hints.size();
            co_await sched.scope(
                [&](auto &inner)
                {
                    complete_on(inner, game.user_input); // until a key gets pressed
                    sleepy_warning(inner, 10);           // give a warning after 100 ticks
                });
        }
    }

    fire_and_forget wait_to_hide_controls_hint(game &game)
    {
        co_await game.user_input.resume_on(sched);
        hints.pop();
    }

    fire_and_forget turn_spy(game &game, scheduler &sched)
    {
        // Just announce a "turn" when it starts
        int turn_n{};
        while (true)
        {
            hints.push(std::format("Turn #{}", ++turn_n));
            auto t = co_await turn_clock.next_turn(sched);
            hints.pop(); // HACK: do something better
        }
    }

    fire_and_forget handle_ui_input(game &game)
    {
        while (true)
        {
            auto ch = co_await game.user_input.resume_on(sched);
            switch (game.ui_actions.map(ch))
            {
            case ui_action::pause:
            {
                printf("Paused");
                std::this_thread::sleep_for(std::chrono::seconds(1));

                // TODO: is this correct?
                scheduler inner;
                [](auto &sched) -> fire_and_forget
                {
                    while (true)
                    {
                        printf("Still paused");
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        co_await sched.schedule();
                    }
                }(inner);

                // TODO: spawn the new scene here
                co_await game.scenes.push(inner);

                printf("After pause");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            break;
            }
        }
    }
};
