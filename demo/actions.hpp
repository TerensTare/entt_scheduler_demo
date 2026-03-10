
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

// Just a type-safe way to map key presses to actions. You can have multiple action maps to handle things like in-game input and UI-related stuff.
// (ie. player movement is one map and UI-related stuff such as pausing the game is in another).
// See below for an example.

// TODO: what if you keep a map of `char -> action`? this way you can bind multiple keys to the same action and lookup is faster
template <typename T>
    requires(std::is_enum_v<T> && requires { T::_action_count; })
struct action_map final
{
    using action_base = std::underlying_type_t<T>;

    // return the action for the given key, or `_action_count` otherwise.
    constexpr T map(char ch) const noexcept
    {
        for (action_base i{}; i < (action_base)T::_action_count; ++i)
        {
            if (actions[i] == ch)
                return T(i);
        }

        return T::_action_count;
    }

    constexpr action_map &remap(T action, char key) noexcept
    {
        actions[action_base(action)] = key;
        return *this;
    }

    std::array<char, action_base(T::_action_count)> actions;
};

enum class player_action : uint8_t
{
    up,
    down,
    left,
    right,
    teleport,
    _action_count,
};

static constexpr auto player_action_map = []
{
    using enum player_action;
    return action_map<player_action>{}
        .remap(up, 'w')
        .remap(down, 's')
        .remap(left, 'a')
        .remap(right, 'd')
        .remap(teleport, 't');
}();

enum class ui_action : uint8_t
{
    up,
    down,
    pause,
    _action_count,
};

static constexpr auto ui_action_map = []
{
    using enum ui_action;
    return action_map<ui_action>{}
        .remap(up, 'w')
        .remap(down, 's')
        .remap(pause, 'p');
}();