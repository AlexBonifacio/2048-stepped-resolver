CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2
SRC := $(wildcard src/*.cpp)
LIB_SRC := $(filter-out src/main.cpp,$(SRC))
OBJ := $(SRC:.cpp=.o)
TEST_OBJ := tests/test_board.o $(LIB_SRC:.cpp=.o)
BIN := 2048-ranks
TEST_BIN := test-2048-ranks

.PHONY: all clean run test

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run: $(BIN)
	./$(BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(OBJ) tests/*.o $(BIN) $(TEST_BIN)
