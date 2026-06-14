#include "board.hpp"
#include "move.hpp"
#include "solver.hpp"
#include "stat.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* StatsPath = "data/observed_spawns.json";
constexpr const char* SessionDir = "data/sessions";
constexpr const char* SimulationReportDir = "data/simulation_reports";

struct CliOptions {
    int depth = 6;
    int rollouts = 1000;
    int candidates = 4;
    int maxRolloutMoves = 160;
    int chanceCells = Board::CellCount;
    int targetRank = 13;
    std::optional<int> startScore;
    std::optional<int> startMoves;
    int simulations = 0;
    int simulationMaxMoves = 0;
    unsigned int simulationSeed = 0x2048;
    std::string simulationReportName;
    std::string simulationStatsSource = "session";
    std::optional<Direction> forcedFirstMove;
    std::optional<double> solvedCheckpointFraction;
    std::optional<int> solvedCheckpointMove;
    std::string solvedCheckpointSource = "human";
    std::string modelSessionName;
    std::string modelStatsSource = "session";
    std::string solver = "hybrid";
    std::string quality = "strong";
    std::string sessionName;
    bool startFromSession = false;
    bool suggestOnly = false;
    bool updateStats = false;
    bool explain = false;
};

struct GameState {
    Board board;
    int moves = 0;
    StatTracker sessionStats;
    std::string solvedJson = "[]";
    std::string outcomeJson = "{\"status\": \"in_progress\", \"target\": 13}";
};

struct UndoSnapshot {
    GameState game;
    StatTracker globalStats;
};

int readIntOption(int argc, char** argv, const std::string& name, int fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == name) {
            const int value = std::atoi(argv[i + 1]);
            if (value > 0) {
                return value;
            }
        }
    }
    return fallback;
}

std::string readStringOption(int argc, char** argv, const std::string& name, const std::string& fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == name) {
            return argv[i + 1];
        }
    }
    return fallback;
}

std::optional<int> readNonNegativeIntOption(int argc, char** argv, const std::string& name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == name) {
            const int value = std::atoi(argv[i + 1]);
            if (value >= 0) {
                return value;
            }
        }
    }
    return std::nullopt;
}

std::optional<double> readNonNegativeDoubleOption(int argc, char** argv, const std::string& name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == name) {
            try {
                const double value = std::stod(argv[i + 1]);
                if (value >= 0.0) {
                    return value;
                }
            } catch (...) {
            }
        }
    }
    return std::nullopt;
}

bool hasFlag(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == name) {
            return true;
        }
    }
    return false;
}

std::string lowerCopy(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::optional<Direction> parseDirectionNameOption(const std::string& value) {
    const std::string direction = lowerCopy(value);
    if (direction == "up" || direction == "haut" || direction == "z" || direction == "w") {
        return Direction::Up;
    }
    if (direction == "down" || direction == "bas" || direction == "s") {
        return Direction::Down;
    }
    if (direction == "left" || direction == "gauche" || direction == "q" || direction == "a") {
        return Direction::Left;
    }
    if (direction == "right" || direction == "droite" || direction == "d") {
        return Direction::Right;
    }
    return std::nullopt;
}

CliOptions readOptions(int argc, char** argv) {
    CliOptions options;
    options.quality = readStringOption(argc, argv, "--quality", options.quality);
    if (options.quality == "strong") {
        options.depth = 6;
        options.rollouts = 1000;
        options.candidates = 4;
        options.maxRolloutMoves = 160;
        options.chanceCells = Board::CellCount;
    } else if (options.quality == "godlike") {
        options.depth = 7;
        options.rollouts = 2500;
        options.candidates = 4;
        options.maxRolloutMoves = 220;
        options.chanceCells = Board::CellCount;
    } else if (options.quality == "balanced") {
        options.depth = 5;
        options.rollouts = 450;
        options.candidates = 3;
        options.maxRolloutMoves = 110;
        options.chanceCells = 12;
    } else if (options.quality == "fast") {
        options.depth = 3;
        options.rollouts = 80;
        options.candidates = 2;
        options.maxRolloutMoves = 50;
        options.chanceCells = 8;
    } else if (options.quality != "balanced") {
        std::cout << "Qualite inconnue, utilisation du mode balanced.\n";
        options.quality = "balanced";
    }
    options.depth = readIntOption(argc, argv, "--depth", options.depth);
    options.rollouts = readIntOption(argc, argv, "--rollouts", options.rollouts);
    options.candidates = readIntOption(argc, argv, "--candidates", options.candidates);
    options.maxRolloutMoves = readIntOption(argc, argv, "--max-rollout-moves", options.maxRolloutMoves);
    options.chanceCells = readIntOption(argc, argv, "--chance-cells", options.chanceCells);
    options.targetRank = readIntOption(argc, argv, "--target", options.targetRank);
    options.startScore = readNonNegativeIntOption(argc, argv, "--start-score");
    options.startMoves = readNonNegativeIntOption(argc, argv, "--start-moves");
    options.simulations = readIntOption(argc, argv, "--simulate", options.simulations);
    options.simulationMaxMoves = readNonNegativeIntOption(argc, argv, "--sim-max-moves").value_or(options.simulationMaxMoves);
    options.simulationSeed = static_cast<unsigned int>(readIntOption(argc, argv, "--seed", static_cast<int>(options.simulationSeed)));
    options.simulationReportName = readStringOption(argc, argv, "--sim-report", options.simulationReportName);
    options.simulationStatsSource = readStringOption(argc, argv, "--sim-stats", options.simulationStatsSource);
    const std::string forcedFirstMove = readStringOption(argc, argv, "--force-first-move", "");
    if (!forcedFirstMove.empty()) {
        options.forcedFirstMove = parseDirectionNameOption(forcedFirstMove);
        if (!options.forcedFirstMove) {
            std::cout << "Coup force inconnu, ignore.\n";
        }
    }
    options.solvedCheckpointFraction = readNonNegativeDoubleOption(argc, argv, "--start-from-solved");
    options.solvedCheckpointMove = readNonNegativeIntOption(argc, argv, "--checkpoint-move");
    options.solvedCheckpointSource = readStringOption(argc, argv, "--checkpoint-source", options.solvedCheckpointSource);
    options.modelSessionName = readStringOption(argc, argv, "--model-session", options.modelSessionName);
    options.modelStatsSource = readStringOption(argc, argv, "--model-stats", options.modelStatsSource);
    options.solver = readStringOption(argc, argv, "--solver", options.solver);
    options.sessionName = readStringOption(argc, argv, "--session", options.sessionName);
    options.startFromSession = hasFlag(argc, argv, "--start-from-session");
    options.suggestOnly = hasFlag(argc, argv, "--suggest-only");
    options.explain = hasFlag(argc, argv, "--explain");
    const std::string statsMode = readStringOption(argc, argv, "--stats", "read-only");
    if (options.solver != "hybrid" && options.solver != "expectimax" && options.solver != "optimistic" && options.solver != "human" && options.solver != "target") {
        std::cout << "Solveur inconnu, utilisation du mode hybrid.\n";
        options.solver = "hybrid";
    }
    if (options.simulationStatsSource != "global" &&
        options.simulationStatsSource != "session" &&
        options.simulationStatsSource != "merged") {
        std::cout << "Source de stats inconnue, utilisation de session.\n";
        options.simulationStatsSource = "session";
    }
    if (options.solvedCheckpointSource != "human" &&
        options.solvedCheckpointSource != "ai" &&
        options.solvedCheckpointSource != "any") {
        std::cout << "Source de checkpoint inconnue, utilisation de human.\n";
        options.solvedCheckpointSource = "human";
    }
    if (options.modelStatsSource != "global" &&
        options.modelStatsSource != "session" &&
        options.modelStatsSource != "merged") {
        std::cout << "Source de modele inconnue, utilisation de session.\n";
        options.modelStatsSource = "session";
    }
    if (statsMode == "read-write") {
        options.updateStats = true;
    } else if (statsMode != "read-only") {
        std::cout << "Mode stats inconnu, utilisation de read-only.\n";
    }
    return options;
}

std::string sanitizedSessionName(std::string name) {
    for (char& ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') {
            ch = '_';
        }
    }
    return name.empty() ? "default" : name;
}

