# The playground. site/ is exactly what gets deployed: the page, its
# script and styles, and the engine (modelita.h compiled to WebAssembly
# with Emscripten). The engine is checked in so deploys need no toolchain;
# rebuild it after changing the C code, and `make check` before deploying.
EMCC ?= emcc
EMSDK_IMAGE ?= docker.io/emscripten/emsdk:4.0.10
FLAGS = -std=c99 -O2 -ffp-contract=off \
        -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=Modelita -sDYNAMIC_EXECUTION=0 \
        -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=1gb -sENVIRONMENT=web,node \
        -sEXPORTED_FUNCTIONS=_modelita_run -sEXPORTED_RUNTIME_METHODS=ccall

all: site/modelita.js

site/modelita.js site/modelita.wasm: modelita_web.c ../c/modelita.h
	$(EMCC) $(FLAGS) -o site/modelita.js modelita_web.c

# Without emcc installed: build in the official Emscripten image.
container:
	podman run --rm -v "$(CURDIR)/..:/src:Z" -w /src/web $(EMSDK_IMAGE) make EMCC=emcc all

# The checked-in engine against the conformance suite, then the page's own logic.
check:
	cd .. && python3 tests/run.py -- node web/dump.mjs
	node check-page.mjs

# The page loads its engine as a module, which browsers refuse from file://.
serve:
	@echo "open http://localhost:8642"
	cd site && python3 -m http.server 8642 --bind 127.0.0.1

.PHONY: all container check serve
