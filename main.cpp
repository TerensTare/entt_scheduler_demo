

#include <cstdio>
#include <chrono>
#include <compare>
#include <random>
#include <span>
#include <thread>

#include <entt/entity/registry.hpp>

#include "counter.hpp"
#include "demo/input.hpp"

enum class tile : uint8_t
{
    empty = '.',
    wall = '#',
    wanderer = 'W',
    follower = 'F',
    player = '@',
};

struct position final
{
    int x, y;
};

struct actor_tag final
{
};

struct player_tag final
{
};

struct map final
{
    inline static map of(int w, int h) noexcept
    {
        assert(((w > 2) && (h > 2)) && "Lame map. Do something bigger");

        // form a rectangular wall
        auto tiles = new tile[w * h];
        std::fill_n(tiles, w, tile::wall); // top row
        // "middle" rows
        {
            auto other_row = new tile[w];
            other_row[0] = tile::wall;
            std::fill_n(other_row + 1, w - 2, tile::empty);
            other_row[w - 1] = tile::wall;
            for (int j = 1; j < h - 1; ++j)
                std::copy_n(other_row, w, tiles + (w * j));

            delete other_row;
        }
        std::fill_n(tiles + (w * (h - 1)), w, tile::wall); // bottom row

        for (int j{}; j < h; ++j)
        {
            tiles[w * j] = tile::wall;
            tiles[w * (j + 1) - 1] = tile::wall;
        }

        // or a memset
        for (int i{}; i < w; ++i)
        {
            tiles[i] = tile::wall;
        }

        // or a memset
        for (int i{}; i < w; ++i)
        {
            tiles[w * (h - 1) + i] = tile::wall;
        }

        return map{
            .tiles = tiles,
            .w = w,
            .h = h,
        };
    }

    inline tile &at(position p) noexcept
    {
        assert(((p.x < w) && (p.y < h)) && "Tile out of reach");
        return tiles[p.y * w + p.x];
    }

    inline tile const &at(position p) const noexcept
    {
        assert(((p.x < w) && (p.y < h)) && "Tile out of reach");
        return tiles[p.y * w + p.x];
    }

    inline bool can_use(position p) const noexcept { return at(p) == tile::empty; }

    inline bool try_move(position &old, position step) noexcept
    {
        position dest = {
            .x = old.x + step.x,
            .y = old.y + step.y,
        };
        if (can_use(dest))
        {
            at(dest) = at(old);
            at(old) = tile::empty;
            old = dest;
            return true;
        }

        return false;
    }

    tile *tiles;
    int w, h;
};

struct game final
{
    entt::registry reg;
    map map;

    scheduler sched;
    counter actors_left; // actors that are not processed yet this turn (mostly relevant for the player, since its action can spread in multiple frames)
    event<> next_turn;
    event<char> user_input;
    std::vector<std::string> message_queue;
};

fire_and_forget handle_input(game &game, entt::entity player);

// generic "factory"
entt::entity make_actor(game &game, tile t, position initial_pos)
{
    auto const id = game.reg.create();
    game.reg.emplace<actor_tag>(id);
    game.reg.emplace<position>(id, initial_pos);
    game.map.at(initial_pos) = game.reg.emplace<tile>(id, t);
    return id;
}

// randomly move to a nearby tile
fire_and_forget wanderer_ai(game &game, entt::entity self)
{
    while (!game.sched.done)
    {
        co_await game.next_turn.resume_on(game.sched);

        static constexpr position dirs[]{
            {0, -1},
            {1, 0},
            {0, 1},
            {-1, 0},
        };

        position step;
        auto &&pos = game.reg.get<position>(self);

        // dummy "try a random position and see if you can go there"
        do
        {
            int dir = rand() % 4; // up, right, down, left
            step = dirs[dir];
        } while (!game.map.try_move(pos, step));

        game.actors_left.decrement();
    }
}

entt::entity make_wanderer(game &game, position initial_pos)
{
    auto const id = make_actor(game, tile::wanderer, initial_pos);
    wanderer_ai(game, id);
    return id;
}

