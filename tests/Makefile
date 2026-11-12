# Host tests for the ESPN parser and the saved Wi-Fi network list. Needs g++
# and the ArduinoJson single header next to this file (ArduinoJson.h, version
# 7.4.3 to match ESPHome).
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O1 -Wall -Wextra -I../components/gameday -I.
# Link the GCC runtime in on Windows. Git Bash puts its own older
# libstdc++-6.dll ahead of the compiler's on PATH, and the mismatched
# runtime faults on the first ifstream.
ifeq ($(OS),Windows_NT)
LDFLAGS += -static
endif

all: build/test_parse build/test_networks
	./build/test_parse
	./build/test_networks

TESTS = test_parse.cpp golden_football.cpp test_set.cpp test_alerts.cpp test_bets.cpp test_watch.cpp test_players.cpp test_mlbstats.cpp test_hockeytech.cpp test_split.cpp test_layout.cpp test_props.cpp test_apps.cpp test_flights.cpp
SRCS = $(TESTS) ../components/gameday/espn_parse.cpp ../components/gameday/box_score.cpp ../components/gameday/sport_baseball.cpp ../components/gameday/sport_soccer.cpp ../components/gameday/sport_basketball.cpp ../components/gameday/sport_hockey.cpp ../components/gameday/sport_golf.cpp ../components/gameday/sport_racing.cpp ../components/gameday/source_mlbstats.cpp ../components/gameday/source_hockeytech.cpp ../components/gameday/flights.cpp ../components/gameday/app_flights.cpp

build/test_parse: $(SRCS) $(wildcard ../components/gameday/*.h) $(wildcard ../components/crash_report/*.h) ../components/panel_layout/layouts.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@ $(LDFLAGS)

build/test_networks: test_networks.cpp ../components/saved_networks/network_list.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I../components/saved_networks test_networks.cpp -o $@ $(LDFLAGS)

# Not part of `all`: replays a folder of recorded single-event documents
# (`.../scoreboard/<event id>`, named ev_<id>_<UTC time>.json) through the
# parse, text and splash code and reports failures and odd splashes.
#   make replay && ./build/replay <folder> <sport: 1 baseball, 2 basketball, 3 hockey, 4 soccer> [name prefix]
replay: build/replay

build/replay: replay.cpp $(filter-out $(TESTS),$(SRCS)) $(wildcard ../components/gameday/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) replay.cpp $(filter-out $(TESTS),$(SRCS)) -o $@ $(LDFLAGS)

# Not part of `all`: the memory each ESPN read takes, as JSON for
# scripts/size_budget.py (CI's budget check, firmware/budget.json).
#   make heap              writes build/heap.json and prints it
heap: build/heap_budget
	./build/heap_budget > build/heap.json
	cat build/heap.json

build/heap_budget: heap_budget.cpp $(filter-out $(TESTS),$(SRCS)) $(wildcard ../components/gameday/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) heap_budget.cpp $(filter-out $(TESTS),$(SRCS)) -o $@ $(LDFLAGS)

# Not part of `all`: fuzzing. fuzz_espn runs mutated ESPN documents through
# every parser, the text code and the finals rotation; fuzz_set runs mutated
# set requests through the device page's parser. Both build with the address
# and undefined behavior sanitizers, seeded from fixtures/ (fuzz_seeds.cpp).
# With clang's libFuzzer the run is coverage guided; without it (g++, Apple
# clang) fuzz_main.cpp mutates at random. A crash leaves its input in
# build/crash-* (libFuzzer: timeout-*, oom-* or leak-* for those failures);
# `./build/fuzz_espn build/crash-...` replays it.
#   make fuzz              about a minute, as CI runs it
#   make fuzz-long         30 minutes a target (FUZZ_LONG_TIME seconds)
FUZZ_CXX ?= $(shell command -v clang++ >/dev/null 2>&1 && echo clang++ || echo $(CXX))
FUZZ_TIME ?= 30
FUZZ_LONG_TIME ?= 1800
FUZZ_CXXFLAGS = -std=c++17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
	-fno-sanitize-recover=all -D_GLIBCXX_ASSERTIONS -I../components/gameday -I.
LIBFUZZER := $(shell echo 'extern "C" int LLVMFuzzerTestOneInput(const unsigned char *, unsigned long) { return 0; }' | \
	$(FUZZ_CXX) -x c++ -fsanitize=fuzzer - -o /dev/null 2>/dev/null && echo yes)
ifeq ($(LIBFUZZER),yes)
FUZZ_ENGINE = -fsanitize=fuzzer
FUZZ_DEPS =
else
FUZZ_ENGINE = fuzz_main.cpp
FUZZ_DEPS = fuzz_main.cpp
endif
PARSER_SRCS = $(filter-out $(TESTS),$(SRCS))
FUZZ_RUN = -max_total_time=$(FUZZ_TIME) -timeout=10 -rss_limit_mb=2048 -artifact_prefix=build/ -print_final_stats=1

build/fuzz_espn: fuzz_espn.cpp $(FUZZ_DEPS) $(PARSER_SRCS) $(wildcard ../components/gameday/*.h)
	@mkdir -p build
	$(FUZZ_CXX) $(FUZZ_CXXFLAGS) $(FUZZ_ENGINE) fuzz_espn.cpp $(PARSER_SRCS) -o $@

FLIGHT_SRCS = ../components/gameday/flights.cpp ../components/gameday/app_flights.cpp
build/fuzz_set: fuzz_set.cpp $(FUZZ_DEPS) $(FLIGHT_SRCS) $(wildcard ../components/gameday/*.h) ../components/panel_layout/layouts.h
	@mkdir -p build
	$(FUZZ_CXX) $(FUZZ_CXXFLAGS) $(FUZZ_ENGINE) fuzz_set.cpp $(FLIGHT_SRCS) -o $@

build/fuzz_seeds: fuzz_seeds.cpp $(PARSER_SRCS) $(wildcard ../components/gameday/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) fuzz_seeds.cpp $(PARSER_SRCS) -o $@ $(LDFLAGS)

build/seeds/.done: build/fuzz_seeds $(wildcard fixtures/*.json)
	rm -rf build/seeds
	./build/fuzz_seeds fixtures build/seeds/espn build/seeds/set
	@touch $@

fuzz: build/fuzz_espn build/fuzz_set build/seeds/.done
	@echo "Fuzzing with $(if $(LIBFUZZER),libFuzzer,fuzz_main.cpp) ($(FUZZ_CXX))"
	@mkdir -p build/corpus/espn build/corpus/set
	./build/fuzz_espn $(FUZZ_RUN) -dict=fuzz_espn.dict build/corpus/espn build/seeds/espn
	./build/fuzz_set $(FUZZ_RUN) -dict=fuzz_set.dict build/corpus/set build/seeds/set

fuzz-long:
	$(MAKE) fuzz FUZZ_TIME=$(FUZZ_LONG_TIME)

clean:
	rm -rf build
.PHONY: all clean replay heap fuzz fuzz-long
