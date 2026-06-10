# --- Configuration ---
# SystemC installation path 
SYSTEMC_HOME = /usr/local/systemc-3.0.2
TARGET_ARCH  = linux64

# Compiler and Flags
CXX      = g++
# Include current directory and SystemC headers 
CXXFLAGS = -Isrc -I$(SYSTEMC_HOME)/include -Wall -std=c++17
# Link SystemC libraries 
LDFLAGS  = -L$(SYSTEMC_HOME)/lib-$(TARGET_ARCH) -lsystemc -lm

# Project Files
# Listing all the modularized files created previously
SRCS = $(wildcard src/*.cpp)
OBJS = $(patsubst src/%.cpp, obj/%.o, $(SRCS))
EXE  = sim_result

# --- Rules ---
all: obj $(EXE)

# Rule to create the object directory if it doesn't exist
obj:
	mkdir -p obj

# Link the executable
$(EXE): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Rule to compile src/*.cpp into obj/*.o
obj/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf obj $(EXE)

# Build and run the simulation 
run: $(EXE)
	export LD_LIBRARY_PATH=$(SYSTEMC_HOME)/lib-$(TARGET_ARCH):$$LD_LIBRARY_PATH; ./$(EXE) 

.PHONY: all clean run