std::string sessionPath(const std::string& name) {
    return std::string(SessionDir) + "/" + sanitizedSessionName(name) + ".json";
}

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::optional<int> readJsonInt(const std::string& content, const std::string& key) {
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

std::optional<std::size_t> findMatchingBracket(const std::string& content, std::size_t start) {
    if (start >= content.size() || content[start] != '[') {
        return std::nullopt;
    }

    int depth = 0;
    for (std::size_t position = start; position < content.size(); ++position) {
        if (content[position] == '[') {
            ++depth;
        } else if (content[position] == ']') {
            --depth;
            if (depth == 0) {
                return position;
            }
        }
    }
    return std::nullopt;
}

std::vector<int> parseInts(const std::string& content) {
    std::vector<int> values;
    std::size_t position = 0;
    while (position < content.size()) {
        while (position < content.size() &&
               !std::isdigit(static_cast<unsigned char>(content[position])) &&
               content[position] != '-') {
            ++position;
        }
        if (position >= content.size()) {
            break;
        }

        std::size_t end = position;
        if (content[end] == '-') {
            ++end;
        }
        while (end < content.size() && std::isdigit(static_cast<unsigned char>(content[end]))) {
            ++end;
        }
        try {
            values.push_back(std::stoi(content.substr(position, end - position)));
        } catch (...) {
        }
        position = end;
    }
    return values;
}

std::optional<Cell> parseCell(const std::string& input) {
    std::string compact;
    for (char ch : input) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            compact.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }

    if (compact.size() == 2) {
        int row = 0;
        int col = 0;
        for (char ch : compact) {
            if (ch >= 'a' && ch < 'a' + Board::Size) {
                col = ch - 'a' + 1;
            } else if (ch >= '1' && ch < '1' + Board::Size) {
                row = ch - '0';
            }
        }
        if (row > 0 && col > 0) {
            return Cell{row - 1, col - 1};
        }
    }

    int row = 0;
    int col = 0;
    if (std::sscanf(input.c_str(), "%d %d", &row, &col) == 2) {
        return Cell{row - 1, col - 1};
    }
    return std::nullopt;
}

int readValue(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    if (line.empty()) {
        return 1;
    }
    try {
        const int value = std::stoi(line);
        return value > 0 ? value : 1;
    } catch (...) {
        return 1;
    }
}

void printHelp() {
    std::cout << "Commandes: z/w=haut, s=bas, q/a=gauche, d=droite, u=undo, u 3=undo 3 pas, c=contexte, e=edit, p=publier session, h=aide, r=nouveau plateau, x=quitter\n";
    std::cout << "Coordonnees: colonnes a-d, lignes 1-4. a1 et 1a designent la meme case. Ancien format 1 1 accepte.\n";
    std::cout << "Sessions: --session nom sauvegarde plateau, score, coups et apparitions dans data/sessions/nom.json.\n";
    std::cout << "Options: --quality fast|balanced|strong|godlike --solver hybrid|expectimax|optimistic|human|target --target 13 --explain --suggest-only --model-session NOM --model-stats global|session|merged --simulate N --sim-max-moves N --sim-stats global|session|merged --force-first-move haut|bas|gauche|droite --sim-report NOM --seed N --depth 4 --rollouts 200 --candidates 2 --chance-cells 12 --max-rollout-moves 80 --start-score N --start-moves N --start-from-session --start-from-solved 0.5 --checkpoint-move N --checkpoint-source human|ai|any --stats read-only|read-write.\n";
}

bool saveStatsIfNeeded(const StatTracker& stats, const CliOptions& options, bool hasSession) {
    if (options.updateStats && !hasSession) {
        return stats.save(StatsPath);
    }
    return true;
}

void rememberUndo(std::vector<UndoSnapshot>& history, const GameState& game, const StatTracker& globalStats) {
    history.push_back({game, globalStats});
}

int parseUndoCount(const std::string& command) {
    std::stringstream input(command.substr(1));
    int count = 1;
    input >> count;
    return std::max(1, count);
}

bool restoreUndo(std::vector<UndoSnapshot>& history, GameState& game, StatTracker& globalStats, int count) {
    if (history.empty()) {
        return false;
    }

    count = std::min(count, static_cast<int>(history.size()));
    UndoSnapshot snapshot;
    for (int i = 0; i < count; ++i) {
        snapshot = history.back();
        history.pop_back();
    }
    game = snapshot.game;
    globalStats = snapshot.globalStats;
    return true;
}

void applyStartContext(GameState& game, const CliOptions& options) {
    if (options.startScore) {
        game.board.setScore(*options.startScore);
    }
    if (options.startMoves) {
        game.moves = *options.startMoves;
    }
}

std::optional<int> readOptionalNonNegative(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    std::getline(std::cin, line);
    if (line.empty()) {
        return std::nullopt;
    }
    try {
        const int value = std::stoi(line);
        if (value >= 0) {
            return value;
        }
    } catch (...) {
    }
    std::cout << "Valeur invalide, ignoree.\n";
    return std::nullopt;
}

std::optional<std::string> readJsonArray(const std::string& content, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyStart = content.find(needle);
    if (keyStart == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t bracketStart = content.find('[', keyStart + needle.size());
    const auto bracketEnd = bracketStart == std::string::npos ? std::nullopt : findMatchingBracket(content, bracketStart);
    if (bracketStart == std::string::npos || !bracketEnd) {
        return std::nullopt;
    }
    return content.substr(bracketStart, *bracketEnd - bracketStart + 1);
}

std::optional<std::string> readJsonObject(const std::string& content, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyStart = content.find(needle);
    if (keyStart == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t objectStart = content.find('{', keyStart + needle.size());
    if (objectStart == std::string::npos) {
        return std::nullopt;
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t position = objectStart; position < content.size(); ++position) {
        const char ch = content[position];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inString = !inString;
            continue;
        }
        if (inString) {
            continue;
        }
        if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return content.substr(objectStart, position - objectStart + 1);
            }
        }
    }
    return std::nullopt;
}

std::string ltrimCopy(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    return value.substr(start);
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::string compactSolvedArrays(const std::string& json) {
    std::istringstream input(json);
    std::ostringstream output;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string stripped = ltrimCopy(lines[index]);
        if (startsWith(stripped, "\"before\": [") || startsWith(stripped, "\"after\": [")) {
            const std::string indent = lines[index].substr(0, lines[index].size() - stripped.size());
            const std::string key = stripped.substr(0, stripped.find(':'));
            std::vector<std::string> values;
            ++index;
            for (; index < lines.size(); ++index) {
                std::string valueLine = ltrimCopy(lines[index]);
                if (startsWith(valueLine, "]")) {
                    const bool hasComma = !valueLine.empty() && valueLine.back() == ',';
                    output << indent << key << ": [";
                    for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
                        if (valueIndex != 0) {
                            output << ", ";
                        }
                        output << values[valueIndex];
                    }
                    output << "]" << (hasComma ? "," : "") << "\n";
                    break;
                }
                if (!valueLine.empty() && valueLine.back() == ',') {
                    valueLine.pop_back();
                }
                values.push_back(valueLine);
            }
        } else {
            output << lines[index] << "\n";
        }
    }
    return output.str();
}

std::string directionJsonName(Direction direction) {
    switch (direction) {
        case Direction::Up:
            return "up";
        case Direction::Down:
            return "down";
        case Direction::Left:
            return "left";
        case Direction::Right:
            return "right";
    }
    return "unknown";
}

std::string boardCellsJson(const Board& board) {
    std::ostringstream out;
    out << "[";
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            if (row != 0 || col != 0) {
                out << ", ";
            }
            out << board.at(row, col);
        }
    }
    out << "]";
    return out.str();
}

