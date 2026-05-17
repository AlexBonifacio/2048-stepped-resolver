#pragma once

#include "board.hpp"

#include <optional>

struct SpawnSuggestion {
    Cell cell;
    int value = 1;
    double score = 0.0;
};

std::optional<SpawnSuggestion> suggestSpawn(const Board& board, int value);
