CXX      := clang++
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -Wno-c2y-extensions -I.
BUILD    := build

EXAMPLE_SRC := examples/example.cpp
EXAMPLE_BIN := $(BUILD)/example

TEST_SRC := tests/tests.cpp tests/catch_amalgamated.cpp
TEST_BIN := $(BUILD)/test_runner

COV_FLAGS := -fprofile-instr-generate -fcoverage-mapping
COV_DIR   := coverage

.PHONY: all clean lint test coverage docs

all: $(EXAMPLE_BIN)

$(EXAMPLE_BIN): $(EXAMPLE_SRC) dasmig/namegen.hpp dasmig/random.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(EXAMPLE_SRC)

$(TEST_BIN): $(TEST_SRC) dasmig/namegen.hpp dasmig/random.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_SRC)

test: $(TEST_BIN)
	./$(TEST_BIN)

coverage: $(TEST_SRC) dasmig/namegen.hpp dasmig/random.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(COV_FLAGS) -o $(BUILD)/test_cov $(TEST_SRC)
	cd $(BUILD) && ./test_cov
	llvm-profdata merge -sparse $(BUILD)/default.profraw -o $(BUILD)/tests.profdata
	llvm-cov report $(BUILD)/test_cov --instr-profile=$(BUILD)/tests.profdata --sources dasmig/namegen.hpp
	llvm-cov show $(BUILD)/test_cov --instr-profile=$(BUILD)/tests.profdata --sources dasmig/namegen.hpp --format=html -o $(COV_DIR)
	@echo "Coverage report: $(COV_DIR)/index.html"

$(BUILD):
	@mkdir -p $(BUILD)

docs:
	doxygen Doxyfile
	@echo "API docs: doc/api/html/index.html"

lint:
	clang-tidy $(EXAMPLE_SRC) -- $(CXXFLAGS)

clean:
	rm -rf $(BUILD) $(COV_DIR) doc/api