std::string solvedMoveJson(Direction direction, const Board& before, const Board& after, int beforeMoves, int beforeScore, int beforeHighest) {
    std::ostringstream out;
    out << "{\"direction\": \"" << directionJsonName(direction)
        << "\", \"source\": \"human\""
        << ", \"moves\": " << beforeMoves
        << ", \"after_moves\": " << beforeMoves + 1
        << ", \"score\": " << beforeScore
        << ", \"highest\": " << beforeHighest
        << ", \"gained\": " << std::max(0, after.score() - beforeScore)
        << ", \"before\": " << boardCellsJson(before)
        << ", \"after\": " << boardCellsJson(after)
        << "}";
    return out.str();
}

void appendSolvedMove(GameState& game, Direction direction, const Board& before, int beforeMoves, int beforeScore, int beforeHighest) {
    std::string solved = game.solvedJson.empty() ? "[]" : game.solvedJson;
    const std::size_t closing = solved.find_last_of(']');
    if (closing == std::string::npos) {
        solved = "[]";
    }

    const std::string entry = solvedMoveJson(direction, before, game.board, beforeMoves, beforeScore, beforeHighest);
    if (solved == "[]") {
        game.solvedJson = "[\n    " + entry + "\n  ]";
        return;
    }

    solved.erase(solved.find_last_of(']'));
    while (!solved.empty() && std::isspace(static_cast<unsigned char>(solved.back()))) {
        solved.pop_back();
    }
    game.solvedJson = solved + ",\n    " + entry + "\n  ]";
}

bool outcomeEnded(const GameState& game) {
    return game.outcomeJson.find("\"status\": \"ended\"") != std::string::npos ||
           game.outcomeJson.find("\"status\":\"ended\"") != std::string::npos;
}

std::string endedOutcomeJson(const GameState& game, const std::string& reason = "no_moves", int target = 13) {
    const int highest = game.board.highestTile();
    std::ostringstream out;
    out << "{"
        << "\"status\": \"ended\""
        << ", \"target\": " << target
        << ", \"final_score\": " << game.board.score()
        << ", \"final_moves\": " << game.moves
        << ", \"final_highest\": " << highest
        << ", \"success\": " << (highest >= target ? "true" : "false")
        << ", \"reason\": \"" << reason << "\""
        << ", \"ended_at\": \"\""
        << "}";
    return out.str();
}

void editContext(GameState& game, StatTracker& globalStats, std::vector<UndoSnapshot>& history) {
    rememberUndo(history, game, globalStats);
    const auto score = readOptionalNonNegative("Score actuel (entree pour garder): ");
    const auto moves = readOptionalNonNegative("Nombre de coups actuel (entree pour garder): ");
    if (score) {
        game.board.setScore(*score);
    }
    if (moves) {
        game.moves = *moves;
    }
    if (!score && !moves) {
        history.pop_back();
    }
}

bool saveSession(const GameState& game, const std::string& name) {
    std::filesystem::create_directories(SessionDir);
    std::ofstream out(sessionPath(name));
    if (!out) {
        return false;
    }

    out << "{\n";
    out << "  \"moves\": " << game.moves << ",\n";
    out << "  \"score\": " << game.board.score() << ",\n";
    out << "  \"cells\": [";
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            if (row != 0 || col != 0) {
                out << ", ";
            }
            out << game.board.at(row, col);
        }
    }
    out << "],\n";
    out << "  \"map\": [\n";
    for (int row = 0; row < Board::Size; ++row) {
        out << "    [";
        for (int col = 0; col < Board::Size; ++col) {
            if (col != 0) {
                out << ", ";
            }
            out << game.board.at(row, col);
        }
        out << "]";
        if (row + 1 < Board::Size) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"observations\": [";
    bool firstObservation = true;
    for (const auto& observation : game.sessionStats.observations()) {
        out << (firstObservation ? "\n" : ",\n");
        out << "    {\"value\": " << observation.value
            << ", \"moves\": " << observation.moves
            << ", \"highest\": " << observation.highest << "}";
        firstObservation = false;
    }
    if (!firstObservation) {
        out << "\n  ";
    }
    out << "],\n";
    out << "  \"spawns\": {";
    bool first = true;
    for (const auto& [value, count] : game.sessionStats.spawnCounts()) {
        out << (first ? "\n" : ",\n");
        out << "    \"" << value << "\": " << count;
        first = false;
    }
    if (!first) {
        out << "\n  ";
    }
    out << "},\n";
    out << "  \"solved\": " << (game.solvedJson.empty() ? "[]" : game.solvedJson) << ",\n";
    out << "  \"outcome\": " << (game.outcomeJson.empty() ? "{\"status\": \"in_progress\", \"target\": 13}" : game.outcomeJson) << "\n";
    out << "}\n";
    return true;
}

bool loadSession(GameState& game, const std::string& name) {
    const std::string content = readFile(sessionPath(name));
    if (content.empty()) {
        return false;
    }

    const auto moves = readJsonInt(content, "moves");
    const auto score = readJsonInt(content, "score");
    if (moves) {
        game.moves = std::max(0, *moves);
    }
    if (score) {
        game.board.setScore(*score);
    }

    std::size_t boardStart = content.find("\"cells\"");
    boardStart = boardStart == std::string::npos ? content.find("\"map\"") : boardStart;
    const std::size_t bracketStart = boardStart == std::string::npos ? std::string::npos : content.find('[', boardStart);
    const auto bracketEnd = bracketStart == std::string::npos ? std::nullopt : findMatchingBracket(content, bracketStart);
    if (bracketStart == std::string::npos || !bracketEnd) {
        return false;
    }
    const auto values = parseInts(content.substr(bracketStart + 1, *bracketEnd - bracketStart - 1));
    if (static_cast<int>(values.size()) < Board::CellCount) {
        return false;
    }
    for (int index = 0; index < Board::CellCount; ++index) {
        game.board.setCell(index / Board::Size, index % Board::Size, values[static_cast<std::size_t>(index)]);
    }

    game.sessionStats.load(sessionPath(name));
    game.solvedJson = compactSolvedArrays(readJsonArray(content, "solved").value_or("[]"));
    game.outcomeJson = readJsonObject(content, "outcome").value_or("{\"status\": \"in_progress\", \"target\": 13}");

    return true;
}

struct SolvedCheckpoint {
    GameState game;
    int solvedIndex = 0;
    std::string source = "human";
    std::string direction;
};

struct HumanMoveExample {
    std::array<int, Board::CellCount> before{};
    Direction direction = Direction::Up;
};

std::vector<std::string> readTopLevelJsonObjects(const std::string& arrayJson) {
    std::vector<std::string> objects;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    std::size_t objectStart = std::string::npos;

    for (std::size_t position = 0; position < arrayJson.size(); ++position) {
        const char ch = arrayJson[position];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            inString = !inString;
            continue;
        }
        if (inString) {
            continue;
        }

        if (ch == '{') {
            if (depth == 0) {
                objectStart = position;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && objectStart != std::string::npos) {
                objects.push_back(arrayJson.substr(objectStart, position - objectStart + 1));
                objectStart = std::string::npos;
            }
        }
    }

    return objects;
}

