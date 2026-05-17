#include "evaluator.hpp"

#include <array>
#include <cmath>

double evaluateBoard(const Board& board) {
    double value = 0.0;
    value += board.emptyCount() * 12.0;
    value += board.highestTile() * 8.0;

    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            const int current = board.at(row, col);
            if (current == 0) {
                continue;
            }
            if (row + 1 < Board::Size) {
                value -= std::abs(current - board.at(row + 1, col)) * 0.6;
            }
            if (col + 1 < Board::Size) {
                value -= std::abs(current - board.at(row, col + 1)) * 0.6;
            }
        }
    }

    const std::array<Cell, 4> corners{Cell{0, 0}, Cell{0, Board::Size - 1}, Cell{Board::Size - 1, 0}, Cell{Board::Size - 1, Board::Size - 1}};
    for (const Cell corner : corners) {
        if (board.at(corner.row, corner.col) == board.highestTile()) {
            value += 10.0;
        }
    }

    return value;
}
