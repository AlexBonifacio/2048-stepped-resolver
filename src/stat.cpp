#include "stat.hpp"

#include <fstream>

void StatTracker::recordSpawn(int value) {
    if (value > 0) {
        ++spawnCounts_[value];
    }
}

bool StatTracker::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) {
        return false;
    }

    out << "{\n  \"spawns\": {";
    bool first = true;
    for (const auto& [value, count] : spawnCounts_) {
        out << (first ? "\n" : ",\n");
        out << "    \"" << value << "\": " << count;
        first = false;
    }
    if (!first) {
        out << "\n  ";
    }
    out << "}\n}\n";
    return true;
}
