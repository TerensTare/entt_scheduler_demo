
#pragma once

#include <random>

struct random final
{
    random(random const &) = delete;
    random &operator=(random const &) = delete;

    inline random() noexcept : rng{std::random_device{}()} {}

    inline explicit random(unsigned int seed) noexcept
    {
        this->seed(seed);
    }

    inline void seed(unsigned int s) noexcept
    {
        rng.seed(s);
        current_seed = s;
    }

    [[nodiscard]] inline unsigned int seed() const noexcept { return current_seed; }

    inline void randseed() noexcept { seed(std::random_device{}()); }

    [[nodiscard]] inline bool bit() noexcept { return (bool)operator()(1); }

    // random(0, max) max is inclusive
    [[nodiscard]] inline int operator()(int max) noexcept { return operator()(0, max); }

    // random in range [min, max] (max is inclusive)
    [[nodiscard]]
    inline int operator()(int min, int max) noexcept
    {
        std::uniform_int_distribution<> dist(min, max);
        return dist(rng);
    }

private:
    std::default_random_engine rng;
    unsigned int current_seed;
};