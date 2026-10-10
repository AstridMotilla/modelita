# Modelita Playground

A web page for writing Modelita programs and seeing their shapes. It runs
`c/modelita.h` compiled to WebAssembly, so it builds exactly what every
other implementation builds.

```sh
make serve       # then open http://localhost:8642
```

- **Editor**: runs as you type. The gutter marks each line: `●` a shape,
  `○` an operator, `=` a definition, `×` an error (listed below the editor;
  click one to jump to it).
- **View**: shows the word on the cursor's line. Drag to turn it, scroll or
  pinch to zoom, double-click to fit. Hover a face for its role, step and
  labels.
- **Steps**: the word's tokens as written. Click one to see the shape after
  it. The seed is on the right and runs first. An operator word (no seed)
  is shown on `x[+z] C`.
- **Colour by**: role, label or step. Hover a legend entry to pick out
  those faces.
- **Copy link**: the whole program travels in the link (deflated, then
  base64url, after `#`). **Copy OBJ** copies the shown shape, y up.

## Files

| File | What |
|---|---|
| `index.html` | the page |
| `modelita_web.c` | runs a program and returns the results as JSON |
| `modelita.js` | the engine: `modelita_web.c` + `../c/modelita.h` built with Emscripten, with the `.wasm` embedded |
| `dump.mjs` | the conformance dump from the WebAssembly build: `python3 tests/run.py -- node web/dump.mjs` |

`modelita.js` is checked in, so the page works without Emscripten.
Rebuild it after changing the C code: `make` with `emcc` installed, or
`make container` to use the official Emscripten image with podman.
