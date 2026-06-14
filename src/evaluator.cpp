#include "evaluator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace {

enum class Corner {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

static constexpr std::array<int, Board::CellCount> SnakeTopLeft{
    65536, 32768, 16384, 8192,
    512, 1024, 2048, 4096,
    256, 128, 64, 32,
    2, 4, 8, 16,
};

static constexpr std::array<int, Board::CellCount> SnakeTopRight{
    8192, 16384, 32768, 65536,
    4096, 2048, 1024, 512,
    32, 64, 128, 256,
    16, 8, 4, 2,
};

static constexpr std::array<int, Board::CellCount> SnakeBottomLeft{
    2, 4, 8, 16,
    256, 128, 64, 32,
    512, 1024, 2048, 4096,
    65536, 32768, 16384, 8192,
};

static constexpr std::array<int, Board::CellCount> SnakeBottomRight{
    16, 8, 4, 2,
    32, 64, 128, 256,
    4096, 2048, 1024, 512,
    8192, 16384, 32768, 65536,
};

static constexpr std::array<std::array<int, Board::CellCount>, 4> SnakeWeights{{
    SnakeTopLeft,
    SnakeTopRight,
    SnakeBottomLeft,
    SnakeBottomRight,
}};

double tileWeight(int rank) {
    if (rank <= 0) {
        return 0.0;
    }
    return static_cast<double>(1 << std::min(rank, 20));
}

int distanceFromCorner(Corner corner, int row, int col) {
    switch (corner) {
        case Corner::TopLeft:
            return row + col;
        case Corner::TopRight:
            return row + (Board::Size - 1 - col);
        case Corner::BottomLeft:
            return (Board::Size - 1 - row) + col;
        case Corner::BottomRight:
            return (Board::Size - 1 - row) + (Board::Size - 1 - col);
    }
    return 0;
}

bool isStairCell(Corner corner, int row, int col) {
    return distanceFromCorner(corner, row, col) < Board::Size;
}

std::optional<Corner> highestCorner(const Board& board) {
    const int highest = board.highestTile();
    if (board.at(0, 0) == highest) {
        return Corner::TopLeft;
    }
    if (board.at(0, Board::Size - 1) == highest) {
        return Corner::TopRight;
    }
    if (board.at(Board::Size - 1, 0) == highest) {
        return Corner::BottomLeft;
    }
    if (board.at(Board::Size - 1, Board::Size - 1) == highest) {
        return Corner::BottomRight;
    }
    return std::nullopt;
}

int cornerIndex(Corner corner) {
    switch (corner) {
        case Corner::TopLeft:
            return 0;
        case Corner::TopRight:
            return 1;
        case Corner::BottomLeft:
            return 2;
        case Corner::BottomRight:
            return 3;
    }
    return 0;
}

std::array<Cell, 2> awayNeighbors(Corner corner, int row, int col) {
    switch (corner) {
        case Corner::TopLeft:
            return {Cell{row + 1, col}, Cell{row, col + 1}};
        case Corner::TopRight:
            return {Cell{row + 1, col}, Cell{row, col - 1}};
        case Corner::BottomLeft:
            return {Cell{row - 1, col}, Cell{row, col + 1}};
        case Corner::BottomRight:
            return {Cell{row - 1, col}, Cell{row, col - 1}};
    }
    return {Cell{row, col}, Cell{row, col}};
}

std::array<Cell, 2> supportNeighbors(Corner corner, int row, int col) {
    switch (corner) {
        case Corner::TopLeft:
            return {Cell{row - 1, col}, Cell{row, col - 1}};
        case Corner::TopRight:
            return {Cell{row - 1, col}, Cell{row, col + 1}};
        case Corner::BottomLeft:
            return {Cell{row + 1, col}, Cell{row, col - 1}};
        case Corner::BottomRight:
            return {Cell{row + 1, col}, Cell{row, col + 1}};
    }
    return {Cell{row, col}, Cell{row, col}};
}

bool hasSupportTowardCorner(const Board& board, Corner corner, int row, int col) {
    if (distanceFromCorner(corner, row, col) == 0) {
        return true;
    }

    for (const Cell cell : supportNeighbors(corner, row, col)) {
        if (board.isInside(cell.row, cell.col) && board.at(cell.row, cell.col) > 0) {
            return true;
        }
    }
    return false;
}

double staircaseScoreFor(const Board& board, Corner corner) {
    double score = 0.0;
    double directionalPenalty = 0.0;

    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            const int rank = board.at(row, col);
            const int distance = distanceFromCorner(corner, row, col);
            const bool inside = isStairCell(corner, row, col);

            if (inside) {
                if (rank > 0) {
                    score += tileWeight(rank) * (84.0 / static_cast<double>(distance + 1));
                    if (!hasSupportTowardCorner(board, corner, row, col)) {
                        score -= tileWeight(rank) * 42.0;
                    }
                } else {
                    score -= 850.0 * static_cast<double>(Board::Size - distance);
                }
            } else if (rank == 0) {
                score += 700.0;
            } else {
                score -= tileWeight(rank) * 46.0;
            }

            if (rank <= 0) {
                continue;
            }
            for (const Cell cell : awayNeighbors(corner, row, col)) {
                if (!board.isInside(cell.row, cell.col)) {
                    continue;
                }
                const int neighbor = board.at(cell.row, cell.col);
                if (neighbor > rank) {
                    const double diff = static_cast<double>(neighbor - rank);
                    directionalPenalty += diff * diff * 3200.0;
                }
            }
        }
    }

    return score - directionalPenalty;
}

