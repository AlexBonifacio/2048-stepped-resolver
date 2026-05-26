#include "solver.hpp"

#include "evaluator.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <unordered_map>
#include <vector>

namespace {

constexpr int DefaultDepth = 5;
constexpr double ProbabilityCutoff = 0.0001;

const std::array<Direction, 4> Directions{
    Direction::Up,
    Direction::Down,
    Direction::Left,
    Direction::Right,
};

std::vector<SpawnProbability> normalizedModel(std::vector<SpawnProbability> model) {
    model.erase(
        std::remove_if(model.begin(), model.end(), [](const SpawnProbability& spawn) {
            return spawn.value <= 0 || spawn.probability <= 0.0;
        }),
        model.end());

    double total = 0.0;
    for (const auto& spawn : model) {
        total += spawn.probability;
    }

    if (model.empty() || total <= 0.0) {
        return {{1, 0.9}, {2, 0.1}};
    }

    for (auto& spawn : model) {
        spawn.probability /= total;
    }
    return model;
}

std::uint64_t boardKey(const Board& board) {
    std::uint64_t key = 0;
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            const int shift = 4 * (row * Board::Size + col);
            key |= static_cast<std::uint64_t>(board.at(row, col) & 0xF) << shift;
        }
    }
    return key;
}

struct CacheKey {
    std::uint64_t board = 0;
    int depth = 0;
    bool playerTurn = false;

    bool operator==(const CacheKey& other) const {
        return board == other.board && depth == other.depth && playerTurn == other.playerTurn;
    }
};

struct CacheKeyHash {
    std::size_t operator()(const CacheKey& key) const {
        return static_cast<std::size_t>(key.board ^ (key.board >> 32)) ^
               (static_cast<std::size_t>(key.depth) << 1) ^
               (key.playerTurn ? 0x9e3779b9U : 0U);
    }
};

struct SearchState {
    std::unordered_map<CacheKey, double, CacheKeyHash> cache;
};

double expectimax(SearchState& state, const Board& board, const std::vector<SpawnProbability>& spawnModel, int depth, bool playerTurn, int chanceCells, double probability) {
    if (depth <= 0 || !board.hasAnyMove()) {
        return evaluateBoard(board);
    }
    if (probability < ProbabilityCutoff) {
        return evaluateBoard(board);
    }

    const CacheKey key{boardKey(board), depth, playerTurn};
    const auto cached = state.cache.find(key);
    if (cached != state.cache.end()) {
        return cached->second;
    }

    double result = 0.0;
    if (playerTurn) {
        double best = -std::numeric_limits<double>::infinity();
        bool found = false;

        for (const Direction direction : Directions) {
            Board candidate = board;
            if (!candidate.move(direction)) {
                continue;
            }
            best = std::max(best, expectimax(state, candidate, spawnModel, depth - 1, false, chanceCells, probability));
            found = true;
        }

        result = found ? best : evaluateBoard(board);
        state.cache[key] = result;
        return result;
    }

    const auto empties = board.emptyCells();
    if (empties.empty()) {
        result = expectimax(state, board, spawnModel, depth - 1, true, chanceCells, probability);
        state.cache[key] = result;
        return result;
    }

    const int sampledCells = std::min(static_cast<int>(empties.size()), std::max(1, chanceCells));
    double total = 0.0;

    for (int index = 0; index < sampledCells; ++index) {
        const Cell cell = empties[static_cast<std::size_t>(index)];
        for (const auto& spawn : spawnModel) {
            Board candidate = board;
            if (candidate.spawn(cell, spawn.value)) {
                const double branchProbability = probability * (1.0 / sampledCells) * spawn.probability;
                total += (1.0 / sampledCells) * spawn.probability * expectimax(state, candidate, spawnModel, depth - 1, true, chanceCells, branchProbability);
            }
        }
    }

    result = total;
    state.cache[key] = result;
    return result;
}

bool spawnRandomFromModel(Board& board, const std::vector<SpawnProbability>& spawnModel, std::mt19937& rng) {
    const auto empties = board.emptyCells();
    if (empties.empty()) {
        return false;
    }

    std::uniform_int_distribution<std::size_t> cellDist(0, empties.size() - 1);
    std::uniform_real_distribution<double> probabilityDist(0.0, 1.0);

    const double sample = probabilityDist(rng);
    double accumulated = 0.0;
    int value = spawnModel.back().value;
    for (const auto& spawn : spawnModel) {
        accumulated += spawn.probability;
        if (sample <= accumulated) {
            value = spawn.value;
            break;
        }
    }

    return board.spawn(empties[cellDist(rng)], value);
}

std::vector<Direction> validDirections(const Board& board) {
    std::vector<Direction> directions;
    for (const Direction direction : Directions) {
        Board candidate = board;
        if (candidate.move(direction)) {
            directions.push_back(direction);
        }
    }
    return directions;
}

Direction chooseRolloutDirection(const Board& board, std::mt19937& rng) {
    const auto directions = validDirections(board);
    if (directions.empty()) {
        return Direction::Up;
    }

    std::uniform_real_distribution<double> chance(0.0, 1.0);
    if (chance(rng) < 0.2) {
        std::uniform_int_distribution<std::size_t> dist(0, directions.size() - 1);
        return directions[dist(rng)];
    }

    Direction bestDirection = directions.front();
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const Direction direction : directions) {
        Board candidate = board;
        candidate.move(direction);
        const double score = evaluateBoard(candidate);
        if (score > bestScore) {
            bestScore = score;
            bestDirection = direction;
        }
    }
    return bestDirection;
}

