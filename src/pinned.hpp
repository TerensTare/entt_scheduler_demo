
#pragma once

struct pinned
{
    pinned() = default;

    pinned(pinned const &) = delete;
    pinned &operator=(pinned const &) = delete;

    pinned(pinned &&) = delete;
    pinned &operator=(pinned &&) = delete;
};