double stableStaircaseScore(const Board& board) {
    if (const auto corner = highestCorner(board)) {
        return staircaseScoreFor(board, *corner);
    }

    double best = -std::numeric_limits<double>::infinity();
    for (const Corner corner : {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft, Corner::BottomRight}) {
        best = std::max(best, staircaseScoreFor(board, corner));
    }
    return best;
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
    double best = -std::numeric_limits<double>::infinity();
    for (const auto& weight : SnakeWeights) {
        best = std::max(best, snakeScoreFor(board, weight));
    }
    return best;
}

double stableSnakeScore(const Board& board) {
    if (const auto corner = highestCorner(board)) {
        return snakeScoreFor(board, SnakeWeights[static_cast<std::size_t>(cornerIndex(*corner))]);
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

double lineMonotonicityPenalty(const std::array<int, Board::Size>& line) {
    double increasing = 0.0;
    double decreasing = 0.0;
    for (int index = 1; index < Board::Size; ++index) {
        const int left = line[static_cast<std::size_t>(index - 1)];
        const int right = line[static_cast<std::size_t>(index)];
        const double diff = static_cast<double>(std::abs(left - right));
        if (left > right) {
            decreasing += diff * diff;
        } else {
            increasing += diff * diff;
        }
    }
    return std::min(increasing, decreasing);
}

double monotonicityPenalty(const Board& board) {
    double penalty = 0.0;
    for (int row = 0; row < Board::Size; ++row) {
        std::array<int, Board::Size> line{};
        for (int col = 0; col < Board::Size; ++col) {
            line[static_cast<std::size_t>(col)] = board.at(row, col);
        }
        penalty += lineMonotonicityPenalty(line);
    }
    for (int col = 0; col < Board::Size; ++col) {
        std::array<int, Board::Size> line{};
        for (int row = 0; row < Board::Size; ++row) {
            line[static_cast<std::size_t>(row)] = board.at(row, col);
        }
        penalty += lineMonotonicityPenalty(line);
    }
    return penalty;
}

double homogeneityPenalty(const Board& board) {
    double penalty = 0.0;
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            const int current = board.at(row, col);
            if (current == 0) {
                continue;
            }
            if (row + 1 < Board::Size) {
                const int neighbor = board.at(row + 1, col);
                if (neighbor > 0) {
                    const double diff = static_cast<double>(std::abs(current - neighbor));
                    penalty += diff * diff * (1.0 + static_cast<double>(std::max(current, neighbor)) * 0.45);
                }
            }
            if (col + 1 < Board::Size) {
                const int neighbor = board.at(row, col + 1);
                if (neighbor > 0) {
                    const double diff = static_cast<double>(std::abs(current - neighbor));
                    penalty += diff * diff * (1.0 + static_cast<double>(std::max(current, neighbor)) * 0.45);
                }
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
    if (!board.hasAnyMove()) {
        return -1.0e9 + tileWeight(board.highestTile());
    }

    double value = 0.0;
    value += board.emptyCount() * 1450.0;
    value += tileWeight(board.highestTile()) * 12.0;
    value += stableSnakeScore(board) * 0.010;
    value += stableStaircaseScore(board) * 0.0;
    value += nneonneoLineScore(board) * 0.08;
    value += mergePotential(board) * 4.0;
    value -= monotonicityPenalty(board) * 620.0;
    value -= homogeneityPenalty(board) * 85.0;
    value -= roughnessPenalty(board) * 20.0;

    if (highestInCorner(board)) {
        value += tileWeight(board.highestTile()) * 120.0;
    } else {
        value -= tileWeight(board.highestTile()) * 220.0;
    }

    return value;
}
