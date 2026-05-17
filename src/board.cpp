#include "board.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>

Board::Board() = default;

int Board::at(int row, int col) const {
    return cells_[static_cast<std::size_t>(row * Size + col)];
}

int& Board::ref(int row, int col) {
    return cells_[static_cast<std::size_t>(row * Size + col)];
}

bool Board::isInside(int row, int col) const {
    return row >= 0 && row < Size && col >= 0 && col < Size;
}

bool Board::isEmpty(int row, int col) const {
    return isInside(row, col) && at(row, col) == 0;
}

bool Board::setCell(int row, int col, int value) {
    if (!isInside(row, col) || value < 0) {
        return false;
    }
    ref(row, col) = value;
    return true;
}

bool Board::spawn(Cell cell, int value) {
    if (value <= 0 || !isEmpty(cell.row, cell.col)) {
        return false;
    }
    ref(cell.row, cell.col) = value;
    return true;
}

bool Board::spawnRandom(std::mt19937& rng, int* spawnedValue) {
    const auto empties = emptyCells();
    if (empties.empty()) {
        return false;
    }

    std::uniform_int_distribution<std::size_t> cellDist(0, empties.size() - 1);
    std::uniform_int_distribution<int> valueDist(1, 10);
    const int value = valueDist(rng) == 10 ? 2 : 1;
    if (spawnedValue != nullptr) {
        *spawnedValue = value;
    }
    return spawn(empties[cellDist(rng)], value);
}

void Board::start(std::mt19937& rng) {
    cells_.fill(0);
    score_ = 0;
    spawnRandom(rng);
    spawnRandom(rng);
}

std::array<int, Board::Size> Board::readLine(Direction direction, int index) const {
    std::array<int, Size> line{};
    for (int offset = 0; offset < Size; ++offset) {
        switch (direction) {
            case Direction::Left:
                line[static_cast<std::size_t>(offset)] = at(index, offset);
                break;
            case Direction::Right:
                line[static_cast<std::size_t>(offset)] = at(index, Size - 1 - offset);
                break;
            case Direction::Up:
                line[static_cast<std::size_t>(offset)] = at(offset, index);
                break;
            case Direction::Down:
                line[static_cast<std::size_t>(offset)] = at(Size - 1 - offset, index);
                break;
        }
    }
    return line;
}

void Board::writeLine(Direction direction, int index, const std::array<int, Size>& line) {
    for (int offset = 0; offset < Size; ++offset) {
        switch (direction) {
            case Direction::Left:
                ref(index, offset) = line[static_cast<std::size_t>(offset)];
                break;
            case Direction::Right:
                ref(index, Size - 1 - offset) = line[static_cast<std::size_t>(offset)];
                break;
            case Direction::Up:
                ref(offset, index) = line[static_cast<std::size_t>(offset)];
                break;
            case Direction::Down:
                ref(Size - 1 - offset, index) = line[static_cast<std::size_t>(offset)];
                break;
        }
    }
}

std::array<int, Board::Size> Board::mergedLine(const std::array<int, Size>& line, int& gainedScore) const {
    std::array<int, Size> compact{};
    int compactCount = 0;
    for (int value : line) {
        if (value != 0) {
            compact[static_cast<std::size_t>(compactCount++)] = value;
        }
    }

    std::array<int, Size> result{};
    int resultCount = 0;
    for (int i = 0; i < compactCount; ++i) {
        if (i + 1 < compactCount && compact[static_cast<std::size_t>(i)] == compact[static_cast<std::size_t>(i + 1)]) {
            const int merged = compact[static_cast<std::size_t>(i)] + 1;
            result[static_cast<std::size_t>(resultCount++)] = merged;
            gainedScore += merged;
            ++i;
        } else {
            result[static_cast<std::size_t>(resultCount++)] = compact[static_cast<std::size_t>(i)];
        }
    }
    return result;
}

bool Board::move(Direction direction) {
    bool changed = false;
    int totalGained = 0;

    for (int index = 0; index < Size; ++index) {
        const auto before = readLine(direction, index);
        int gained = 0;
        const auto after = mergedLine(before, gained);
        if (before != after) {
            changed = true;
            writeLine(direction, index, after);
            totalGained += gained;
        }
    }

    score_ += totalGained;
    return changed;
}

bool Board::canMove(Direction direction) const {
    Board copy = *this;
    return copy.move(direction);
}

bool Board::hasAnyMove() const {
    return canMove(Direction::Up) || canMove(Direction::Down) || canMove(Direction::Left) || canMove(Direction::Right);
}

bool Board::isFull() const {
    return std::none_of(cells_.begin(), cells_.end(), [](int value) { return value == 0; });
}

int Board::score() const {
    return score_;
}

int Board::highestTile() const {
    return *std::max_element(cells_.begin(), cells_.end());
}

int Board::emptyCount() const {
    return static_cast<int>(std::count(cells_.begin(), cells_.end(), 0));
}

std::vector<Cell> Board::emptyCells() const {
    std::vector<Cell> cells;
    for (int row = 0; row < Size; ++row) {
        for (int col = 0; col < Size; ++col) {
            if (isEmpty(row, col)) {
                cells.push_back({row, col});
            }
        }
    }
    return cells;
}

void Board::print(std::ostream& out) const {
    out << "\nScore: " << score_ << " | Max: " << highestTile() << "\n";
    out << "    1   2   3   4\n";
    out << "  +---+---+---+---+\n";
    for (int row = 0; row < Size; ++row) {
        out << row + 1 << " |";
        for (int col = 0; col < Size; ++col) {
            const int value = at(row, col);
            if (value == 0) {
                out << "   |";
            } else {
                out << std::setw(3) << value << "|";
            }
        }
        out << "\n  +---+---+---+---+\n";
    }
}
