# The Modelita site

The pages at modelita.vercel.app: a home page, a tour, a cheatsheet and the
playground. They all run `c/modelita.h` compiled to WebAssembly, so every
figure is drawn from what the engine builds, exactly as every other
implementation builds it.

```sh
make serve       # then open http://localhost:8642
make check       # the engine against the suite; the pages' logic, figures and links
```

It is deployed to Vercel as a static site: `vercel.json` at the repository
root serves `web/site/` with no build step.

The look follows Byrne's Euclid (1847): white paper, black ink, and red,
yellow and blue that always mean a kind of face (`site.css` maps each role
to one). Pages are one narrow column of figures with a word under each.

**Figures.** Any `<canvas data-word="...">` on a page is drawn by
`figures.js`: it runs the program (lines joined by `&#10;`) and draws its
last shape. `data-focus="last"` colours only what the last step made;
`data-spin` turns it slowly. Any `<a data-play="...">` becomes a link that
opens its program in the playground. `make check` runs every one of them,
so a page can't show a word the language doesn't accept.

**Old links.** Shared programs used to live at the root (`/#z...`).
`forward.js` sends those on to `/play/#z...`.

## The playground

At `/play/`:

- **Editor**: highlighted in the site's colours (operators red, selectors blue, labels yellow, comments grey) and runs as you type. The gutter marks each line: `●` a shape,
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
| `site/index.html`, `tour/`, `cheatsheet/` | the home page, the tour and the cheatsheet (which prints on one A4 page) |
| `site/site.css` | the look: colour tokens, face colours by role, type, and each page's layout |
| `site/figures.js` | draws the figures on the pages and fills in the playground links |
| `site/forward.js` | sends old shared links (`/#...`) to the playground |
| `site/play/` | the playground: `index.html`, `style.css` and `app.js`, the editor, the view and the controls |
| `site/lang.js` | what the pages know about Modelita text: line kinds, tokens, steps, highlighting, links |
| `site/modelita.js`, `modelita.wasm` | the engine: `modelita_web.c` + `../c/modelita.h` built with Emscripten |
| `site/og-*.png`, `favicon.svg` | the link preview and the tab icon. The preview's name carries a fingerprint of its contents, so Discord, Slack and X fetch it again when it changes |
| `og.mjs`, `og.svg` | the link preview: `og.mjs` draws the tower from the engine into `og.svg`, and `make og` exports that to `site/og-<fingerprint>.png` and points every page at it (needs Inkscape and the Archivo and DM Mono fonts) |
| `modelita_web.c` | runs a program and returns the results as JSON |
| `serve.py` | `make serve`: the site on localhost, with `Cache-Control: no-cache` so a rebuilt engine is always fetched |
| `dump.mjs` | the conformance dump from the engine: `python3 tests/run.py -- node web/dump.mjs` |
| `check-page.mjs` | `lang.js` against the engine: every example, every step, link round trips; and every figure and playground link on every page |

The engine is checked in, so the page needs no toolchain. Rebuild it after
changing the C code: `make` with `emcc` installed, or `make container` to
use the official Emscripten image with podman.
