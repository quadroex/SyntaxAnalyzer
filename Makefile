CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic
SOURCES = src/core.cpp src/semantic.cpp src/interpreter.cpp src/generator.cpp src/main.cpp

.PHONY: all test serve clean

all: build/minicpp

build:
	mkdir -p build

build/parser.cpp build/parser.hpp &: src/parser.y src/core.hpp | build
	bison -Wall -Wcounterexamples -Werror=conflicts-sr -Werror=conflicts-rr --report=state,solved --report-file=build/parser.output --defines=build/parser.hpp -o build/parser.cpp src/parser.y

build/lexer.cpp: src/lexer.l build/parser.hpp | build
	flex -o $@ $<

build/minicpp: $(SOURCES) src/core.hpp build/parser.cpp build/parser.hpp build/lexer.cpp
	$(CXX) $(CXXFLAGS) -Isrc -Ibuild $(SOURCES) build/parser.cpp build/lexer.cpp -o $@

test: build/minicpp
	python3 tests/test_project.py

serve: build/minicpp
	python3 server.py

clean:
	rm -f build/minicpp build/parser.cpp build/parser.hpp build/location.hh build/lexer.cpp build/parser.output
