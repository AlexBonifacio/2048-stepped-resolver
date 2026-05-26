#include "evaluator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

double tileWeight(int rank) {
    if (rank <= 0) {
        return 0.0;
    }
    return static_cast<double>(1 << std::min(rank, 20));
}

double snakeScoreFor(const Board& board, const std::array<int, Board::CellCount>& weights) {
    double score = 0.0;
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            const int index = row * Board::Size + col;
            score += tileWeight(board.at(row, col)) * weights[static_cast<std::size_t>(index)];
        }
    }
    return score;
}

double bestSnakeScore(const Board& board) {
    static constexpr std::array<std::array<int, Board::CellCount>, 4> weights{{
        {32768, 16384, 8192, 4096, 256, 512, 1024, 2048, 128, 64, 32, 16, 1, 2, 4, 8},
        {4096, 8192, 16384, 32768, 2048, 1024, 512, 256, 16, 32, 64, 128, 8, 4, 2, 1},
        {8, 4, 2, 1, 16, 32, 64, 128, 2048, 1024, 512, 256, 4096, 8192, 16384, 32768},
        {1, 2, 4, 8, 128, 64, 32, 16, 256, 512, 1024, 2048, 32768, 16384, 8192, 4096},
    }};

    double best = -std::numeric_limits<double>::infinity();
    for (const auto& weight : weights) {
        best = std::max(best, snakeScoreFor(board, weight));
    }
    return best;
}

double stableSnakeScore(const Board& board) {
    static constexpr std::array<std::array<int, Board::CellCount>, 4> weights{{
        {32768, 16384, 8192, 4096, 256, 512, 1024, 2048, 128, 64, 32, 16, 1, 2, 4, 8},
        {4096, 8192, 16384, 32768, 2048, 1024, 512, 256, 16, 32, 64, 128, 8, 4, 2, 1},
        {8, 4, 2, 1, 16, 32, 64, 128, 2048, 1024, 512, 256, 4096, 8192, 16384, 32768},
        {1, 2, 4, 8, 128, 64, 32, 16, 256, 512, 1024, 2048, 32768, 16384, 8192, 4096},
    }};

    const int highest = board.highestTile();
    if (board.at(0, 0) == highest) {
        return snakeScoreFor(board, weights[0]);
    }
    if (board.at(0, Board::Size - 1) == highest) {
        return snakeScoreFor(board, weights[1]);
    }
    if (board.at(Board::Size - 1, Board::Size - 1) == highest) {
        return snakeScoreFor(board, weights[2]);
    }
    if (board.at(Board::Size - 1, 0) == highest) {
        return snakeScoreFor(board, weights[3]);
    }
    return bestSnakeScore(board);
}

bool highestInCorner(const Board& board) {
    const int highest = board.highestTile();
    return board.at(0, 0) == highest ||
           board.at(0, Board::Size - 1) == highest ||
           board.at(Board::Size - 1, 0) == highest ||
           board.at(Board::Size - 1, Board::Size - 1) == highest;
}

double mergePotential(const Board& board) {
    double score = 0.0;
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            const int current = board.at(row, col);
            if (current == 0) {
                continue;
            }
            if (row + 1 < Board::Size && board.at(row + 1, col) == current) {
                score += tileWeight(current + 1);
            }
            if (col + 1 < Board::Size && board.at(row, col + 1) == current) {
                score += tileWeight(current + 1);
            }
        }
    }
    return score;
}

double roughnessPenalty(const Board& board) {
    double penalty = 0.0;
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            const int current = board.at(row, col);
            if (current == 0) {
                continue;
            }
            if (row + 1 < Board::Size && board.at(row + 1, col) != 0) {
                penalty += std::abs(current - board.at(row + 1, col));
            }
            if (col + 1 < Board::Size && board.at(row, col + 1) != 0) {
                penalty += std::abs(current - board.at(row, col + 1));
            }
        }
    }
    return penalty;
}

double lineHeuristic(const std::array<int, Board::Size>& line) {
    constexpr double MonotonicityPower = 4.0;
    constexpr double MonotonicityWeight = 47.0;
    constexpr double SumPower = 3.5;
    constexpr double SumWeight = 11.0;
    constexpr double MergesWeight = 700.0;
    constexpr double EmptyWeight = 270.0;

    double sum = 0.0;
    int empty = 0;
    int merges = 0;
    int previous = 0;
    int counter = 0;

    for (int rank : line) {
        sum += std::pow(rank, SumPower);
        if (rank == 0) {
            ++empty;
            continue;
        }

        if (previous == rank) {
            ++counter;
        } else if (counter > 0) {
            merges += 1 + counter;
            counter = 0;
        }
        previous = rank;
    }
    if (counter > 0) {
        merges += 1 + counter;
    }

    double monotonicityLeft = 0.0;
    double monotonicityRight = 0.0;
    for (int i = 1; i < Board::Size; ++i) {
        if (line[static_cast<std::size_t>(i - 1)] > line[static_cast<std::size_t>(i)]) {
            monotonicityLeft += std::pow(line[static_cast<std::size_t>(i - 1)], MonotonicityPower) -
                                std::pow(line[static_cast<std::size_t>(i)], MonotonicityPower);
        } else {
            monotonicityRight += std::pow(line[static_cast<std::size_t>(i)], MonotonicityPower) -
                                 std::pow(line[static_cast<std::size_t>(i - 1)], MonotonicityPower);
        }
    }

    return EmptyWeight * empty +
           MergesWeight * merges -
           MonotonicityWeight * std::min(monotonicityLeft, monotonicityRight) -
           SumWeight * sum;
}

double nneonneoLineScore(const Board& board) {
    double score = 0.0;
    for (int row = 0; row < Board::Size; ++row) {
        std::array<int, Board::Size> line{};
        for (int col = 0; col < Board::Size; ++col) {
            line[static_cast<std::size_t>(col)] = board.at(row, col);
        }
        score += lineHeuristic(line);
    }
    for (int col = 0; col < Board::Size; ++col) {
        std::array<int, Board::Size> line{};
        for (int row = 0; row < Board::Size; ++row) {
            line[static_cast<std::size_t>(row)] = board.at(row, col);
        }
        score += lineHeuristic(line);
    }
    return score;
}

}  // namespace

double evaluateBoard(const Board& board) {
    double value = 0.0;
    value += board.emptyCount() * 1200.0;
    value += tileWeight(board.highestTile()) * 12.0;
    value += stableSnakeScore(board) * 0.008;
    value += nneonneoLineScore(board) * 0.12;
    value += mergePotential(board) * 3.0;
    value -= roughnessPenalty(board) * 35.0;

    if (highestInCorner(board)) {
        value += tileWeight(board.highestTile()) * 80.0;
    } else {
        value -= tileWeight(board.highestTile()) * 140.0;
    }

    return value;
}
