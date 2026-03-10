
#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "demo/coords.hpp"
#include "demo/random.hpp"

enum class tile : uint8_t
{
    empty = '.',
    wall = '#',
    wanderer = 'W',
    follower = 'F',
    player = '@',
};

struct map final
{
    inline static map of(size s) noexcept
    {
        return map::of(s.w, s.h);
    }

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

            delete[] other_row;
        }
        std::fill_n(tiles + (w * (h - 1)), w, tile::wall); // bottom row

        for (int j{}; j < h; ++j)
        {
            tiles[w * j] = tile::wall;
            tiles[w * (j + 1) - 1] = tile::wall;
        }

        std::fill_n(tiles, w, tile::wall);
        std::fill_n(&tiles[w * (h - 1)], w, tile::wall);

        return map{tiles, w, h};
    }

    map(map const &) = delete;
    map &operator=(map const &) = delete;

    inline ~map()
    {
        delete[] tiles;
    }

    inline point random_free(random &rng) const noexcept
    {
        int index;
        do
            index = rng(w * h - 1);
        while (!can_use(index));

        return {
            .x = index % w,
            .y = index / w,
        };
    }

    inline tile &at(point p) noexcept
    {
        assert(((p.x < w) && (p.y < h)) && "Tile out of reach");
        return tiles[p.y * w + p.x];
    }

    inline tile const &at(point p) const noexcept
    {
        assert(((p.x < w) && (p.y < h)) && "Tile out of reach");
        return tiles[p.y * w + p.x];
    }

    inline bool can_use(point p) const noexcept
    {
        return (p.x >= 0 && p.y >= 0) && (p.x < w && p.y < h) && (at(p) == tile::empty);
    }

    inline bool try_move(point &old, point step) noexcept
    {
        point const dest{
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

private:
    constexpr map(tile *tiles, int w, int h) noexcept
        : tiles{tiles}, w{w}, h{h} {}

    inline bool can_use(int index) const noexcept
    {
        return tiles[index] == tile::empty;
    }
};
