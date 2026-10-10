# Modelita

A small language for building polyhedral shapes, one local step at a time.

```
k[top](0.8) storey(1.2)^3 x[+z]{tower} C
```

Read right to left: a cube (`C`), its top extruded (`x[+z]`) and labelled
`tower`, a storey added three times, and a tent on the top. Each operator
acts only on the faces its `[selector]` picks. Conway's polyhedron
operators (`d a t k g` …) are part of the language too, so `t I` is still
a football.

A word is plain text. Any implementation that follows the spec builds
exactly the same shape from it, so a word can be stored, sent and rebuilt
anywhere.

## What's here

| Path | What |
|---|---|
| [`SPEC.md`](SPEC.md) | the language, precisely enough to implement from |
| [`tests/`](tests/) | the conformance suite: programs and the dumps they must produce, plus a runner that works with any implementation |
| [`racket/`](racket/) | the reference implementation |

## Try it

```sh
racket racket/cli.rkt -e "k[top](0.9) x[+z](0.6) C"     # the dump of one word
racket racket/cli.rkt house.mdl                         # a program; export lines write .obj files
python3 tests/run.py -- racket racket/cli.rkt           # the conformance suite
```

## Writing an implementation

1. Read `SPEC.md`.
2. Make a command that runs a program file and prints the dump described in
   `tests/README.md`.
3. Run `python3 tests/run.py -- <your command>` until every case passes.

## Status

Draft 1 of the spec. Planned next:

- a single-header C implementation, so that any language can call it
  through bindings, and WASM can run it in the browser;
- a web playground where the word lives in the URL;
- a one-page cheatsheet.

The DrRacket `#lang` and the studio from `local-ops` will be moved over on
top of `racket/`.
