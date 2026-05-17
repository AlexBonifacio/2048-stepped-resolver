#pragma once

#include "board.hpp"

#include <map>
#include <string>

class StatTracker {
public:
    void recordSpawn(int value);
    bool save(const std::string& path) const;

private:
    std::map<int, int> spawnCounts_;
};
