#include "solver.hpp"

#include "evaluator.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <unordered_map>
#include <vector>

namespace {

constexpr int DefaultDepth = 6;
constexpr double ProbabilityCutoff = 0.00002;

const std::array<Direction, 4> Directions{
    Direction::Up,
    Direction::Down,
    Direction::Left,
    Direction::Right,
};

double tileWeight(int rank) {
    if (rank <= 0) {
        return 0.0;
    }
    return static_cast<double>(1 << std::min(rank, 20));
}

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
    std::uint64_t key = 1469598103934665603ULL;
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            key ^= static_cast<std::uint64_t>(board.at(row, col) + 0x9e3779b9U);
            key *= 1099511628211ULL;
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

double expectimax(SearchState& state, const Board& board, const std::vector<SpawnProbability>& spawnModel, int depth, bool playerTurn, int chanceCells, double probability, double chanceOptimism) {
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
            best = std::max(best, expectimax(state, candidate, spawnModel, depth - 1, false, chanceCells, probability, chanceOptimism));
            found = true;
        }

        result = found ? best : evaluateBoard(board);
        state.cache[key] = result;
        return result;
    }

    const auto empties = board.emptyCells();
    if (empties.empty()) {
        result = expectimax(state, board, spawnModel, depth - 1, true, chanceCells, probability, chanceOptimism);
        state.cache[key] = result;
        return result;
    }

    const int sampledCells = std::min(static_cast<int>(empties.size()), std::max(1, chanceCells));
    double total = 0.0;
    double bestBranch = -std::numeric_limits<double>::infinity();

    for (int index = 0; index < sampledCells; ++index) {
        const std::size_t cellIndex = sampledCells == static_cast<int>(empties.size())
            ? static_cast<std::size_t>(index)
            : static_cast<std::size_t>(index * static_cast<int>(empties.size()) / sampledCells);
        const Cell cell = empties[cellIndex];
        for (const auto& spawn : spawnModel) {
            Board candidate = board;
            if (candidate.spawn(cell, spawn.value)) {
                const double branchProbability = probability * (1.0 / sampledCells) * spawn.probability;
                const double branchValue = expectimax(state, candidate, spawnModel, depth - 1, true, chanceCells, branchProbability, chanceOptimism);
                total += (1.0 / sampledCells) * spawn.probability * branchValue;
                bestBranch = std::max(bestBranch, branchValue);
            }
        }
    }

    result = total;
    if (chanceOptimism > 0.0 && bestBranch > result) {
        result += chanceOptimism * (bestBranch - result);
    }
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

double rolloutMoveScore(const Board& board) {
    return evaluateBoard(board) + static_cast<double>(board.score()) * 0.20 + static_cast<double>(board.emptyCount()) * 420.0;
}

Direction chooseRolloutDirection(const Board& board, std::mt19937& rng) {
    const auto directions = validDirections(board);
    if (directions.empty()) {
        return Direction::Up;
    }

    std::uniform_real_distribution<double> chance(0.0, 1.0);
    if (chance(rng) < 0.1) {
        std::uniform_int_distribution<std::size_t> dist(0, directions.size() - 1);
        return directions[dist(rng)];
    }

    Direction bestDirection = directions.front();
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const Direction direction : directions) {
        Board candidate = board;
        candidate.move(direction);
        const double score = rolloutMoveScore(candidate);
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

double targetProgressScore(const Board& board, int targetRank) {
    if (!board.hasAnyMove()) {
        return -1.0e9;
    }
    if (board.highestTile() >= targetRank) {
        return 1.0e12 + board.score();
    }

    std::array<int, 32> counts{};
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            const int rank = board.at(row, col);
            if (rank > 0 && rank < static_cast<int>(counts.size())) {
                ++counts[static_cast<std::size_t>(rank)];
            }
        }
    }

    double score = evaluateBoard(board) + board.score() * 0.4 + board.emptyCount() * 850.0;
    const int highest = board.highestTile();
    score += tileWeight(highest) * 650.0;

    const int firstImportantRank = std::max(1, targetRank - 5);
    for (int rank = firstImportantRank; rank < targetRank && rank < static_cast<int>(counts.size()); ++rank) {
        const int count = counts[static_cast<std::size_t>(rank)];
        if (count == 0) {
            continue;
        }
        const double urgency = static_cast<double>(rank - firstImportantRank + 1);
        score += tileWeight(rank) * urgency * std::min(count, 2) * 120.0;
        if (count >= 2) {
            score += tileWeight(rank + 1) * urgency * 480.0;
        }
    }

    const int distance = std::max(0, targetRank - highest);
    score -= std::pow(static_cast<double>(distance), 2.0) * 22000.0;
    return score;
}