std::optional<std::string> readJsonStringField(const std::string& objectJson, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t position = objectJson.find(needle);
    if (position == std::string::npos) {
        return std::nullopt;
    }
    position = objectJson.find(':', position + needle.size());
    if (position == std::string::npos) {
        return std::nullopt;
    }
    position = objectJson.find('"', position + 1);
    if (position == std::string::npos) {
        return std::nullopt;
    }
    ++position;

    std::string value;
    bool escaped = false;
    for (; position < objectJson.size(); ++position) {
        const char ch = objectJson[position];
        if (escaped) {
            value.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }
    return std::nullopt;
}

std::optional<std::vector<int>> readJsonIntArrayField(const std::string& objectJson, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyStart = objectJson.find(needle);
    if (keyStart == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t bracketStart = objectJson.find('[', keyStart + needle.size());
    const auto bracketEnd = bracketStart == std::string::npos ? std::nullopt : findMatchingBracket(objectJson, bracketStart);
    if (bracketStart == std::string::npos || !bracketEnd) {
        return std::nullopt;
    }
    return parseInts(objectJson.substr(bracketStart + 1, *bracketEnd - bracketStart - 1));
}

std::vector<SolvedCheckpoint> solvedCheckpoints(const GameState& game, const std::string& sourceFilter) {
    std::vector<SolvedCheckpoint> checkpoints;
    const auto objects = readTopLevelJsonObjects(game.solvedJson);

    for (std::size_t index = 0; index < objects.size(); ++index) {
        const std::string source = readJsonStringField(objects[index], "source").value_or("human");
        if (sourceFilter != "any" && source != sourceFilter) {
            continue;
        }

        const auto cells = readJsonIntArrayField(objects[index], "before");
        if (!cells || static_cast<int>(cells->size()) < Board::CellCount) {
            continue;
        }

        SolvedCheckpoint checkpoint;
        checkpoint.solvedIndex = static_cast<int>(index);
        checkpoint.source = source;
        checkpoint.direction = readJsonStringField(objects[index], "direction").value_or("");
        checkpoint.game.moves = readJsonInt(objects[index], "moves").value_or(0);
        checkpoint.game.board.setScore(readJsonInt(objects[index], "score").value_or(0));
        for (int cell = 0; cell < Board::CellCount; ++cell) {
            checkpoint.game.board.setCell(cell / Board::Size, cell % Board::Size, (*cells)[static_cast<std::size_t>(cell)]);
        }
        checkpoints.push_back(checkpoint);
    }

    return checkpoints;
}

std::vector<HumanMoveExample> humanMoveExamples(const GameState& game) {
    std::vector<HumanMoveExample> examples;
    const auto objects = readTopLevelJsonObjects(game.solvedJson);

    for (const std::string& object : objects) {
        const std::string source = readJsonStringField(object, "source").value_or("human");
        if (source != "human") {
            continue;
        }
        const auto direction = parseDirectionNameOption(readJsonStringField(object, "direction").value_or(""));
        const auto cells = readJsonIntArrayField(object, "before");
        if (!direction || !cells || static_cast<int>(cells->size()) < Board::CellCount) {
            continue;
        }

        HumanMoveExample example;
        example.direction = *direction;
        for (int index = 0; index < Board::CellCount; ++index) {
            example.before[static_cast<std::size_t>(index)] = (*cells)[static_cast<std::size_t>(index)];
        }
        examples.push_back(example);
    }

    return examples;
}

int cornerIndexForHighest(const Board& board) {
    const int highest = board.highestTile();
    if (board.at(0, 0) == highest) {
        return 0;
    }
    if (board.at(0, Board::Size - 1) == highest) {
        return 1;
    }
    if (board.at(Board::Size - 1, 0) == highest) {
        return 2;
    }
    if (board.at(Board::Size - 1, Board::Size - 1) == highest) {
        return 3;
    }
    return -1;
}

int cornerIndexForHighest(const HumanMoveExample& example) {
    int highest = 0;
    for (const int rank : example.before) {
        highest = std::max(highest, rank);
    }
    if (example.before[0] == highest) {
        return 0;
    }
    if (example.before[Board::Size - 1] == highest) {
        return 1;
    }
    if (example.before[(Board::Size - 1) * Board::Size] == highest) {
        return 2;
    }
    if (example.before[Board::CellCount - 1] == highest) {
        return 3;
    }
    return -1;
}

double humanBoardDistance(const Board& board, const HumanMoveExample& example) {
    double distance = 0.0;
    for (int index = 0; index < Board::CellCount; ++index) {
        const int row = index / Board::Size;
        const int col = index % Board::Size;
        const int current = board.at(row, col);
        const int learned = example.before[static_cast<std::size_t>(index)];
        const int diff = std::abs(current - learned);
        const int importance = std::max(current, learned);
        distance += static_cast<double>(diff) * (1.0 + static_cast<double>(importance) * 0.45);
        if ((current == 0) != (learned == 0)) {
            distance += 1.5;
        }
    }

    const int boardCorner = cornerIndexForHighest(board);
    const int exampleCorner = cornerIndexForHighest(example);
    if (boardCorner != -1 && exampleCorner != -1 && boardCorner != exampleCorner) {
        distance += 18.0;
    }
    return distance;
}

bool wantsSolvedCheckpoint(const CliOptions& options) {
    return options.solvedCheckpointFraction.has_value() || options.solvedCheckpointMove.has_value();
}

std::optional<SolvedCheckpoint> selectSolvedCheckpoint(const GameState& game, const CliOptions& options) {
    const auto checkpoints = solvedCheckpoints(game, options.solvedCheckpointSource);
    if (checkpoints.empty()) {
        return std::nullopt;
    }

    if (options.solvedCheckpointMove) {
        const int targetMove = *options.solvedCheckpointMove;
        return *std::min_element(checkpoints.begin(), checkpoints.end(), [targetMove](const SolvedCheckpoint& lhs, const SolvedCheckpoint& rhs) {
            return std::abs(lhs.game.moves - targetMove) < std::abs(rhs.game.moves - targetMove);
        });
    }

    double fraction = options.solvedCheckpointFraction.value_or(0.5);
    if (fraction > 1.0) {
        fraction /= 100.0;
    }
    fraction = std::clamp(fraction, 0.0, 1.0);
    const std::size_t index = static_cast<std::size_t>(fraction * static_cast<double>(checkpoints.size() - 1) + 0.5);
    return checkpoints[index];
}

bool hasAnyTile(const Board& board) {
    for (int row = 0; row < Board::Size; ++row) {
        for (int col = 0; col < Board::Size; ++col) {
            if (board.at(row, col) != 0) {
                return true;
            }
        }
    }
    return false;
}

bool spawnRandomFromModel(Board& board, const std::vector<SpawnProbability>& model, std::mt19937& rng) {
    const auto empties = board.emptyCells();
    if (empties.empty()) {
        return false;
    }

    std::uniform_int_distribution<std::size_t> cellDist(0, empties.size() - 1);
    std::uniform_real_distribution<double> valueDist(0.0, 1.0);

    double accumulated = 0.0;
    int value = model.empty() ? 1 : model.back().value;
    const double sample = valueDist(rng);
    for (const auto& spawn : model) {
        accumulated += spawn.probability;
        if (sample <= accumulated) {
            value = spawn.value;
            break;
        }
    }

    return board.spawn(empties[cellDist(rng)], value);
}

bool spawnRandomValue(Board& board, int value, std::mt19937& rng) {
    const auto empties = board.emptyCells();
    if (empties.empty() || value <= 0) {
        return false;
    }

    std::uniform_int_distribution<std::size_t> cellDist(0, empties.size() - 1);
    return board.spawn(empties[cellDist(rng)], value);
}

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

struct SimulationStartInfo {
    std::string kind = "zero";
    int checkpointIndex = -1;
    std::string checkpointSource;
    std::string checkpointDirection;
};

struct SimulationResult {
    int score = 0;
    int moves = 0;
    int highest = 0;
    bool capped = false;
};

std::vector<MoveAnalysis> analyzeHumanPriorMoves(
    const Board& board,
    const std::vector<SpawnProbability>& spawnModel,
    const CliOptions& options,
    const std::vector<HumanMoveExample>& examples);

SimulationResult runSimulation(
    const CliOptions& options,
    const StatTracker& stats,
    std::mt19937& rng,
    const std::optional<GameState>& startGame,
    const std::vector<HumanMoveExample>& humanExamples) {
    Board board = startGame ? startGame->board : Board();
    int moves = startGame ? startGame->moves : 0;

    if (!startGame) {
        spawnRandomValue(board, 1, rng);
        spawnRandomValue(board, 1, rng);
    }

    if (options.forcedFirstMove && board.hasAnyMove() && (options.simulationMaxMoves <= 0 || moves < options.simulationMaxMoves)) {
        if (board.move(*options.forcedFirstMove)) {
            ++moves;
            spawnRandomFromModel(board, stats.spawnModel(moves, board.highestTile()), rng);
        } else {
            return {board.score(), moves, board.highestTile(), false};
        }
    }

    while (board.hasAnyMove() && (options.simulationMaxMoves <= 0 || moves < options.simulationMaxMoves)) {
        const auto spawnModel = stats.spawnModel(moves, board.highestTile());
        std::optional<MoveSuggestion> suggestion;
        if (options.solver == "expectimax") {
            suggestion = suggestMove(board, spawnModel, options.depth, options.chanceCells);
        } else if (options.solver == "optimistic") {
            suggestion = suggestOptimisticMove(board, spawnModel, options.depth, options.chanceCells);
        } else if (options.solver == "human") {
            const auto analyses = analyzeHumanPriorMoves(board, spawnModel, options, humanExamples);
            if (!analyses.empty()) {
                const auto& best = analyses.front();
                suggestion = MoveSuggestion{best.direction, best.score, best.depth, best.rollouts, best.solver};
            }
        } else if (options.solver == "target") {
            suggestion = suggestTargetMove(
                board,
                spawnModel,
                {options.rollouts, options.maxRolloutMoves, options.targetRank, options.simulationSeed + static_cast<unsigned int>(moves * 2654435761U)});
        } else {
            suggestion = suggestHybridMove(
                board,
                spawnModel,
                {options.depth, options.rollouts, options.candidates, options.maxRolloutMoves, options.chanceCells, options.simulationSeed + static_cast<unsigned int>(moves * 2654435761U)});
        }
        if (!suggestion || !board.move(suggestion->direction)) {
            break;
        }
        ++moves;
        spawnRandomFromModel(board, stats.spawnModel(moves, board.highestTile()), rng);
    }

    return {board.score(), moves, board.highestTile(), options.simulationMaxMoves > 0 && moves >= options.simulationMaxMoves && board.hasAnyMove()};
}

void runSimulations(
    const CliOptions& options,
    const StatTracker& stats,
    const std::optional<GameState>& startGame,
    const SimulationStartInfo& startInfo,
    const std::vector<HumanMoveExample>& humanExamples) {
    std::mt19937 rng(options.simulationSeed);
    long long totalScore = 0;
    long long totalMoves = 0;
    int bestScore = 0;
    int bestMoves = 0;
    int bestHighest = 0;
    std::map<int, int> highestCounts;
    std::vector<SimulationResult> results;

    for (int index = 1; index <= options.simulations; ++index) {
        const auto result = runSimulation(options, stats, rng, startGame, humanExamples);
        results.push_back(result);
        totalScore += result.score;
        totalMoves += result.moves;
        bestScore = std::max(bestScore, result.score);
        bestMoves = std::max(bestMoves, result.moves);
        bestHighest = std::max(bestHighest, result.highest);
        ++highestCounts[result.highest];

        std::cout << "Simulation " << index << "/" << options.simulations
                  << ": score " << result.score
                  << ", coups " << result.moves
                  << ", max " << result.highest
                  << (result.capped ? " (limite)" : "") << "\n"
                  << std::flush;
    }

    std::cout << "\nResume simulations\n";
    std::cout << "Moyenne score: " << (options.simulations > 0 ? totalScore / options.simulations : 0) << "\n";
    std::cout << "Moyenne coups: " << (options.simulations > 0 ? totalMoves / options.simulations : 0) << "\n";
    std::cout << "Meilleur score: " << bestScore << "\n";
    std::cout << "Meilleur nombre de coups: " << bestMoves << "\n";
    std::cout << "Meilleur max: " << bestHighest << "\n";
    std::cout << "Distribution max:";
    for (const auto& [highest, count] : highestCounts) {
        std::cout << " " << highest << "=" << count;
    }
    std::cout << "\n";

    if (!options.simulationReportName.empty()) {
        std::filesystem::create_directories(SimulationReportDir);
        const std::string path = std::string(SimulationReportDir) + "/" + sanitizedSessionName(options.simulationReportName) + ".jsonl";
        std::ofstream out(path, std::ios::app);
        if (!out) {
            std::cout << "Impossible d'ecrire le rapport de simulation: " << path << "\n";
            return;
        }

        out << "{\"simulations\": " << options.simulations
            << ", \"solver\": \"" << options.solver << "\""
            << ", \"quality\": \"" << options.quality << "\""
            << ", \"played_by\": \"ai\""
            << ", \"sim_stats\": \"" << options.simulationStatsSource << "\""
            << ", \"forced_first_move\": \"" << (options.forcedFirstMove ? directionName(*options.forcedFirstMove) : "") << "\""
            << ", \"session\": \"" << sanitizedSessionName(options.sessionName) << "\""
            << ", \"start_from_session\": " << (startGame ? "true" : "false")
            << ", \"start_kind\": \"" << jsonEscape(startInfo.kind) << "\""
            << ", \"start_moves\": " << (startGame ? startGame->moves : 0)
            << ", \"start_highest\": " << (startGame ? startGame->board.highestTile() : 0)
            << ", \"checkpoint_index\": " << startInfo.checkpointIndex
            << ", \"checkpoint_source\": \"" << jsonEscape(startInfo.checkpointSource) << "\""
            << ", \"checkpoint_direction\": \"" << jsonEscape(startInfo.checkpointDirection) << "\""
            << ", \"seed\": " << options.simulationSeed
            << ", \"depth\": " << options.depth
            << ", \"rollouts\": " << options.rollouts
            << ", \"candidates\": " << options.candidates
            << ", \"chance_cells\": " << options.chanceCells
            << ", \"max_rollout_moves\": " << options.maxRolloutMoves
            << ", \"target\": " << options.targetRank
            << ", \"sim_max_moves\": " << options.simulationMaxMoves
            << ", \"avg_score\": " << (options.simulations > 0 ? totalScore / options.simulations : 0)
            << ", \"avg_moves\": " << (options.simulations > 0 ? totalMoves / options.simulations : 0)
            << ", \"best_score\": " << bestScore
            << ", \"best_moves\": " << bestMoves
            << ", \"best_highest\": " << bestHighest
            << ", \"results\": [";
        for (std::size_t index = 0; index < results.size(); ++index) {
            if (index != 0) {
                out << ", ";
            }
            out << "{\"score\": " << results[index].score
                << ", \"moves\": " << results[index].moves
                << ", \"highest\": " << results[index].highest
                << ", \"capped\": " << (results[index].capped ? "true" : "false") << "}";
        }
        out << "]}\n";
        std::cout << "Rapport de simulation ajoute: " << path << "\n";
    }
}

bool publishSessionStats(StatTracker& globalStats, GameState& game) {
    globalStats.mergeFrom(game.sessionStats);
    if (!globalStats.save(StatsPath)) {
        return false;
    }
    game.sessionStats = StatTracker();
    return true;
}

void writeJsonError(const std::string& message) {
    std::cout << "{\"ok\": false, \"error\": \"" << jsonEscape(message) << "\"}\n";
}

bool mergeSessionStats(StatTracker& target, const std::string& name) {
    GameState loaded;
    if (!loadSession(loaded, name)) {
        return false;
    }
    target.mergeFrom(loaded.sessionStats);
    return true;
}

std::vector<MoveAnalysis> analyzeHumanPriorMoves(
    const Board& board,
    const std::vector<SpawnProbability>& spawnModel,
    const CliOptions& options,
    const std::vector<HumanMoveExample>& examples) {
    auto analyses = analyzeExpectimaxMoves(board, spawnModel, options.depth, options.chanceCells);
    if (analyses.empty() || examples.empty()) {
        for (auto& analysis : analyses) {
            analysis.solver = "human";
        }
        return analyses;
    }

    struct Neighbor {
        Direction direction = Direction::Up;
        double distance = 0.0;
    };

    std::vector<Neighbor> neighbors;
    neighbors.reserve(examples.size());
    for (const auto& example : examples) {
        Board candidate = board;
        if (!candidate.move(example.direction)) {
            continue;
        }
        neighbors.push_back({example.direction, humanBoardDistance(board, example)});
    }
    if (neighbors.empty()) {
        for (auto& analysis : analyses) {
            analysis.solver = "human";
        }
        return analyses;
    }

    std::sort(neighbors.begin(), neighbors.end(), [](const Neighbor& lhs, const Neighbor& rhs) {
        return lhs.distance < rhs.distance;
    });

    std::map<Direction, double> prior;
    const std::size_t neighborLimit = std::min<std::size_t>(32, neighbors.size());
    for (std::size_t index = 0; index < neighborLimit; ++index) {
        const double weight = 1.0 / (1.0 + neighbors[index].distance);
        prior[neighbors[index].direction] += weight;
    }

    const auto scoreBounds = std::minmax_element(analyses.begin(), analyses.end(), [](const MoveAnalysis& lhs, const MoveAnalysis& rhs) {
        return lhs.score < rhs.score;
    });
    const double scoreRange = std::max(1.0, scoreBounds.second->score - scoreBounds.first->score);
    double maxPrior = 0.0;
    for (const auto& [_, weight] : prior) {
        maxPrior = std::max(maxPrior, weight);
    }
    maxPrior = std::max(0.000001, maxPrior);

    const double nearestDistance = neighbors.front().distance;
    const double priorWeight = nearestDistance <= 2.0 ? 0.82 : nearestDistance <= 12.0 ? 0.42 : 0.20;
    const double searchWeight = 1.0 - priorWeight;

    for (auto& analysis : analyses) {
        const double searchScore = (analysis.score - scoreBounds.first->score) / scoreRange;
        const double priorScore = prior[analysis.direction] / maxPrior;
        analysis.score = (searchScore * searchWeight + priorScore * priorWeight) * 1000000.0 + analysis.score * 0.000001;
        analysis.solver = "human";
    }

    std::sort(analyses.begin(), analyses.end(), [](const MoveAnalysis& lhs, const MoveAnalysis& rhs) {
        return lhs.score > rhs.score;
    });
    return analyses;
}

std::optional<StatTracker> buildModelStats(const CliOptions& options, const StatTracker& globalStats, const GameState& game) {
    StatTracker modelStats;

    if (options.modelStatsSource == "global" || options.modelStatsSource == "merged") {
        modelStats.mergeFrom(globalStats);
    }

    if (options.modelStatsSource == "session" || options.modelStatsSource == "merged") {
        if (options.modelSessionName.empty()) {
            modelStats.mergeFrom(game.sessionStats);
        } else {
            if (!mergeSessionStats(modelStats, options.modelSessionName)) {
                return std::nullopt;
            }
            if (sanitizedSessionName(options.modelSessionName) != sanitizedSessionName(options.sessionName)) {
                modelStats.mergeFrom(game.sessionStats);
            }
        }
    }

    return modelStats;
}

std::vector<HumanMoveExample> buildHumanExamples(const CliOptions& options, const GameState& game) {
    std::vector<HumanMoveExample> examples;
    if (!options.modelSessionName.empty()) {
        GameState modelGame;
        if (loadSession(modelGame, options.modelSessionName)) {
            examples = humanMoveExamples(modelGame);
        }
    }
    if (options.modelSessionName.empty() ||
        sanitizedSessionName(options.modelSessionName) != sanitizedSessionName(options.sessionName)) {
        const auto currentExamples = humanMoveExamples(game);
        examples.insert(examples.end(), currentExamples.begin(), currentExamples.end());
    }
    return examples;
}

std::vector<MoveAnalysis> computeMoveAnalyses(
    const CliOptions& options,
    const GameState& game,
    const StatTracker& modelStats,
    const std::vector<HumanMoveExample>& humanExamples) {
    const auto spawnModel = modelStats.spawnModel(game.moves, game.board.highestTile());
    if (options.solver == "expectimax") {
        return analyzeExpectimaxMoves(game.board, spawnModel, options.depth, options.chanceCells);
    }
    if (options.solver == "optimistic") {
        return analyzeOptimisticMoves(game.board, spawnModel, options.depth, options.chanceCells);
    }
    if (options.solver == "human") {
        return analyzeHumanPriorMoves(game.board, spawnModel, options, humanExamples);
    }
    if (options.solver == "target") {
        return analyzeTargetMoves(
            game.board,
            spawnModel,
            {options.rollouts, options.maxRolloutMoves, options.targetRank, options.simulationSeed + static_cast<unsigned int>(game.moves * 2654435761U)});
    }

    const HybridOptions hybridOptions{
        options.depth,
        options.rollouts,
        options.candidates,
        options.maxRolloutMoves,
        options.chanceCells,
        options.simulationSeed + static_cast<unsigned int>(game.moves * 2654435761U),
    };
    return analyzeHybridMoves(game.board, spawnModel, hybridOptions);
}

void writeSuggestionJson(const CliOptions& options, const GameState& game, const MoveSuggestion& suggestion, const std::vector<MoveAnalysis>& analyses) {
    std::cout << "{"
              << "\"ok\": true"
              << ", \"direction\": \"" << directionName(suggestion.direction) << "\""
              << ", \"solver\": \"" << jsonEscape(suggestion.solver) << "\""
              << ", \"quality\": \"" << jsonEscape(options.quality) << "\""
              << ", \"model_stats\": \"" << jsonEscape(options.modelStatsSource) << "\""
              << ", \"model_session\": \"" << jsonEscape(options.modelSessionName) << "\""
              << ", \"depth\": " << suggestion.depth
              << ", \"rollouts\": " << suggestion.rollouts
              << ", \"target\": " << options.targetRank
              << ", \"score\": " << std::fixed << std::setprecision(6) << suggestion.score << std::defaultfloat
              << ", \"moves\": " << game.moves
              << ", \"highest\": " << game.board.highestTile()
              << ", \"ranking\": [";
    for (std::size_t index = 0; index < analyses.size(); ++index) {
        if (index != 0) {
            std::cout << ", ";
        }
        const auto& analysis = analyses[index];
        std::cout << "{"
                  << "\"direction\": \"" << directionName(analysis.direction) << "\""
                  << ", \"score\": " << std::fixed << std::setprecision(6) << analysis.score << std::defaultfloat
                  << ", \"depth\": " << analysis.depth
                  << ", \"rollouts\": " << analysis.rollouts
                  << "}";
    }
    std::cout << "]"
              << "}\n";
}

bool placeTile(GameState& game, StatTracker& globalStats, std::vector<UndoSnapshot>& history, const std::string& prompt) {
    const int value = readValue("Valeur du bloc a placer (1 ou 2 conseille, entree=1): ");

    while (true) {
        std::cout << prompt << " (ex: a1 ou 1a), x pour quitter: ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "x" || line == "X") {
            return false;
        }

        const auto cell = parseCell(line);
        if (!cell) {
            std::cout << "Format attendu: a1, 1a ou 1 1.\n";
            continue;
        }

        const int highestBeforeSpawn = game.board.highestTile();
        rememberUndo(history, game, globalStats);
        if (game.board.spawn(*cell, value)) {
            game.sessionStats.recordSpawn(value, game.moves, highestBeforeSpawn);
            return true;
        }
        history.pop_back();
        std::cout << "Case invalide ou deja occupee.\n";
    }
}

bool setupInitialBoard(GameState& game, StatTracker& globalStats, std::vector<UndoSnapshot>& history) {
    std::cout << "Saisie du plateau initial du 2048 en ligne.\n";
    std::cout << "Ajoute les blocs visibles un par un. Appuie sur entree a la valeur pour commencer la partie.\n";

    while (true) {
        game.board.print(std::cout);
        std::cout << "Valeur initiale (entree pour demarrer, x pour quitter): ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "x" || line == "X") {
            return false;
        }
        if (line == "u" || line == "U") {
            if (!restoreUndo(history, game, globalStats, 1)) {
                std::cout << "Aucun etat precedent disponible.\n";
            }
            continue;
        }
        if (line.empty()) {
            return true;
        }

        int value = 0;
        try {
            value = std::stoi(line);
        } catch (...) {
            std::cout << "Valeur invalide.\n";
            continue;
        }
        if (value <= 0) {
            std::cout << "La valeur doit etre positive.\n";
            continue;
        }

        while (true) {
            std::cout << "Position du " << value << " (ex: a1 ou 1a): ";
            std::getline(std::cin, line);
            if (line == "u" || line == "U") {
                if (!restoreUndo(history, game, globalStats, 1)) {
                    std::cout << "Aucun etat precedent disponible.\n";
                }
                break;
            }
            const auto cell = parseCell(line);
            if (!cell) {
                std::cout << "Format attendu: a1, 1a ou 1 1.\n";
                continue;
            }
            rememberUndo(history, game, globalStats);
            if (game.board.spawn(*cell, value)) {
                break;
            }
            history.pop_back();
            std::cout << "Case invalide ou deja occupee.\n";
        }
    }
}

