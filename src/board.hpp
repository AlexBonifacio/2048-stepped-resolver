#pragma once

#include "move.hpp"

#include <array>
#include <cstddef>
#include <iosfwd>
#include <random>
#include <vector>

struct Cell {
    int row = 0;
    int col = 0;
};

class Board {
public:
    static constexpr int Size = 4;
    static constexpr int CellCount = Size * Size;

    Board();

    int at(int row, int col) const;
    bool setCell(int row, int col, int value);
    void setScore(int score);
    bool isInside(int row, int col) const;
    bool isEmpty(int row, int col) const;

    bool move(Direction direction);
    bool canMove(Direction direction) const;
    bool hasAnyMove() const;
    bool isFull() const;

    bool spawn(Cell cell, int value);
    bool spawnRandom(std::mt19937& rng, int* spawnedValue = nullptr);
    void start(std::mt19937& rng);

    int score() const;
    int highestTile() const;
    int emptyCount() const;
    std::vector<Cell> emptyCells() const;

    void print(std::ostream& out) const;

private:
    std::array<int, CellCount> cells_{};
    int score_ = 0;

    int& ref(int row, int col);
    std::array<int, Size> readLine(Direction direction, int index) const;
    void writeLine(Direction direction, int index, const std::array<int, Size>& line);
    std::array<int, Size> mergedLine(const std::array<int, Size>& line, int& gainedScore) const;
};