Direction chooseTargetRolloutDirection(const Board& board, const std::vector<SpawnProbability>& spawnModel, int targetRank, std::mt19937& rng) {
    const auto directions = validDirections(board);
    if (directions.empty()) {
        return Direction::Up;
    }

    std::uniform_real_distribution<double> chance(0.0, 1.0);
    if (chance(rng) < 0.08) {
        std::uniform_int_distribution<std::size_t> dist(0, directions.size() - 1);
        return directions[dist(rng)];
    }

    Direction bestDirection = directions.front();
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const Direction direction : directions) {
        Board moved = board;
        moved.move(direction);
        const auto empties = moved.emptyCells();
        if (empties.empty()) {
            const double score = targetProgressScore(moved, targetRank);
            if (score > bestScore) {
                bestScore = score;
                bestDirection = direction;
            }
            continue;
        }

        double expected = 0.0;
        const double cellProbability = 1.0 / static_cast<double>(empties.size());
        for (const Cell cell : empties) {
            for (const auto& spawn : spawnModel) {
                Board candidate = moved;
                if (candidate.spawn(cell, spawn.value)) {
                    expected += cellProbability * spawn.probability * targetProgressScore(candidate, targetRank);
                }
            }
        }
        if (expected > bestScore) {
            bestScore = expected;
            bestDirection = direction;
        }
    }
    return bestDirection;
}

struct TargetRolloutResult {
    int score = 0;
    int moves = 0;
    int highest = 0;
    bool success = false;
};

TargetRolloutResult runTargetRollout(Board board, const std::vector<SpawnProbability>& spawnModel, const TargetOptions& options, std::mt19937& rng) {
    int moves = 0;
    const int maxMoves = std::max(1, options.maxMoves);
    const int targetRank = std::max(1, options.targetRank);

    while (moves < maxMoves && board.hasAnyMove() && board.highestTile() < targetRank) {
        spawnRandomFromModel(board, spawnModel, rng);
        if (!board.hasAnyMove() || board.highestTile() >= targetRank) {
            break;
        }

        const Direction direction = chooseTargetRolloutDirection(board, spawnModel, targetRank, rng);
        if (!board.move(direction)) {
            break;
        }
        ++moves;
    }

    return {board.score(), moves, board.highestTile(), board.highestTile() >= targetRank};
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
    const auto analyses = analyzeExpectimaxMoves(board, spawnModel, depth, chanceCells);
    if (analyses.empty()) {
        return std::nullopt;
    }
    const auto& best = analyses.front();
    return MoveSuggestion{best.direction, best.score, best.depth, best.rollouts, best.solver};
}

std::vector<MoveAnalysis> analyzeExpectimaxMoves(const Board& board, const std::vector<SpawnProbability>& spawnModel, int depth, int chanceCells) {
    const auto model = normalizedModel(spawnModel);
    const int searchDepth = std::max(1, depth);
    const int sampledChanceCells = std::max(1, chanceCells);
    SearchState state;
    state.cache.reserve(120000);
    std::vector<MoveAnalysis> analyses;

    for (const Direction direction : Directions) {
        Board candidate = board;
        if (!candidate.move(direction)) {
            continue;
        }

        const double score = expectimax(state, candidate, model, searchDepth - 1, false, sampledChanceCells, 1.0, 0.0);
        analyses.push_back({direction, score, searchDepth, 0, "expectimax"});
    }

    std::sort(analyses.begin(), analyses.end(), [](const MoveAnalysis& lhs, const MoveAnalysis& rhs) {
        return lhs.score > rhs.score;
    });
    return analyses;
}

std::optional<MoveSuggestion> suggestOptimisticMove(const Board& board, const std::vector<SpawnProbability>& spawnModel, int depth, int chanceCells) {
    const auto analyses = analyzeOptimisticMoves(board, spawnModel, depth, chanceCells);
    if (analyses.empty()) {
        return std::nullopt;
    }
    const auto& best = analyses.front();
    return MoveSuggestion{best.direction, best.score, best.depth, best.rollouts, best.solver};
}

