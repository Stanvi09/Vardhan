# Vardhan - build file
#   make          -> app + tests
#   make test     -> run tests
# Windows: w64devkit / MSYS2 (raylib bundled). Linux: raylib bundled. macOS: brew install raylib

CC     ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wno-format-truncation
CFLAGS += -Iinclude -Ithird_party/cjson -D_POSIX_C_SOURCE=200809L

CORE = src/ds.c src/data.c src/profile.c src/risk.c src/advisor.c src/assistant.c src/backtest.c src/users.c src/portfolio.c src/alerts.c src/net.c third_party/cjson/cJSON.c
APP  = src/ui.c src/app.c src/scr_home.c src/scr_plan.c src/scr_ask.c src/scr_explore.c src/scr_friend.c src/scr_proof.c src/scr_hood.c src/scr_settings.c src/paper.c src/brand.c src/login.c src/scr_alerts.c src/report.c

ifeq ($(OS),Windows_NT)
  RLINC = -Ithird_party/raylib/include
  LIBS  = -Lthird_party/raylib/windows/lib -lraylib -lopengl32 -lgdi32 -lwinmm -lshell32 -lpthread -mwindows
  EXE   = .exe
  RES   = vardhan_res.o
else
  UNAME := $(shell uname -s)
  ifeq ($(UNAME),Darwin)
    BREW := $(shell brew --prefix 2>/dev/null)
    RLINC = -I$(BREW)/include
    LIBS  = -L$(BREW)/lib -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo -lpthread
  else
    RLINC = -Ithird_party/raylib/include
    LIBS  = -Lthird_party/raylib/linux/lib -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
  endif
endif

all: vardhan$(EXE) vardhan_tests$(EXE)

vardhan_res.o: assets/vardhan.rc assets/vardhan.ico
	windres assets/vardhan.rc -O coff -o $@

vardhan$(EXE): $(CORE) $(APP) include/*.h $(RES)
	$(CC) $(CFLAGS) $(RLINC) $(CORE) $(APP) $(RES) $(LIBS) -lm -o $@

vardhan_tests$(EXE): $(CORE) src/tests.c include/*.h
	$(CC) $(CFLAGS) $(CORE) src/tests.c -lpthread -lm -o $@

test: vardhan_tests$(EXE)
	./vardhan_tests$(EXE)

clean:
	rm -f vardhan vardhan_tests

.PHONY: all test clean
