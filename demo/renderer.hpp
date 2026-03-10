
#pragma once

#include <cstdio>
#include <string>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "coords.hpp"

struct renderer final
{
    renderer(renderer const &) = delete;
    renderer &operator=(renderer const &) = delete;

    renderer(renderer &&) = delete;
    renderer &operator=(renderer &&) = delete;

    inline renderer() noexcept
        : renderer{terminal_size()} {}

    inline explicit renderer(size s) noexcept
        : renderer{s.w, s.h} {}

    inline renderer(int w, int h) noexcept
        : w{w}, h{h}
    {
        auto memory = new char[2 * (w * h)];
        front = memory;
        back = memory + (w * h);

        printf(
            "\033[2J"   // clear the screen
            "\033[?25l" // hide the cursor
        );
    }

    inline ~renderer()
    {
        delete[] front;
    }

    inline void clear(char ch = ' ') noexcept
    {
        memset(back, ch, w * h);
    }

    inline void rune(int x, int y, char ch) noexcept { back[y * w + x] = ch; }
    inline void rune(point p, char ch) noexcept { rune(p.x, p.y, ch); }

    // returns the number of lines that the string took
    inline int string(int line, std::string_view msg, int offset = 0) noexcept
    {
        auto left = msg.size();
        auto str = msg.data();

        auto const start = line;
        while (left)
        {
            auto const len = std::min(size_t(w - offset), left);
            std::copy_n(str, len, &back[line * w + offset]);

            ++line;
            left -= len;
            str += len;
        }

        return line - start;
    }

    inline void present() noexcept
    {
        // hopefully this works
        for (int y{}; y < h; ++y)
        {
            auto const yw = y * w;
            for (int x{}; x < w; ++x)
            {
                auto const index = yw + x;

                if (front[index] != back[index])
                {
                    printf("\033[%d;%dH%c", y + 1, x + 1, back[index]);
                    front[index] = back[index];
                }
            }
        }
    }

private:
    inline static size terminal_size() noexcept
    {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);

        return {
            .w = csbi.srWindow.Right - csbi.srWindow.Left + 1,
            .h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1,
        };
#else
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        return {
            .w = static_cast<int>(w.ws_col),
            .h = static_cast<int>(w.ws_row),
        };
#endif
    }

    int w, h;
    char *front, *back;
};