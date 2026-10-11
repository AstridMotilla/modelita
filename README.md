# Modelita

A little language for shapes.

**Try it:** [modelita.vercel.app](https://modelita.vercel.app): a tour, a
cheatsheet and a playground, all running the C implementation as WebAssembly.

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
| [`c/`](c/) | the whole language in one C99 header, `modelita.h`, plus a command-line tool |
| [`web/`](web/) | the site: a tour, a cheatsheet, and a playground with an editor and 3D view, all running the C header as WebAssembly |

## Try it

```sh
make -C c                                        # build the C version
c/modelita -e "k[top](0.9) x[+z](0.6) C"         # the dump of one word
c/modelita examples/demo.mdl                     # a program; export lines write .obj files
python3 tests/run.py -- c/modelita               # the conformance suite
python3 tests/run.py -- racket racket/cli.rkt    # ... against the Racket reference
make -C web serve                                # the site, at http://localhost:8642
make check                                       # every implementation against the suite
```

## Writing an implementation

1. Read `SPEC.md`.
2. Make a command that runs a program file and prints the dump described in
   `tests/README.md`.
3. Run `python3 tests/run.py -- <your command>` until every case passes.

## Status

Draft 1 of the spec, with two implementations (Racket and C) that pass the
same suite, and the C one also passes it compiled to WebAssembly. They also agree on thousands of randomly generated words.
Planned next:

- bindings for other languages, starting with Python.

The DrRacket `#lang` and the studio from `local-ops` will be moved over on
top of `racket/`.

## License

MIT. See [LICENSE](LICENSE).
