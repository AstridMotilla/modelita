# Modelita Playground

A web page for writing Modelita programs and seeing their shapes. It runs
`c/modelita.h` compiled to WebAssembly, so it builds exactly what every
other implementation builds.

```sh
make serve       # then open http://localhost:8642
make check       # the engine against the suite, and the page's own logic
```

It is deployed to Vercel as a static site: `vercel.json` at the repository
root serves `web/site/` with no build step.

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
- **Share**: the whole program travels in the link (deflated, then
  base64url, after `#`, so it never reaches the server). Phones get their
  share sheet; elsewhere the link is copied. **Download OBJ** saves the
  shown shape, y up.

## Files

| File | What |
|---|---|
| `site/` | exactly what is deployed |
| `site/index.html`, `style.css`, `app.js` | the page: the editor, the view and the controls |
| `site/lang.js` | what the page knows about Modelita text: line kinds, tokens, steps, links |
| `site/modelita.js`, `modelita.wasm` | the engine: `modelita_web.c` + `../c/modelita.h` built with Emscripten |
| `site/og-*.png`, `favicon.svg` | the link preview and the tab icon. The preview's name carries a fingerprint of its contents, so Discord, Slack and X fetch it again when it changes |
| `og.svg` | the link preview's source: `make og` exports it to `site/og-<fingerprint>.png` and points the page at it (needs Inkscape and the Archivo and DM Mono fonts) |
| `modelita_web.c` | runs a program and returns the results as JSON |
| `dump.mjs` | the conformance dump from the engine: `python3 tests/run.py -- node web/dump.mjs` |
| `check-page.mjs` | `lang.js` against the engine: every example, every step, link round trips |

The engine is checked in, so the page needs no toolchain. Rebuild it after
changing the C code: `make` with `emcc` installed, or `make container` to
use the official Emscripten image with podman.
