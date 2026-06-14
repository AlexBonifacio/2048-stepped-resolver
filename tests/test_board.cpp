#include "../src/board.hpp"
#include "../src/evaluator.hpp"
#include "../src/solver.hpp"
#include "../src/stat.hpp"

#include <cassert>
#include <cstdio>

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
    assert(board.score() == 4);
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
    assert(board.score() == 8);
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

void testMoveSuggestion() {
    Board board;
    board.setCell(0, 0, 1);
    board.setCell(0, 1, 1);

    const auto suggestion = suggestMove(board);
    assert(suggestion.has_value());

    Board candidate = board;
    assert(candidate.move(suggestion->direction));
}

void testExpectimaxSuggestion() {
    Board board;
    board.setCell(0, 0, 1);
    board.setCell(0, 1, 1);
    board.setCell(1, 0, 2);

    const auto suggestion = suggestMove(board, {{1, 0.9}, {2, 0.1}}, 4);
    assert(suggestion.has_value());
    assert(suggestion->depth == 4);

    Board candidate = board;
    assert(candidate.move(suggestion->direction));
}

void testHybridSuggestion() {
    Board board;
    board.setCell(0, 0, 1);
    board.setCell(0, 1, 1);
    board.setCell(1, 0, 2);
    board.setCell(2, 0, 3);

    const auto suggestion = suggestHybridMove(board, {{1, 0.9}, {2, 0.1}}, {3, 20, 2, 12, 7});
    assert(suggestion.has_value());
    assert(suggestion->solver == "hybrid");
    assert(suggestion->rollouts > 0);

    Board candidate = board;
    assert(candidate.move(suggestion->direction));
}

void testStatPersistence() {
    const char* path = "/tmp/2048-ranks-stats-test.tmp";

    StatTracker saved;
    saved.recordSpawn(3);
    saved.recordSpawn(3);
    assert(saved.save(path));

    StatTracker loaded;
    assert(loaded.load(path));
    const auto model = loaded.spawnModel();

    bool foundThree = false;
    for (const auto& spawn : model) {
        if (spawn.value == 3 && spawn.probability > 0.0) {
            foundThree = true;
        }
    }
    assert(foundThree);
    std::remove(path);
}

void testContextualSpawnModel() {
    StatTracker stats;
    for (int i = 0; i < 8; ++i) {
        stats.recordSpawn(1, 5 + i, 2);
    }
    for (int i = 0; i < 8; ++i) {
        stats.recordSpawn(4, 120 + i, 7);
    }

    const auto earlyModel = stats.spawnModel(6, 2);
    const auto lateModel = stats.spawnModel(124, 7);

    double earlyOne = 0.0;
    double earlyFour = 0.0;
    double lateOne = 0.0;
    double lateFour = 0.0;
    for (const auto& spawn : earlyModel) {
        if (spawn.value == 1) {
            earlyOne = spawn.probability;
        }
        if (spawn.value == 4) {
            earlyFour = spawn.probability;
        }
    }
    for (const auto& spawn : lateModel) {
        if (spawn.value == 1) {
            lateOne = spawn.probability;
        }
        if (spawn.value == 4) {
            lateFour = spawn.probability;
        }
    }

    assert(earlyOne > earlyFour);
    assert(lateFour > lateOne);
}

void testEvaluatorPrefersAnchoredHighestTile() {
    Board anchored;
    anchored.setCell(0, 0, 8);
    anchored.setCell(0, 1, 7);
    anchored.setCell(0, 2, 6);
    anchored.setCell(0, 3, 5);
    anchored.setCell(1, 3, 4);
    anchored.setCell(1, 2, 3);

    Board loose;
    loose.setCell(1, 1, 8);
    loose.setCell(0, 0, 7);
    loose.setCell(0, 1, 6);
    loose.setCell(0, 2, 5);
    loose.setCell(1, 2, 4);
    loose.setCell(1, 3, 3);

    assert(evaluateBoard(anchored) > evaluateBoard(loose));
}

void testEvaluatorPrefersSnakeWeightedChain() {
    Board snake;
    snake.setCell(2, 0, 1);
    snake.setCell(2, 1, 2);
    snake.setCell(2, 2, 3);
    snake.setCell(2, 3, 4);
    snake.setCell(3, 0, 8);
    snake.setCell(3, 1, 7);
    snake.setCell(3, 2, 6);
    snake.setCell(3, 3, 5);

    Board scattered;
    scattered.setCell(0, 0, 8);
    scattered.setCell(0, 1, 1);
    scattered.setCell(1, 0, 2);
    scattered.setCell(1, 1, 7);
    scattered.setCell(2, 0, 3);
    scattered.setCell(2, 1, 6);
    scattered.setCell(3, 0, 4);
    scattered.setCell(3, 1, 5);

    assert(evaluateBoard(snake) > evaluateBoard(scattered));
}

void testEvaluatorPenalizesDeadBoard() {
    Board dead;
    const int deadCells[Board::CellCount] = {
        6, 8, 9, 10,
        2, 4, 7, 6,
        4, 3, 6, 5,
        2, 1, 2, 1,
    };
    for (int index = 0; index < Board::CellCount; ++index) {
        dead.setCell(index / Board::Size, index % Board::Size, deadCells[index]);
    }
    assert(!dead.hasAnyMove());

    Board live;
    live.setCell(0, 0, 1);
    live.setCell(0, 1, 1);
    assert(live.hasAnyMove());

    assert(evaluateBoard(live) > evaluateBoard(dead));
}

void testExpectimaxAvoidsImmediateDeadSpawnTrap() {
    Board board;
    const int cells[Board::CellCount] = {
        6, 8, 9, 10,
        2, 4, 7, 6,
        4, 3, 6, 5,
        2, 0, 1, 2,
    };
    for (int index = 0; index < Board::CellCount; ++index) {
        board.setCell(index / Board::Size, index % Board::Size, cells[index]);
    }

    const auto suggestion = suggestMove(board, {{1, 0.77}, {2, 0.15}, {3, 0.05}, {4, 0.02}, {5, 0.01}}, 3, Board::CellCount);
    assert(suggestion.has_value());
    assert(suggestion->direction == Direction::Down);
}

}  // namespace

int main() {
    testMergeRanks();
    testSingleMergePerTile();
    testBlockedMove();
    testSuggestion();
    testMoveSuggestion();
    testExpectimaxSuggestion();
    testHybridSuggestion();
    testStatPersistence();
    testContextualSpawnModel();
    testEvaluatorPrefersAnchoredHighestTile();
    testEvaluatorPrefersSnakeWeightedChain();
    testEvaluatorPenalizesDeadBoard();
    testExpectimaxAvoidsImmediateDeadSpawnTrap();
    return 0;
}
