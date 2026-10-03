# Digital Wallet & Payment System - build with GNU make + g++ (C++20).
# Windows: run from Git Bash / MSYS2 with `mingw32-make`; Linux/macOS: `make`.
# (No CMake needed. PowerShell users without a POSIX shell can use scripts/build.ps1.)

CXX      ?= g++
STD      := -std=c++20
WARN     := -Wall -Wextra -Wpedantic
INC      := -Iinclude
BUILD    := build
BIN      := $(BUILD)/wallet
TESTBIN  := $(BUILD)/wallet_tests

LIB_SRCS  := $(filter-out src/main.cpp,$(wildcard src/*.cpp src/*/*.cpp))
TEST_SRCS := $(wildcard tests/*.cpp)

ifeq ($(OS),Windows_NT)
  LDFLAGS += -static
  EXE := .exe
endif
BIN     := $(BIN)$(EXE)
TESTBIN := $(TESTBIN)$(EXE)

# `make` / `make release` -> optimised; `make debug` -> -O0 -g with sanitizers off (portable)
MODE ?= release
ifeq ($(MODE),debug)
  OPT := -O0 -g -DDEBUG
  OBJ := $(BUILD)/debug
else
  OPT := -O2 -DNDEBUG
  OBJ := $(BUILD)/release
endif
CXXFLAGS := $(STD) $(WARN) $(INC) $(OPT)

LIB_OBJS  := $(patsubst src/%.cpp,$(OBJ)/%.o,$(LIB_SRCS))
MAIN_OBJ  := $(OBJ)/main.o
TEST_OBJS := $(patsubst tests/%.cpp,$(OBJ)/tests/%.o,$(TEST_SRCS))

.PHONY: all release debug test run demo clean help

all: $(BIN)
release: ; @$(MAKE) --no-print-directory MODE=release all
debug:   ; @$(MAKE) --no-print-directory MODE=debug all

$(BIN): $(LIB_OBJS) $(MAIN_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(TESTBIN): $(LIB_OBJS) $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: $(TESTBIN)
	./$(TESTBIN)

run: $(BIN)
	./$(BIN)

demo: $(BIN)
	./$(BIN) --demo

clean:
	rm -rf $(BUILD)

help:
	@echo "make [release|debug]  build ./build/wallet"
	@echo "make test             build and run the automated tests"
	@echo "make run | demo       launch the CLI | run the scripted demo"
	@echo "make clean            remove build output"

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
