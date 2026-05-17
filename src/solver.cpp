#include "solver.hpp"

#include "evaluator.hpp"

#include <limits>

std::optional<SpawnSuggestion> suggestSpawn(const Board& board, int value) {
    const auto empties = board.emptyCells();
    if (empties.empty() || value <= 0) {
        return std::nullopt;
    }

    SpawnSuggestion best;
    best.value = value;
    best.score = -std::numeric_limits<double>::infinity();

    for (const Cell cell : empties) {
        Board candidate = board;
        candidate.spawn(cell, value);

        double score = evaluateBoard(candidate);
        if (candidate.hasAnyMove()) {
            score += 20.0;
        }

        if (score > best.score) {
            best = {cell, value, score};
        }
    }

    return best;
}
