CXX := g++
TARGET := valerain

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := bin
PGO_DIR := $(BUILD_DIR)/pgo

SRC := \
	Attack.cpp \
	Bench.cpp \
	Evaluation.cpp \
	History.cpp \
	Main.cpp \
	MovePicker.cpp \
	MoveGen.cpp \
	Nnue.cpp \
	Perft.cpp \
	Position.cpp \
	Search.cpp \
	See.cpp \
	Time.cpp \
	TT.cpp \
	Uci.cpp

OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

ARCH_FLAGS := \
	-m64 \
	-mavx2 \
	-mbmi2 \
	-mpopcnt \
	-mlzcnt

OPT_FLAGS := \
	-Ofast \
	-DNDEBUG \
	-flto=auto \
	-fuse-linker-plugin \
	-fomit-frame-pointer \
	-funroll-loops \
	-fstrict-aliasing \
	-ffast-math \
	-fno-math-errno \
	-fno-signed-zeros \
	-fno-trapping-math \
	-fdevirtualize \
	-fdevirtualize-at-ltrans \
	-ftree-vectorize \
	-fipa-pta \
	-fipa-cp \
	-fipa-cp-clone \
	-fipa-icf \
	-falign-functions=32 \
	-falign-loops=32 \
	-falign-jumps=32 \
	-falign-labels=32 \
	-fweb \
	-fprefetch-loop-arrays

LOWLEVEL_FLAGS := \
	-fno-exceptions \
	-fno-rtti

WARN_FLAGS := \
	-Wall \
	-Wextra \
	-Wshadow \
	-Wconversion \
	-Wcast-qual \
	-Wno-unused-parameter \
	-Wno-sign-conversion

CPPFLAGS := -MMD -MP

CXXFLAGS := \
	-std=c++20 \
	$(ARCH_FLAGS) \
	$(OPT_FLAGS) \
	$(LOWLEVEL_FLAGS) \
	$(WARN_FLAGS) \
	$(CPPFLAGS)

LDFLAGS := \
	$(ARCH_FLAGS) \
	-Ofast \
	-flto=auto

LDLIBS :=

ifeq ($(OS),Windows_NT)
	EXEEXT := .exe

	fixpath = $(subst /,\,$(1))

	MKDIR_P = if not exist "$(call fixpath,$(1))" mkdir "$(call fixpath,$(1))"
	RM_F    = if exist "$(call fixpath,$(1))" del /q "$(call fixpath,$(1))"
	RMDIR_R = if exist "$(call fixpath,$(1))" rmdir /s /q "$(call fixpath,$(1))"

	RUN_CMD = .\$(call fixpath,$(BIN_DIR))\$(TARGET)$(EXEEXT)
else
	EXEEXT :=
	MKDIR_P = mkdir -p "$(1)"
	RM_F    = rm -f "$(1)"
	RMDIR_R = rm -rf "$(1)"
	RUN_CMD = ./$(BIN_DIR)/$(TARGET)$(EXEEXT)
endif

OUTPUT := $(BIN_DIR)/$(TARGET)$(EXEEXT)
PGO_OUTPUT := $(BIN_DIR)/$(TARGET)_pgo$(EXEEXT)

.PHONY: all release releases
all: $(OUTPUT)
release: all
releases: all

$(OUTPUT): $(OBJ) | $(BIN_DIR)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(PGO_OUTPUT): $(OBJ) | $(BIN_DIR)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	$(call MKDIR_P,$@)

$(BIN_DIR):
	$(call MKDIR_P,$@)

$(PGO_DIR):
	$(call MKDIR_P,$@)

.PHONY: pgo-generate
pgo-generate: CXXFLAGS += -fprofile-generate=$(PGO_DIR)
pgo-generate: LDFLAGS  += -fprofile-generate=$(PGO_DIR)
pgo-generate: clean $(PGO_OUTPUT)

.PHONY: pgo-use
pgo-use: CXXFLAGS += -fprofile-use=$(PGO_DIR) -fprofile-correction
pgo-use: LDFLAGS  += -fprofile-use=$(PGO_DIR) -fprofile-correction
pgo-use: clean $(OUTPUT)

.PHONY: run
run: all
	$(RUN_CMD)

.PHONY: clean
clean:
	$(call RMDIR_R,$(OBJ_DIR))
	$(call RM_F,$(OUTPUT))
	$(call RM_F,$(PGO_OUTPUT))

.PHONY: distclean
distclean: clean
	$(call RMDIR_R,$(PGO_DIR))
	$(call RMDIR_R,$(BUILD_DIR))

-include $(DEP)
