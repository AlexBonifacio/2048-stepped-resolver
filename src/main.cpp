#include "board.hpp"
#include "move.hpp"
#include "solver.hpp"
#include "stat.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
    int depth = 4;
    int rollouts = 200;
    int candidates = 2;
    int maxRolloutMoves = 80;
    int chanceCells = 12;
    std::optional<int> startScore;
    std::optional<int> startMoves;
    int simulations = 0;
    unsigned int simulationSeed = 0x2048;
    std::string simulationReportName;
    std::string solver = "hybrid";
    std::string quality = "balanced";
    std::string sessionName;
    bool updateStats = false;
    bool explain = false;
};

struct GameState {
    Board board;
    int moves = 0;
    StatTracker sessionStats;
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

bool hasFlag(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == name) {
            return true;
        }
    }
    return false;
}

CliOptions readOptions(int argc, char** argv) {
    CliOptions options;
    options.quality = readStringOption(argc, argv, "--quality", options.quality);
    if (options.quality == "strong") {
        options.depth = 6;
        options.rollouts = 800;
        options.candidates = 4;
        options.maxRolloutMoves = 140;
        options.chanceCells = Board::CellCount;
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
    options.startScore = readNonNegativeIntOption(argc, argv, "--start-score");
    options.startMoves = readNonNegativeIntOption(argc, argv, "--start-moves");
    options.simulations = readIntOption(argc, argv, "--simulate", options.simulations);
    options.simulationSeed = static_cast<unsigned int>(readIntOption(argc, argv, "--seed", static_cast<int>(options.simulationSeed)));
    options.simulationReportName = readStringOption(argc, argv, "--sim-report", options.simulationReportName);
    options.solver = readStringOption(argc, argv, "--solver", options.solver);
    options.sessionName = readStringOption(argc, argv, "--session", options.sessionName);
    options.explain = hasFlag(argc, argv, "--explain");
    const std::string statsMode = readStringOption(argc, argv, "--stats", "read-only");
    if (options.solver != "hybrid" && options.solver != "expectimax") {
        std::cout << "Solveur inconnu, utilisation du mode hybrid.\n";
        options.solver = "hybrid";
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
    std::cout << "Options: --quality fast|balanced|strong --solver hybrid|expectimax --explain --simulate N --sim-report NOM --seed N --depth 4 --rollouts 200 --candidates 2 --chance-cells 12 --max-rollout-moves 80 --start-score N --start-moves N --stats read-only|read-write.\n";
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
    out << "}\n}\n";
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

    return true;
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

struct SimulationResult {
    int score = 0;
    int moves = 0;
    int highest = 0;
};

SimulationResult runSimulation(const CliOptions& options, const StatTracker& stats, std::mt19937& rng) {
    Board board;
    int moves = 0;

    spawnRandomFromModel(board, stats.spawnModel(moves, board.highestTile()), rng);
    spawnRandomFromModel(board, stats.spawnModel(moves, board.highestTile()), rng);

    while (board.hasAnyMove()) {
        const auto spawnModel = stats.spawnModel(moves, board.highestTile());
        std::optional<MoveSuggestion> suggestion;
        if (options.solver == "expectimax") {
            suggestion = suggestMove(board, spawnModel, options.depth, options.chanceCells);
        } else {
            suggestion = suggestHybridMove(
                board,
                spawnModel,
                {options.depth, options.rollouts, options.candidates, options.maxRolloutMoves, options.chanceCells, options.simulationSeed});
        }
        if (!suggestion || !board.move(suggestion->direction)) {
            break;
        }
        ++moves;
        spawnRandomFromModel(board, stats.spawnModel(moves, board.highestTile()), rng);
    }

    return {board.score(), moves, board.highestTile()};
}

void runSimulations(const CliOptions& options, const StatTracker& stats) {
    std::mt19937 rng(options.simulationSeed);
    long long totalScore = 0;
    long long totalMoves = 0;
    int bestScore = 0;
    int bestMoves = 0;
    int bestHighest = 0;
    std::map<int, int> highestCounts;
    std::vector<SimulationResult> results;

    for (int index = 1; index <= options.simulations; ++index) {
        const auto result = runSimulation(options, stats, rng);
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
                  << ", max " << result.highest << "\n";
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
            << ", \"seed\": " << options.simulationSeed
            << ", \"depth\": " << options.depth
            << ", \"rollouts\": " << options.rollouts
            << ", \"candidates\": " << options.candidates
            << ", \"chance_cells\": " << options.chanceCells
            << ", \"max_rollout_moves\": " << options.maxRolloutMoves
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
                << ", \"highest\": " << results[index].highest << "}";
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

    if (options.simulations > 0) {
        std::cout << "Simulation avec " << StatsPath << " (" << options.simulations << " parties).\n";
        runSimulations(options, globalStats);
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
            std::cout << "Partie terminee. Score final: " << game.board.score() << ", coups: " << game.moves << ", tu as atteint " << game.board.highestTile() << ".\n";
            if (hasSession) {
                saveSession(game, options.sessionName);
            } else {
                saveStatsIfNeeded(globalStats, options, hasSession);
            }
        } else {
            StatTracker modelStats = globalStats;
            modelStats.mergeFrom(game.sessionStats);
            const auto spawnModel = modelStats.spawnModel(game.moves, game.board.highestTile());

            std::optional<MoveSuggestion> suggestion;
            if (options.solver == "expectimax") {
                suggestion = suggestMove(game.board, spawnModel, options.depth, options.chanceCells);
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
                        std::cout << " " << directionName(analysis.direction) << "=" << static_cast<long long>(analysis.score);
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
        if (!game.board.move(*direction)) {
            history.pop_back();
            std::cout << "Aucun bloc ne bouge vers " << directionName(*direction) << ".\n";
            continue;
        }
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
