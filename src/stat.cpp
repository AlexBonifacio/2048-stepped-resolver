#include "stat.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

namespace {

std::optional<int> readIntField(const std::string& content, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t position = content.find(needle);
    if (position == std::string::npos) {
        return std::nullopt;
    }
    position = content.find(':', position + needle.size());
    if (position == std::string::npos) {
        return std::nullopt;
    }
    ++position;
    while (position < content.size() && std::isspace(static_cast<unsigned char>(content[position]))) {
        ++position;
    }
    std::size_t end = position;
    if (end < content.size() && content[end] == '-') {
        ++end;
    }
    while (end < content.size() && std::isdigit(static_cast<unsigned char>(content[end]))) {
        ++end;
    }
    if (end == position) {
        return std::nullopt;
    }
    return std::stoi(content.substr(position, end - position));
}

std::vector<SpawnProbability> normalizedModel(const std::map<int, double>& weights) {
    double total = 0.0;
    for (const auto& [value, weight] : weights) {
        total += weight;
    }

    std::vector<SpawnProbability> model;
    for (const auto& [value, weight] : weights) {
        if (weight > 0.0 && total > 0.0) {
            model.push_back({value, weight / total});
        }
    }
    return model;
}

}  // namespace

void StatTracker::recordSpawn(int value) {
    if (value > 0) {
        ++spawnCounts_[value];
    }
}

void StatTracker::recordSpawn(int value, int moves, int highest) {
    if (value > 0) {
        ++spawnCounts_[value];
        observations_.push_back({value, std::max(0, moves), std::max(0, highest)});
    }
}

void StatTracker::mergeFrom(const StatTracker& other) {
    for (const auto& [value, count] : other.spawnCounts_) {
        spawnCounts_[value] += count;
    }
    observations_.insert(observations_.end(), other.observations_.begin(), other.observations_.end());
}

int StatTracker::totalSpawns() const {
    int total = 0;
    for (const auto& [value, count] : spawnCounts_) {
        total += count;
    }
    return total;
}

const std::map<int, int>& StatTracker::spawnCounts() const {
    return spawnCounts_;
}

const std::vector<SpawnObservation>& StatTracker::observations() const {
    return observations_;
}

bool StatTracker::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string content = buffer.str();

    std::size_t position = 0;
    while ((position = content.find('"', position)) != std::string::npos) {
        const std::size_t endKey = content.find('"', position + 1);
        if (endKey == std::string::npos) {
            break;
        }

        const std::string key = content.substr(position + 1, endKey - position - 1);
        position = endKey + 1;
        if (key == "spawns") {
            continue;
        }

        std::size_t colon = content.find(':', position);
        if (colon == std::string::npos) {
            break;
        }
        ++colon;

        while (colon < content.size() && std::isspace(static_cast<unsigned char>(content[colon]))) {
            ++colon;
        }

        std::size_t endValue = colon;
        while (endValue < content.size() && std::isdigit(static_cast<unsigned char>(content[endValue]))) {
            ++endValue;
        }

        if (endValue > colon) {
            try {
                const int value = std::stoi(key);
                const int count = std::stoi(content.substr(colon, endValue - colon));
                if (value > 0 && count > 0) {
                    spawnCounts_[value] += count;
                }
            } catch (...) {
            }
        }

        position = endValue;
    }

    const std::size_t observationsStart = content.find("\"observations\"");
    const std::size_t arrayStart = observationsStart == std::string::npos ? std::string::npos : content.find('[', observationsStart);
    const std::size_t arrayEnd = arrayStart == std::string::npos ? std::string::npos : content.find(']', arrayStart);
    if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
        std::size_t position = arrayStart;
        while ((position = content.find('{', position)) != std::string::npos && position < arrayEnd) {
            const std::size_t objectEnd = content.find('}', position);
            if (objectEnd == std::string::npos || objectEnd > arrayEnd) {
                break;
            }

            const std::string object = content.substr(position, objectEnd - position + 1);
            const auto value = readIntField(object, "value");
            const auto moves = readIntField(object, "moves");
            const auto highest = readIntField(object, "highest");
            if (value && *value > 0) {
                observations_.push_back({*value, moves.value_or(0), highest.value_or(0)});
            }
            position = objectEnd + 1;
        }
    }

    return true;
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
    out << "},\n  \"observations\": [";
    bool firstObservation = true;
    for (const auto& observation : observations_) {
        out << (firstObservation ? "\n" : ",\n");
        out << "    {\"value\": " << observation.value
            << ", \"moves\": " << observation.moves
            << ", \"highest\": " << observation.highest << "}";
        firstObservation = false;
    }
    if (!firstObservation) {
        out << "\n  ";
    }
    out << "]\n}\n";
    return true;
}

std::vector<SpawnProbability> StatTracker::spawnModel() const {
    std::map<int, double> weights;

    // Prior equivalent to classic 2048-ish behavior, adapted to ranks: mostly 1, sometimes 2.
    weights[1] = 9.0;
    weights[2] = 1.0;

    for (const auto& [value, count] : spawnCounts_) {
        weights[value] += count;
    }

    return normalizedModel(weights);
}

std::vector<SpawnProbability> StatTracker::spawnModel(int moves, int highest) const {
    std::map<int, double> weights;

    weights[1] = 9.0;
    weights[2] = 1.0;

    for (const auto& [value, count] : spawnCounts_) {
        weights[value] += count * 0.15;
    }

    for (const auto& observation : observations_) {
        const int moveDistance = std::abs(observation.moves - moves);
        const int highestDistance = std::abs(observation.highest - highest);
        double weight = 1.0;
        weight /= 1.0 + static_cast<double>(moveDistance) / 25.0;
        weight /= 1.0 + static_cast<double>(highestDistance);
        weights[observation.value] += weight * 3.0;
    }

    return normalizedModel(weights);
}
