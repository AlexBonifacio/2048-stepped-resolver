CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2
SRC := $(wildcard src/*.cpp)
LIB_SRC := $(filter-out src/main.cpp,$(SRC))
OBJ := $(SRC:.cpp=.o)
TEST_OBJ := tests/test_board.o $(LIB_SRC:.cpp=.o)
BIN := 2048-ranks
TEST_BIN := test-2048-ranks

.PHONY: all clean run run-game run-test test web package-windows

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run:
	@echo "Choisis explicitement:"
	@echo "  make run-game GAME_ARGS=\"--solver hybrid --session compte1\"  # partie reelle sauvegardee/reprise"
	@echo "  make run-game GAME_ARGS=\"--quality strong --solver hybrid --session compte1\"  # plus lent, plus fort"
	@echo "  make run-test TEST_ARGS=\"--solver hybrid --session test\"       # essai local sauvegarde"
	@echo "Note: --session est obligatoire pour eviter de perdre le plateau."

run-game: $(BIN)
	./$(BIN) --stats read-write $(GAME_ARGS)

run-test: $(BIN)
	./$(BIN) --stats read-only $(TEST_ARGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

web: $(BIN)
	python3 web/server.py $(WEB_ARGS)

package-windows:
	python3 tools/package_windows.py $(WINDOWS_PACKAGE_ARGS)

$(TEST_BIN): $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(OBJ) tests/*.o $(BIN) $(TEST_BIN)
