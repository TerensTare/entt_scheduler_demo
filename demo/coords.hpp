
#pragma once

#include "generator.hpp"

struct size final
{
    int w, h;
};

struct point final
{
    int x, y;
};

// Just a small helper
generator<point> cartesian(int w, int h)
{
    for (int j{}; j < h; ++j)
    {
        for (int i{}; i < w; ++i)
            co_yield point{i, j};
    }
}
