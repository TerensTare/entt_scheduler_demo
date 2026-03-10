
#pragma once

#ifdef _WIN32
#include <conio.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#endif

#include "event.hpp"

namespace detail
{
#ifdef _WIN32
// no "global" state
#else
    static termios newt;
#endif

    inline void setup_terminal() noexcept
    {
#ifdef _WIN32
// no setup needed
#else
        tcgetattr(STDIN_FILENO, &oldt);

        struct termios newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO); // Disable canonical mode and echo
        newt.c_cc[VMIN] = 0;              // read returns immediately
        newt.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);

        // Set non-blocking mode
        fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
#endif
    }

    inline bool read_key(char &ch) noexcept
    {
#ifdef _WIN32
        if (_kbhit())
        {
            ch = (char)_getch_nolock();
            return true;
        }
        return false;
#else
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        timeval timeout{0, 0};
        if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) > 0)
        {
            char ch;
            read(STDIN_FILENO, &ch, 1);
            return true;
        }

        return false;
#endif
    }

    inline fire_and_forget cleanup_terminal(scheduler &sched) noexcept
    {
#ifdef _WIN32
// no cleanup needed
#else
        co_await sched.when_done();

        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
#endif

        co_return;
    }
}

inline fire_and_forget listen_to_keyboard(scheduler &sched, event<char> &key)
{
    while (true)
    {
        co_await sched.schedule();

        if (char ch; detail::read_key(ch))
        {
            key.fire(ch);
        }
    }
}

inline fire_and_forget keyboard_plugin(scheduler &sched, event<char> &key)
{
    detail::setup_terminal();
    listen_to_keyboard(sched, key);
    detail::cleanup_terminal(sched);

    co_return;
}