std::vector<MoveAnalysis> analyzeOptimisticMoves(const Board& board, const std::vector<SpawnProbability>& spawnModel, int depth, int chanceCells) {
    constexpr double ChanceOptimism = 0.18;
    const auto model = normalizedModel(spawnModel);
    const int searchDepth = std::max(1, depth);
    const int sampledChanceCells = std::max(1, chanceCells);
    SearchState state;
    state.cache.reserve(120000);
    std::vector<MoveAnalysis> analyses;

    for (const Direction direction : Directions) {
        Board candidate = board;
        if (!candidate.move(direction)) {
            continue;
        }

        const double score = expectimax(state, candidate, model, searchDepth - 1, false, sampledChanceCells, 1.0, ChanceOptimism);
        analyses.push_back({direction, score, searchDepth, 0, "optimistic"});
    }

    std::sort(analyses.begin(), analyses.end(), [](const MoveAnalysis& lhs, const MoveAnalysis& rhs) {
        return lhs.score > rhs.score;
    });
    return analyses;
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
    state.cache.reserve(120000);
    for (const Direction direction : Directions) {
        Board candidateBoard = board;
        if (!candidateBoard.move(direction)) {
            continue;
        }

        const double score = expectimax(state, candidateBoard, model, depth - 1, false, chanceCells, 1.0, 0.0);
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
    const double rolloutWeight = depth >= 6 ? 0.22 : 0.35;
    const double expectimaxWeight = 1.0 - rolloutWeight;

    for (auto& candidate : candidates) {
        const double normalizedExpectimax = (candidate.expectimaxScore - expectimaxBounds.first->expectimaxScore) / expectimaxRange;
        const double normalizedRollout = (candidate.rolloutScore - rolloutBounds.first->rolloutScore) / rolloutRange;
        candidate.combinedScore = normalizedExpectimax * expectimaxWeight + normalizedRollout * rolloutWeight;
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

std::optional<MoveSuggestion> suggestTargetMove(const Board& board, const std::vector<SpawnProbability>& spawnModel, const TargetOptions& options) {
    const auto analyses = analyzeTargetMoves(board, spawnModel, options);
    if (analyses.empty()) {
        return std::nullopt;
    }
    const auto& best = analyses.front();
    return MoveSuggestion{best.direction, best.score, best.depth, best.rollouts, best.solver};
}

std::vector<MoveAnalysis> analyzeTargetMoves(const Board& board, const std::vector<SpawnProbability>& spawnModel, const TargetOptions& options) {
    const auto model = normalizedModel(spawnModel);
    const int rollouts = std::max(1, options.rollouts);
    const int maxMoves = std::max(1, options.maxMoves);
    const int targetRank = std::max(1, options.targetRank);

    struct Candidate {
        Direction direction = Direction::Up;
        double score = 0.0;
        int successes = 0;
        double totalHighest = 0.0;
        double totalScore = 0.0;
        double totalMoves = 0.0;
    };

    std::vector<Candidate> candidates;
    const auto directions = validDirections(board);
    if (directions.empty()) {
        return {};
    }

    const int rolloutsPerCandidate = std::max(1, rollouts / static_cast<int>(directions.size()));
    for (const Direction direction : directions) {
        Board candidateBoard = board;
        if (!candidateBoard.move(direction)) {
            continue;
        }

        std::mt19937 rng(options.seed ^ (static_cast<unsigned int>(static_cast<int>(direction) + 1) * 0x9e3779b9U));
        Candidate candidate;
        candidate.direction = direction;
        for (int rollout = 0; rollout < rolloutsPerCandidate; ++rollout) {
            const auto result = runTargetRollout(candidateBoard, model, {rollouts, maxMoves, targetRank, options.seed}, rng);
            candidate.successes += result.success ? 1 : 0;
            candidate.totalHighest += result.highest;
            candidate.totalScore += result.score;
            candidate.totalMoves += result.moves;
        }

        const double successRate = static_cast<double>(candidate.successes) / static_cast<double>(rolloutsPerCandidate);
        const double averageHighest = candidate.totalHighest / static_cast<double>(rolloutsPerCandidate);
        const double averageScore = candidate.totalScore / static_cast<double>(rolloutsPerCandidate);
        const double averageMoves = candidate.totalMoves / static_cast<double>(rolloutsPerCandidate);
        candidate.score =
            successRate * 1.0e9 +
            averageHighest * 1.0e6 +
            averageScore * 12.0 +
            averageMoves * 120.0 +
            targetProgressScore(candidateBoard, targetRank) * 0.03;
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        return lhs.score > rhs.score;
    });

    std::vector<MoveAnalysis> analyses;
    for (const auto& candidate : candidates) {
        analyses.push_back({candidate.direction, candidate.score, maxMoves, rolloutsPerCandidate * static_cast<int>(candidates.size()), "target"});
    }
    return analyses;
}
