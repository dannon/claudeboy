# The board's renderer compiled to WebAssembly for the PWA. Output lands next to
# the static files so the agent can serve this directory as it stands.
SRC := $(wildcard ../src/core/*.cpp) ../src/web/main.cpp

claudeboy.mjs: $(SRC) $(wildcard ../src/core/*.h)
	em++ -std=c++17 -O2 -Wall -Wextra -I../src $(SRC) -o $@ \
		-sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web \
		-sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=0 \
		-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32

clean:
	rm -f claudeboy.mjs claudeboy.wasm

.PHONY: clean
