# Digital Wallet & Payment System - build with GNU make + g++ (C++20).
# Windows: run from Git Bash / MSYS2 / Command Prompt / PowerShell with `mingw32-make`; Linux/macOS: `make`.

CXX      ?= g++
STD      := -std=c++20
WARN     := -Wall -Wextra -Wpedantic
INC      := -Iinclude
BUILD    := build
BIN      := $(BUILD)/wallet
TESTBIN  := $(BUILD)/wallet_tests

LIB_SRCS  := $(filter-out src/main.cpp,$(wildcard src/*.cpp src/*/*.cpp))
TEST_SRCS := $(wildcard tests/*.cpp)

# Platform detection: Windows vs macOS vs Linux
ifeq ($(OS),Windows_NT)
  LDFLAGS   += -static
  EXE       := .exe
  CLEAN_CMD  = cmd.exe /c if exist "$(subst /,\,$(1))" rmdir /s /q "$(subst /,\,$(1))"
  MKDIR_CMD  = cmd.exe /c if not exist "$(subst /,\,$(patsubst %/,%,$(1)))" mkdir "$(subst /,\,$(patsubst %/,%,$(1)))"
  RUN_CMD    = $(subst /,\,$(1))
else
  EXE       :=
  CLEAN_CMD  = rm -rf $(1)
  MKDIR_CMD  = mkdir -p $(1)
  RUN_CMD    = ./$(1)
  # macOS linker does not support -static libc
  UNAME_S   := $(shell uname -s 2>/dev/null)
  ifneq ($(UNAME_S),Darwin)
    # Linux can optionally link static or dynamic
  endif
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
	@$(call MKDIR_CMD,$(dir $@))
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(TESTBIN): $(LIB_OBJS) $(TEST_OBJS)
	@$(call MKDIR_CMD,$(dir $@))
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ)/%.o: src/%.cpp
	@$(call MKDIR_CMD,$(dir $@))
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ)/tests/%.o: tests/%.cpp
	@$(call MKDIR_CMD,$(dir $@))
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: $(TESTBIN)
	@$(call RUN_CMD,$(TESTBIN))

run: $(BIN)
	@$(call RUN_CMD,$(BIN))

demo: $(BIN)
	@$(call RUN_CMD,$(BIN)) --demo

clean:
	@$(call CLEAN_CMD,$(BUILD))

help:
	@echo make [release^|debug]  build ./build/wallet
	@echo make test             build and run the automated tests
	@echo make run ^| demo       launch the CLI ^| run the scripted demo
	@echo make clean            remove build output

-include $(wildcard $(OBJ)/*.d $(OBJ)/*/*.d $(OBJ)/tests/*.d)
