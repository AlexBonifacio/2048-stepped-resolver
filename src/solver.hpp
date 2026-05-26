#pragma once

#include "board.hpp"

#include <optional>
#include <string>
#include <vector>

struct SpawnProbability {
    int value = 1;
    double probability = 1.0;
};

struct SpawnSuggestion {
    Cell cell;
    int value = 1;
    double score = 0.0;
};

struct MoveSuggestion {
    Direction direction = Direction::Up;
    double score = 0.0;
    int depth = 0;
    int rollouts = 0;
    std::string solver = "expectimax";
};

struct MoveAnalysis {
    Direction direction = Direction::Up;
    double score = 0.0;
    int depth = 0;
    int rollouts = 0;
    std::string solver = "expectimax";
};

struct HybridOptions {
    int depth = 4;
    int rollouts = 200;
    int candidates = 2;
    int maxRolloutMoves = 80;
    int chanceCells = 12;
    unsigned int seed = 0x2048;
};

std::optional<SpawnSuggestion> suggestSpawn(const Board& board, int value);
std::optional<MoveSuggestion> suggestMove(const Board& board);
std::optional<MoveSuggestion> suggestMove(const Board& board, const std::vector<SpawnProbability>& spawnModel, int depth);
std::optional<MoveSuggestion> suggestMove(const Board& board, const std::vector<SpawnProbability>& spawnModel, int depth, int chanceCells);
std::optional<MoveSuggestion> suggestHybridMove(const Board& board, const std::vector<SpawnProbability>& spawnModel, const HybridOptions& options);
std::vector<MoveAnalysis> analyzeHybridMoves(const Board& board, const std::vector<SpawnProbability>& spawnModel, const HybridOptions& options);
