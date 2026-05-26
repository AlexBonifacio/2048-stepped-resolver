#pragma once

#include "board.hpp"
#include "solver.hpp"

#include <map>
#include <string>
#include <vector>

struct SpawnObservation {
    int value = 0;
    int moves = 0;
    int highest = 0;
};

class StatTracker {
public:
    void recordSpawn(int value);
    void recordSpawn(int value, int moves, int highest);
    void mergeFrom(const StatTracker& other);
    int totalSpawns() const;
    const std::map<int, int>& spawnCounts() const;
    const std::vector<SpawnObservation>& observations() const;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
    std::vector<SpawnProbability> spawnModel() const;
    std::vector<SpawnProbability> spawnModel(int moves, int highest) const;

private:
    std::map<int, int> spawnCounts_;
    std::vector<SpawnObservation> observations_;
};
