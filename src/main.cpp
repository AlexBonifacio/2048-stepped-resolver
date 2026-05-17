#include "board.hpp"
#include "move.hpp"
#include "solver.hpp"
#include "stat.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <limits>
#include <random>
#include <string>

namespace {

bool hasFlag(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == flag) {
            return true;
        }
    }
    return false;
}

void printHelp() {
    std::cout << "Commandes: z/w=haut, s=bas, q/a=gauche, d=droite, h=aide placement, r=restart, x=quitter\n";
    std::cout << "Mode par defaut: apres chaque coup, tu places le nouveau bloc.\n";
    std::cout << "Lance avec --random-spawn pour retrouver un spawn automatique type 2048.\n";
}

int readValue() {
    int value = 1;
    std::cout << "Valeur du bloc a placer (1 ou 2 conseille, entree=1): ";
    std::string line;
    std::getline(std::cin, line);
    if (line.empty()) {
        return 1;
    }
    try {
        value = std::stoi(line);
    } catch (...) {
        return 1;
    }
    return value > 0 ? value : 1;
}

bool placeManually(Board& board, StatTracker& stats) {
    int value = readValue();

    while (true) {
        if (const auto suggestion = suggestSpawn(board, value)) {
            std::cout << "Suggestion IA: ligne " << suggestion->cell.row + 1
                      << ", colonne " << suggestion->cell.col + 1
                      << " pour un " << suggestion->value << "\n";
        }

        std::cout << "Placement (ligne colonne), h pour revoir l'aide, x pour quitter: ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "x" || line == "X") {
            return false;
        }
        if (line == "h" || line == "H") {
            continue;
        }

        int row = 0;
        int col = 0;
        if (std::sscanf(line.c_str(), "%d %d", &row, &col) != 2) {
            std::cout << "Format attendu: ligne colonne, par exemple 2 3.\n";
            continue;
        }

        if (board.spawn({row - 1, col - 1}, value)) {
            stats.recordSpawn(value);
            return true;
        }
        std::cout << "Case invalide ou deja occupee.\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    const bool randomSpawn = hasFlag(argc, argv, "--random-spawn");
    std::mt19937 rng(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));

    Board board;
    StatTracker stats;
    board.start(rng);

    std::cout << "2048 ranks: 1+1=2, 2+2=3, etc.\n";
    printHelp();

    while (true) {
        board.print(std::cout);

        if (!board.hasAnyMove()) {
            std::cout << "Partie terminee. Score final: " << board.score() << ", tu as atteint " << board.highestTile() << ".\n";
            stats.save("data/observed_spawns.json");
            return 0;
        }

        std::cout << "Ton coup: ";
        char input = '\0';
        std::cin >> input;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (input == 'x' || input == 'X') {
            stats.save("data/observed_spawns.json");
            std::cout << "A plus. Stats sauvegardees dans data/observed_spawns.json.\n";
            return 0;
        }
        if (input == 'h' || input == 'H') {
            printHelp();
            continue;
        }
        if (input == 'r' || input == 'R') {
            board.start(rng);
            continue;
        }

        const auto direction = directionFromInput(input);
        if (!direction) {
            std::cout << "Commande inconnue.\n";
            continue;
        }

        if (!board.move(*direction)) {
            std::cout << "Aucun bloc ne bouge vers " << directionName(*direction) << ".\n";
            continue;
        }

        if (randomSpawn) {
            int spawnedValue = 0;
            if (board.spawnRandom(rng, &spawnedValue)) {
                stats.recordSpawn(spawnedValue);
            }
            continue;
        }

        board.print(std::cout);
        if (!placeManually(board, stats)) {
            stats.save("data/observed_spawns.json");
            return 0;
        }
    }
}