void editBoard(GameState& game, StatTracker& globalStats, std::vector<UndoSnapshot>& history) {
    std::cout << "Mode edit: saisis une case puis une valeur. Valeur 0 = vider la case, entree = sortir.\n";
    while (true) {
        game.board.print(std::cout);
        std::cout << "Case a modifier (ex: b3, entree pour finir): ";
        std::string line;
        std::getline(std::cin, line);
        if (line.empty()) {
            return;
        }
        const auto cell = parseCell(line);
        if (!cell) {
            std::cout << "Format attendu: a1, 1a ou 1 1.\n";
            continue;
        }
        std::cout << "Nouvelle valeur pour cette case (0 pour vider): ";
        std::getline(std::cin, line);
        int value = 0;
        try {
            value = std::stoi(line);
        } catch (...) {
            std::cout << "Valeur invalide.\n";
            continue;
        }
        rememberUndo(history, game, globalStats);
        if (!game.board.setCell(cell->row, cell->col, value)) {
            history.pop_back();
            std::cout << "Case ou valeur invalide.\n";
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const CliOptions options = readOptions(argc, argv);
    const bool hasSession = !options.sessionName.empty();
    StatTracker globalStats;
    globalStats.load(StatsPath);

    if (options.suggestOnly) {
        if (!hasSession) {
            writeJsonError("--suggest-only demande --session NOM.");
            return 1;
        }

        GameState game;
        if (!loadSession(game, options.sessionName)) {
            writeJsonError("Session introuvable ou invalide: " + sessionPath(options.sessionName));
            return 1;
        }
        applyStartContext(game, options);

        const auto modelStats = buildModelStats(options, globalStats, game);
        if (!modelStats) {
            writeJsonError("Modele de stats introuvable: " + sessionPath(options.modelSessionName));
            return 1;
        }

        const auto humanExamples = buildHumanExamples(options, game);
        const auto analyses = computeMoveAnalyses(options, game, *modelStats, humanExamples);
        if (analyses.empty()) {
            writeJsonError("Aucun coup possible.");
            return 1;
        }
        const auto& best = analyses.front();
        const MoveSuggestion suggestion{best.direction, best.score, best.depth, best.rollouts, best.solver};

        writeSuggestionJson(options, game, suggestion, analyses);
        return 0;
    }

    if (options.simulations > 0) {
        if (options.startFromSession && wantsSolvedCheckpoint(options)) {
            std::cout << "Erreur: choisis soit --start-from-session, soit --start-from-solved/--checkpoint-move.\n";
            return 1;
        }
        if (wantsSolvedCheckpoint(options) && !hasSession) {
            std::cout << "Erreur: --start-from-solved et --checkpoint-move demandent aussi --session NOM.\n";
            return 1;
        }

        std::optional<GameState> startGame;
        SimulationStartInfo startInfo;
        std::vector<HumanMoveExample> humanExamples;
        StatTracker simulationStats;
        if (options.simulationStatsSource == "global" || options.simulationStatsSource == "merged") {
            simulationStats.mergeFrom(globalStats);
        }
        if (hasSession) {
            GameState loaded;
            if (!loadSession(loaded, options.sessionName)) {
                std::cout << "Erreur: impossible de charger la session " << sessionPath(options.sessionName) << " pour apprendre les apparitions.\n";
                return 1;
            }
            if (options.simulationStatsSource == "session" || options.simulationStatsSource == "merged") {
                simulationStats.mergeFrom(loaded.sessionStats);
            }
            humanExamples = buildHumanExamples(options, loaded);
            if (wantsSolvedCheckpoint(options)) {
                const auto checkpoint = selectSolvedCheckpoint(loaded, options);
                if (!checkpoint) {
                    std::cout << "Erreur: aucun checkpoint " << options.solvedCheckpointSource
                              << " utilisable dans " << sessionPath(options.sessionName) << ".\n";
                    return 1;
                }
                startGame = checkpoint->game;
                startInfo.kind = "solved";
                startInfo.checkpointIndex = checkpoint->solvedIndex;
                startInfo.checkpointSource = checkpoint->source;
                startInfo.checkpointDirection = checkpoint->direction;
                std::cout << "Simulation IA depuis checkpoint " << checkpoint->source
                          << " #" << checkpoint->solvedIndex
                          << " de " << sessionPath(options.sessionName)
                          << " (" << checkpoint->game.moves << " coups, score "
                          << checkpoint->game.board.score() << ", max "
                          << checkpoint->game.board.highestTile() << ", "
                          << options.simulations << " branches).\n" << std::flush;
            } else if (options.startFromSession) {
                applyStartContext(loaded, options);
                startGame = loaded;
                startInfo.kind = "session";
                std::cout << "Simulation depuis " << sessionPath(options.sessionName)
                          << " (" << loaded.moves << " coups, max " << loaded.board.highestTile() << ", "
                          << options.simulations << " branches).\n" << std::flush;
            } else {
                std::cout << "Simulation depuis zero avec deux 1 aleatoires, modele enrichi par "
                          << sessionPath(options.sessionName) << " ("
                          << loaded.sessionStats.totalSpawns() << " apparitions, "
                          << options.simulations << " parties).\n" << std::flush;
            }
        } else {
            if (options.simulationStatsSource == "session") {
                std::cout << "Erreur: --sim-stats session demande aussi --session NOM.\n";
                return 1;
            }
            std::cout << "Simulation depuis zero avec deux 1 aleatoires et " << StatsPath
                      << " (" << options.simulations << " parties).\n" << std::flush;
        }
        runSimulations(options, simulationStats, startGame, startInfo, humanExamples);
        return 0;
    }

    if (!hasSession) {
        std::cout << "Erreur: ajoute un nom de session pour sauvegarder le plateau.\n";
        std::cout << "Exemple: ./2048-ranks --stats read-write --solver hybrid --session compte1\n";
        return 1;
    }

    GameState game;
    std::vector<UndoSnapshot> history;

    std::cout << "2048 ranks: 1+1=2, 2+2=3, etc.\n";
    std::cout << "Stats globales: " << (options.updateStats && !hasSession ? "read-write" : "read-only") << " (" << StatsPath << ")\n";
    if (hasSession) {
        std::cout << "Session temporaire: " << sessionPath(options.sessionName) << "\n";
    }
    printHelp();

    if (hasSession && loadSession(game, options.sessionName)) {
        applyStartContext(game, options);
        saveSession(game, options.sessionName);
        std::cout << "Session rechargee: " << game.moves << " coups, " << game.sessionStats.totalSpawns() << " apparitions temporaires.\n";
        if (!hasAnyTile(game.board)) {
            std::cout << "Session sans plateau sauvegarde: saisis le plateau actuel.\n";
            if (!setupInitialBoard(game, globalStats, history)) {
                saveSession(game, options.sessionName);
                return 0;
            }
            saveSession(game, options.sessionName);
        }
    } else if (!setupInitialBoard(game, globalStats, history)) {
        saveStatsIfNeeded(globalStats, options, hasSession);
        if (hasSession) {
            saveSession(game, options.sessionName);
        }
        return 0;
    } else {
        applyStartContext(game, options);
        if (hasSession) {
            saveSession(game, options.sessionName);
        }
    }

    while (true) {
        game.board.print(std::cout);
        std::cout << "Coups: " << game.moves << "\n";

        const bool gameOver = !game.board.hasAnyMove();
        if (gameOver) {
            if (!outcomeEnded(game)) {
                game.outcomeJson = endedOutcomeJson(game);
            }
            std::cout << "Partie terminee. Score final: " << game.board.score() << ", coups: " << game.moves << ", tu as atteint " << game.board.highestTile() << ".\n";
            if (hasSession) {
                saveSession(game, options.sessionName);
            } else {
                saveStatsIfNeeded(globalStats, options, hasSession);
            }
        } else {
            const auto modelStats = buildModelStats(options, globalStats, game);
            if (!modelStats) {
                std::cout << "Modele de stats introuvable: " << sessionPath(options.modelSessionName) << "\n";
                continue;
            }
            const auto spawnModel = modelStats->spawnModel(game.moves, game.board.highestTile());

            std::optional<MoveSuggestion> suggestion;
            if (options.solver == "expectimax") {
                suggestion = suggestMove(game.board, spawnModel, options.depth, options.chanceCells);
            } else if (options.solver == "optimistic") {
                suggestion = suggestOptimisticMove(game.board, spawnModel, options.depth, options.chanceCells);
            } else if (options.solver == "human") {
                const auto humanExamples = buildHumanExamples(options, game);
                const auto analyses = analyzeHumanPriorMoves(game.board, spawnModel, options, humanExamples);
                if (!analyses.empty()) {
                    const auto& best = analyses.front();
                    suggestion = MoveSuggestion{best.direction, best.score, best.depth, best.rollouts, best.solver};
                }
                if (options.explain && !analyses.empty()) {
                    std::cout << "Classement IA:";
                    for (const auto& analysis : analyses) {
                        std::cout << " " << directionName(analysis.direction) << "="
                                  << std::fixed << std::setprecision(3) << analysis.score
                                  << std::defaultfloat;
                    }
                    std::cout << "\n";
                }
            } else if (options.solver == "target") {
                const TargetOptions targetOptions{
                    options.rollouts,
                    options.maxRolloutMoves,
                    options.targetRank,
                    options.simulationSeed + static_cast<unsigned int>(game.moves * 2654435761U),
                };
                suggestion = suggestTargetMove(game.board, spawnModel, targetOptions);
                if (options.explain) {
                    const auto analyses = analyzeTargetMoves(game.board, spawnModel, targetOptions);
                    std::cout << "Classement IA:";
                    for (const auto& analysis : analyses) {
                        std::cout << " " << directionName(analysis.direction) << "="
                                  << std::fixed << std::setprecision(3) << analysis.score
                                  << std::defaultfloat;
                    }
                    std::cout << "\n";
                }
            } else {
                const HybridOptions hybridOptions{options.depth, options.rollouts, options.candidates, options.maxRolloutMoves, options.chanceCells};
                suggestion = suggestHybridMove(
                    game.board,
                    spawnModel,
                    hybridOptions);
                if (options.explain) {
                    const auto analyses = analyzeHybridMoves(game.board, spawnModel, hybridOptions);
                    std::cout << "Classement IA:";
                    for (const auto& analysis : analyses) {
                        std::cout << " " << directionName(analysis.direction) << "="
                                  << std::fixed << std::setprecision(3) << analysis.score
                                  << std::defaultfloat;
                    }
                    std::cout << "\n";
                }
            }

            if (suggestion) {
                std::cout << "Suggestion IA: " << directionName(suggestion->direction)
                          << " (" << suggestion->solver << ", profondeur " << suggestion->depth;
                if (suggestion->rollouts > 0) {
                    std::cout << ", " << suggestion->rollouts << " rollouts";
                }
                std::cout << ")\n";
            }
        }

        std::cout << "Ton coup: ";
        std::string command;
        std::getline(std::cin, command);
        if (command.empty()) {
            continue;
        }
        const char input = command[0];

        if (input == 'x' || input == 'X') {
            if (hasSession) {
                saveSession(game, options.sessionName);
                std::cout << "A plus. Session sauvegardee sans publier les stats globales.\n";
            } else {
                saveStatsIfNeeded(globalStats, options, hasSession);
                std::cout << (options.updateStats ? "A plus. Stats sauvegardees.\n" : "A plus. Stats laissees intactes.\n");
            }
            return 0;
        }
        if (input == 'h' || input == 'H') {
            printHelp();
            continue;
        }
        if (input == 'c' || input == 'C') {
            editContext(game, globalStats, history);
            if (hasSession) {
                saveSession(game, options.sessionName);
            }
            continue;
        }
        if (input == 'e' || input == 'E') {
            editBoard(game, globalStats, history);
            if (hasSession) {
                saveSession(game, options.sessionName);
            }
            continue;
        }
        if (input == 'u' || input == 'U') {
            const int count = parseUndoCount(command);
            if (!restoreUndo(history, game, globalStats, count)) {
                std::cout << "Aucun etat precedent disponible.\n";
            } else {
                if (hasSession) {
                    saveSession(game, options.sessionName);
                    globalStats.save(StatsPath);
                } else {
                    saveStatsIfNeeded(globalStats, options, hasSession);
                }
                std::cout << "Retour en arriere effectue (" << count << " pas demandes, " << history.size() << " etats encore disponibles).\n";
            }
            continue;
        }
        if (input == 'p' || input == 'P') {
            if (!hasSession) {
                std::cout << "Pas de session temporaire a publier.\n";
            } else if (game.sessionStats.totalSpawns() == 0) {
                std::cout << "Aucune apparition temporaire en attente de publication.\n";
                if (gameOver) {
                    std::cout << "Partie terminee, fermeture.\n";
                    return 0;
                }
            } else {
                const int publishedSpawns = game.sessionStats.totalSpawns();
                rememberUndo(history, game, globalStats);
                if (publishSessionStats(globalStats, game)) {
                    saveSession(game, options.sessionName);
                    std::cout << publishedSpawns << " apparitions de la session publiees dans les vraies stats.\n";
                    if (gameOver) {
                        std::cout << "Partie terminee, fermeture.\n";
                        return 0;
                    }
                } else {
                    history.pop_back();
                    std::cout << "Impossible de publier les stats.\n";
                }
            }
            continue;
        }
        if (input == 'r' || input == 'R') {
            rememberUndo(history, game, globalStats);
            const StatTracker keptSessionStats = game.sessionStats;
            game = GameState();
            game.sessionStats = keptSessionStats;
            if (!setupInitialBoard(game, globalStats, history)) {
                if (hasSession) {
                    saveSession(game, options.sessionName);
                } else {
                    saveStatsIfNeeded(globalStats, options, hasSession);
                }
                return 0;
            }
            applyStartContext(game, options);
            if (hasSession) {
                saveSession(game, options.sessionName);
            }
            continue;
        }

        const auto direction = directionFromInput(input);
        if (!direction) {
            std::cout << "Commande inconnue.\n";
            continue;
        }
        if (gameOver) {
            std::cout << "Partie terminee: utilise p pour publier, r pour nouveau plateau, ou x pour quitter.\n";
            continue;
        }

        rememberUndo(history, game, globalStats);
        const Board beforeMoveBoard = game.board;
        const int beforeMoveCount = game.moves;
        const int beforeMoveScore = game.board.score();
        const int beforeMoveHighest = game.board.highestTile();
        if (!game.board.move(*direction)) {
            history.pop_back();
            std::cout << "Aucun bloc ne bouge vers " << directionName(*direction) << ".\n";
            continue;
        }
        appendSolvedMove(game, *direction, beforeMoveBoard, beforeMoveCount, beforeMoveScore, beforeMoveHighest);
        ++game.moves;

        game.board.print(std::cout);
        if (!placeTile(game, globalStats, history, "Bloc apparu en ligne")) {
            if (hasSession) {
                saveSession(game, options.sessionName);
            } else {
                saveStatsIfNeeded(globalStats, options, hasSession);
            }
            return 0;
        }

        if (hasSession) {
            saveSession(game, options.sessionName);
        } else if (options.updateStats) {
            globalStats.mergeFrom(game.sessionStats);
            game.sessionStats = StatTracker();
            saveStatsIfNeeded(globalStats, options, hasSession);
        }
    }
}
