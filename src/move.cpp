#include "move.hpp"

#include <cctype>

std::optional<Direction> directionFromInput(char input) {
    switch (std::tolower(static_cast<unsigned char>(input))) {
        case 'w':
        case 'z':
            return Direction::Up;
        case 's':
            return Direction::Down;
        case 'a':
        case 'q':
            return Direction::Left;
        case 'd':
            return Direction::Right;
        default:
            return std::nullopt;
    }
}

std::string directionName(Direction direction) {
    switch (direction) {
        case Direction::Up:
            return "haut";
        case Direction::Down:
            return "bas";
        case Direction::Left:
            return "gauche";
        case Direction::Right:
            return "droite";
    }
    return "inconnu";
}
