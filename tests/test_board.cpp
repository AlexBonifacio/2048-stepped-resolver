#include "../src/board.hpp"
#include "../src/solver.hpp"

#include <cassert>

namespace {

void testMergeRanks() {
    Board board;
    board.setCell(0, 0, 1);
    board.setCell(0, 1, 1);
    board.setCell(0, 2, 1);

    assert(board.move(Direction::Left));
    assert(board.at(0, 0) == 2);
    assert(board.at(0, 1) == 1);
    assert(board.at(0, 2) == 0);
    assert(board.at(0, 3) == 0);
}

void testSingleMergePerTile() {
    Board board;
    board.setCell(0, 0, 1);
    board.setCell(0, 1, 1);
    board.setCell(0, 2, 1);
    board.setCell(0, 3, 1);

    assert(board.move(Direction::Left));
    assert(board.at(0, 0) == 2);
    assert(board.at(0, 1) == 2);
    assert(board.at(0, 2) == 0);
    assert(board.at(0, 3) == 0);
}

void testBlockedMove() {
    Board board;
    board.setCell(0, 0, 1);

    assert(!board.move(Direction::Left));
    assert(board.move(Direction::Right));
    assert(board.at(0, 3) == 1);
}

void testSuggestion() {
    Board board;
    board.setCell(0, 0, 1);

    const auto suggestion = suggestSpawn(board, 1);
    assert(suggestion.has_value());
    assert(board.isEmpty(suggestion->cell.row, suggestion->cell.col));
    assert(suggestion->value == 1);
}

}  // namespace

int main() {
    testMergeRanks();
    testSingleMergePerTile();
    testBlockedMove();
    testSuggestion();
    return 0;
}
