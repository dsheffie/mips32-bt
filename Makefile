UNAME_S = $(shell uname -s)
LIBS =  $(EXTRA_LD) -lpthread -lffi -lcurses -lz
ifeq ($(UNAME_S),Linux)
     LLVM_CXXFLAGS = $(shell llvm-config-11 --cppflags)
     LLVM_LDFLAGS = $(shell llvm-config-11 --ldflags --libs all)
     CXX = clang++-12 -fomit-frame-pointer -flto=thin
     EXTRA_LD = -ldl -lffi -lbfd -lboost_program_options -lunwind -lcapstone
     DL = -Wl,--export-dynamic 
endif

ifeq ($(UNAME_S),FreeBSD)
     LLVM_CXXFLAGS = $(shell llvm-config10 --cppflags)
     LLVM_LDFLAGS = $(shell llvm-config10 --ldflags --libs all)
     CXX = clang++ -march=native -fomit-frame-pointer -flto
     EXTRA_LD = -L/usr/local/lib -lboost_program_options  -lunwind -lcapstone
     DL = -Wl,--export-dynamic 
endif

ifeq ($(UNAME_S),Darwin)
     LLVM_CXXFLAGS = $(shell llvm-config-mp-9.0 --cppflags)
     LLVM_LDFLAGS = $(shell llvm-config-mp-9.0 --ldflags --libs all)
     CXX = clang++ -march=native -fomit-frame-pointer -I/opt/local/include
     EXTRA_LD = -L/opt/local/lib -lboost_program_options-mt
endif

CXXFLAGS = -std=c++14 -g $(OPT) $(LLVM_CXXFLAGS)


OPT = -O3 -g -Wall -Wpedantic -Wextra -Wno-unused-parameter 
EXE = cfg_mips
OBJ = main.o cfgBasicBlock.o loadelf.o disassemble.o helper.o interpret.o basicBlock.o compile.o region.o mipsInstruction.o regionCFG.o perfmap.o debugSymbols.o saveState.o simPoints.o githash.o state.o
DEP = $(OBJ:.o=.d)

.PHONY: all clean

all: $(EXE)

$(EXE) : $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) $(LLVM_LDFLAGS) $(LIBS) $(DL) -o $(EXE)

githash.cc : .git/HEAD .git/index
	echo "const char *githash = \"$(shell git rev-parse HEAD)\";" > $@

%.o: %.cc
	$(CXX) -MMD $(CXXFLAGS) -c $<

-include $(DEP)

clean:
	rm -rf $(EXE) $(OBJ) $(DEP)
