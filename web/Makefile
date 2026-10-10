# The playground's engine: modelita.h compiled to WebAssembly, in one .js
# file (the .wasm is embedded), built with Emscripten. Without emcc
# installed, `make container` builds it in the official Emscripten image.
EMCC ?= emcc
EMSDK_IMAGE ?= docker.io/emscripten/emsdk:4.0.10
FLAGS = -std=c99 -O2 -ffp-contract=off \
        -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=Modelita -sSINGLE_FILE=1 \
        -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=1gb -sENVIRONMENT=web,node \
        -sEXPORTED_FUNCTIONS=_modelita_run -sEXPORTED_RUNTIME_METHODS=ccall

all: modelita.js

modelita.js: modelita_web.c ../c/modelita.h
	$(EMCC) $(FLAGS) -o $@ modelita_web.c

container:
	podman run --rm -v "$(CURDIR)/..:/src:Z" -w /src/web $(EMSDK_IMAGE) make EMCC=emcc all

.PHONY: all container

# The page loads modelita.js as a module, which browsers refuse from file://.
serve: modelita.js
	@echo "open http://localhost:8642"
	python3 -m http.server 8642 --bind 127.0.0.1

.PHONY: serve