double runMonteCarlo(Board board, const std::vector<SpawnProbability>& spawnModel, int maxMoves, std::mt19937& rng) {
    int moves = 0;

    while (moves < maxMoves && board.hasAnyMove()) {
        spawnRandomFromModel(board, spawnModel, rng);
        if (!board.hasAnyMove()) {
            break;
        }

        const Direction direction = chooseRolloutDirection(board, rng);
        if (!board.move(direction)) {
            break;
        }
        ++moves;
    }

    return evaluateBoard(board) + board.score() * 0.25 + moves * 1.5;
}

}  // namespace

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

std::optional<MoveSuggestion> suggestMove(const Board& board) {
    return suggestMove(board, {{1, 0.9}, {2, 0.1}}, DefaultDepth);
}

std::optional<MoveSuggestion> suggestMove(const Board& board, const std::vector<SpawnProbability>& spawnModel, int depth) {
    return suggestMove(board, spawnModel, depth, 12);
}

std::optional<MoveSuggestion> suggestMove(const Board& board, const std::vector<SpawnProbability>& spawnModel, int depth, int chanceCells) {
    const auto model = normalizedModel(spawnModel);
    const int searchDepth = std::max(1, depth);
    const int sampledChanceCells = std::max(1, chanceCells);
    SearchState state;
    MoveSuggestion best;
    best.score = -std::numeric_limits<double>::infinity();
    best.depth = searchDepth;
    bool found = false;

    for (const Direction direction : Directions) {
        Board candidate = board;
        if (!candidate.move(direction)) {
            continue;
        }

        const double score = expectimax(state, candidate, model, searchDepth - 1, false, sampledChanceCells, 1.0);

        if (!found || score > best.score) {
            best = {direction, score, searchDepth, 0, "expectimax"};
            found = true;
        }
    }

    if (!found) {
        return std::nullopt;
    }
    return best;
}

std::optional<MoveSuggestion> suggestHybridMove(const Board& board, const std::vector<SpawnProbability>& spawnModel, const HybridOptions& options) {
    const auto analyses = analyzeHybridMoves(board, spawnModel, options);
    if (analyses.empty()) {
        return std::nullopt;
    }
    const auto& best = analyses.front();
    return MoveSuggestion{best.direction, best.score, best.depth, best.rollouts, best.solver};
}

std::vector<MoveAnalysis> analyzeHybridMoves(const Board& board, const std::vector<SpawnProbability>& spawnModel, const HybridOptions& options) {
    const auto model = normalizedModel(spawnModel);
    const int depth = std::max(1, options.depth);
    const int rollouts = std::max(1, options.rollouts);
    const int candidateLimit = std::max(1, options.candidates);
    const int maxRolloutMoves = std::max(1, options.maxRolloutMoves);
    const int chanceCells = std::max(1, options.chanceCells);

    struct Candidate {
        Direction direction = Direction::Up;
        Board board;
        double expectimaxScore = 0.0;
        double rolloutScore = 0.0;
        double combinedScore = 0.0;
    };

    std::vector<Candidate> candidates;
    SearchState state;
    for (const Direction direction : Directions) {
        Board candidateBoard = board;
        if (!candidateBoard.move(direction)) {
            continue;
        }

        const double score = expectimax(state, candidateBoard, model, depth - 1, false, chanceCells, 1.0);
        candidates.push_back({direction, candidateBoard, score, 0.0, score});
    }

    if (candidates.empty()) {
        return {};
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.expectimaxScore > rhs.expectimaxScore;
    });
    if (static_cast<int>(candidates.size()) > candidateLimit) {
        candidates.resize(static_cast<std::size_t>(candidateLimit));
    }

    std::mt19937 rng(options.seed);
    const int rolloutsPerCandidate = std::max(1, rollouts / static_cast<int>(candidates.size()));
    for (auto& candidate : candidates) {
        double total = 0.0;
        for (int rollout = 0; rollout < rolloutsPerCandidate; ++rollout) {
            total += runMonteCarlo(candidate.board, model, maxRolloutMoves, rng);
        }
        candidate.rolloutScore = total / rolloutsPerCandidate;
    }

    const auto expectimaxBounds = std::minmax_element(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.expectimaxScore < rhs.expectimaxScore;
    });
    const auto rolloutBounds = std::minmax_element(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.rolloutScore < rhs.rolloutScore;
    });
    const double expectimaxRange = std::max(1.0, expectimaxBounds.second->expectimaxScore - expectimaxBounds.first->expectimaxScore);
    const double rolloutRange = std::max(1.0, rolloutBounds.second->rolloutScore - rolloutBounds.first->rolloutScore);

    for (auto& candidate : candidates) {
        const double normalizedExpectimax = (candidate.expectimaxScore - expectimaxBounds.first->expectimaxScore) / expectimaxRange;
        const double normalizedRollout = (candidate.rolloutScore - rolloutBounds.first->rolloutScore) / rolloutRange;
        candidate.combinedScore = normalizedExpectimax * 0.6 + normalizedRollout * 0.4;
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.combinedScore > rhs.combinedScore;
    });

    std::vector<MoveAnalysis> analyses;
    for (const auto& candidate : candidates) {
        analyses.push_back({candidate.direction, candidate.combinedScore, depth, rolloutsPerCandidate * static_cast<int>(candidates.size()), "hybrid"});
    }
    return analyses;
}
