
#pragma once

#include "task.hpp"
#include "scheduler.hpp"

task<> pause_menu(scheduler &sched)
{

    co_return;
}