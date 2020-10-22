CC = clang
CFLAGS = -std=c99 -O3 -march=pentium4 -msse -mfpmath=sse -Wall -Wextra -pedantic-errors -DNDEBUG -DWINVER=0x0601 -D_WIN32_WINNT=0x0601
LDFLAGS = -s -Wl,--kill-at -shared -static-libgcc

all: bridge luabridge

bridge: obj/main.o obj/process.o obj/bridge.o obj/ods.o
	$(CC) obj/main.o obj/process.o obj/bridge.o obj/ods.o $(LDFLAGS) -lwinmm -o bin/bridge.auf
obj/main.o: src/main.c
	$(CC) $(CFLAGS) -o obj/main.o -c src/main.c
obj/bridge.o: src/bridge.c
	$(CC) $(CFLAGS) -o obj/bridge.o -c src/bridge.c
obj/process.o: src/process.c
	$(CC) $(CFLAGS) -o obj/process.o -c src/process.c
obj/ods.o: src/ods.c
	$(CC) $(CFLAGS) -o obj/ods.o -c src/ods.c

luabridge: obj/luamain.o
	$(CC) obj/luamain.o $(LDFLAGS) -llua5.1 -o bin/script/bridge.dll
obj/luamain.o: src/luamain.c
	$(CC) $(CFLAGS) -o obj/luamain.o -c src/luamain.c

src/bridge.c: src/ver.h
src/main.c: src/bridge_public.h src/aviutl.h src/aviutl_sdk/filter.h src/threads/threads.h src/stb_ds.h
src/luamain.c: src/bridge_public.h

.PHONY: clean
clean:
	rm -f obj/*.o