// follow the player
fire_and_forget follower_ai(game &game, entt::entity self)
{
    // invariant: this shouldn't change
    auto player_id = game.reg.storage<player_tag>()[0];

    while (!game.sched.done)
    {
        co_await game.next_turn.resume_on(game.sched);

        static constexpr position dirs[]{
            {0, -1},
            {1, 0},
            {0, 1},
            {-1, 0},
        };

        position step;
        auto &&pos = game.reg.get<position>(self);
        auto &&player_pos = game.reg.get<position>(player_id);

        // dummy "try a random position and see if you can go there"
        do
        {
            // get the distance from the player
            position const dist{
                .x = player_pos.x - pos.x,
                .y = player_pos.y - pos.y,
            };

            // move along the farthest axis from the player
            auto cmp = abs(dist.x) <=> abs(dist.y);
            if (cmp > 0)
            {
                step.x = 1 - 2 * signbit(dist.x); // -1 if negative dist.x, 1 otherwise
                step.y = 0;
            }
            else if (cmp < 0)
            {
                step.x = 0;
                step.y = 1 - 2 * signbit(dist.y); // -1 if negative dist.y, 1 otherwise
            }
            // if equal, pick a random axis
            else if (rand() & 1)
            {
                step.x = 1 - 2 * signbit(dist.x); // -1 if negative dist.x, 1 otherwise
                step.y = 0;
            }
            else
            {
                step.x = 0;
                step.y = 1 - 2 * signbit(dist.y); // -1 if negative dist.y, 1 otherwise
            }
        } while (!game.map.try_move(pos, step));

        game.actors_left.decrement();
    }
}

entt::entity make_follower(game &game, position initial_pos)
{
    auto const id = make_actor(game, tile::follower, initial_pos);
    follower_ai(game, id);
    return id;
}

entt::entity make_player(game &game, position initial_pos)
{
    auto const id = make_actor(game, tile::player, initial_pos);
    game.reg.emplace<player_tag>(id);
    handle_input(game, id);
    return id;
}

inline void render(game &game)
{
    // hopefully this works
    printf("\033[H\033[2K\n");

    for (int j{}; j < game.map.h; ++j)
    {
        // HACK, but it works :)
        auto row = std::string_view((char const *)game.map.tiles, game.map.w * game.map.h);
        row = row.substr(game.map.w * j, game.map.w);
        printf("%.*s\n", (int)row.size(), row.data());
    }

    printf("\n\nPress WASD to move around. Press T to teleport.\n\n");
    for (auto &&msg : game.message_queue)
    {
        printf("%.*s\n", (int)msg.size(), msg.data());
    }

    printf("\n\n");

    game.message_queue.clear();
}

// Fires the `next_turn` event when appropriate
fire_and_forget turn_clock(game &game)
{
    co_await game.sched.schedule(); // first just wait for one tick
    // This ensures that the entity coroutines are spawned and they can listen for "next turn"

    while (!game.sched.done)
    {
        co_await game.actors_left.resume_on(game.sched);
        game.next_turn.fire(); // a turn starts after everybody took an action

        game.actors_left.reset(game.reg.storage<actor_tag>().size()); // reset the counter
    }
}

fire_and_forget handle_input(game &game, entt::entity player)
{
    while (!game.sched.done)
    {
        // TODO: there should be a sequential wait
        // TODO: show an energy bar for everybody
        co_await game.next_turn.resume_on(game.sched); // wait for your turn first

        auto &&pos = game.reg.get<position>(player);
        position step{};

        do
        {
            auto ch = co_await game.user_input.resume_on(game.sched);

            switch (ch)
            {
            case 'w':
                step = {0, -1};
                break;
            case 'a':
                step = {-1, 0};
                break;
            case 's':
                step = {0, 1};
                break;
            case 'd':
                step = {1, 0};
                break;

            case 't': // teleport
            {
                int rand_idx = -1;
                position dest{};
                do
                {
                    rand_idx = rand() % (game.map.w * game.map.h);
                    dest.x = rand_idx % game.map.w;
                    dest.y = rand_idx / game.map.w;
                } while (!game.map.can_use(dest));

                step.x = dest.x - pos.x;
                step.y = dest.y - pos.y;
            }
            break;
            }
            // invariant: trying to move to yourself fails, so even if the key is not a "movement" this should be fine
        } while (!game.map.try_move(pos, step));

        game.actors_left.decrement();
    }
}

fire_and_forget sleepy_warning(game &game, scheduler &inner, int ticks)
{
    co_await inner.sleep(ticks);

    while (!inner.done)
    {
        game.message_queue.push_back("\033[33mC'mon man, do something...\033[0m");
        co_await inner.schedule();
    }
}

fire_and_forget warn_for_sleep(game &game)
{
    // repeat the warning as long as the game is running
    while (!game.sched.done)
    {
        co_await game.sched.scope(
            [&](auto &inner)
            {
                sleepy_warning(game, inner, 10);     // give a warning after 100 ticks
                complete_on(inner, game.user_input); // until a key gets pressed
            });
    }
}

int main()
{
    srand(time(0));

    using namespace std::chrono_literals;

    game game{
        .map = map::of(40, 10),
        .actors_left{0}, // 0 initially, turn_clock will take care of this
    };

    // coroutines
    listen_to_keyboard(game.sched, game.user_input);
    turn_clock(game);
    warn_for_sleep(game);

    // entities
    make_player(game, {20, 5});
    make_wanderer(game, {10, 3});
    make_follower(game, {30, 8});

    while (!game.sched.done)
    {
        game.sched.update();
        render(game);
        std::this_thread::sleep_for(100ms);
    }
}