#pragma once

#include <optional>
#include <string>

enum class Direction {
    Up,
    Down,
    Left,
    Right,
};

std::optional<Direction> directionFromInput(char input);
std::string directionName(Direction direction);
