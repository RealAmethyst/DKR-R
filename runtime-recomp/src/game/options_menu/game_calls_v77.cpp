#include "options_menu/game_calls.hpp"

#include "funcs.h"

const dkr::options_menu::GameCalls& dkr::options_menu::game_calls_v77() {
    static const GameCalls calls{
#include "options_menu/game_calls_table.inl"
    };
    return calls;
}
