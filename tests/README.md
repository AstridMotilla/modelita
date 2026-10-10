# Conformance suite

Each case is a Modelita program (`cases/NAME.mdl`) and the dump an
implementation must print for it (`cases/NAME.out`).

```sh
python3 tests/run.py -- racket racket/cli.rkt      # the reference implementation
python3 tests/run.py -k bridge -- ./my-modelita    # yours, only the bridge cases
```

`run.py` needs only Python 3. It runs the command with the case's path as
its last argument and compares the output with the `.out` file, token by
token. Two tokens that are both numbers match when they differ by at most
1e-9 (relative to their size, once that is above 1). Every other token must
match exactly, so implementations may write numbers however they like:
`1`, `1.0` and `1e0` are all fine.

## The dump

For every statement that shows a word (a plain word, or `export`) and every
statement that fails, in order, one block:

```
line 1                      the statement's line number, from 1
shape                       the word is a shape
vertices 12
v -1 -1 -1                  one line per vertex, in order
...
faces 10
f side 1 roof,wall : 1 5 9 8    role, step, labels (sorted, comma-separated, or - for none), ':', corners
...
skipped 0                   how many picked elements the steps skipped (SPEC.md §9)
end
```

A word that is an operator:

```
line 3
operator
end
```

A failed statement:

```
line 4
error range                 the category from SPEC.md §9
end
```

Successful definitions, `use`, `gallery`, blank lines and comments print
nothing. Error messages are not compared: print them to stderr.

## Adding cases

Write `cases/NAME.mdl`, then generate its `.out` with the reference
implementation and **read it** before committing. The reference can be
wrong, and an expected output is only worth what someone checked:

```sh
python3 tests/run.py --write -k NAME -- racket racket/cli.rkt
```

`--write` overwrites the `.out` of every case it runs, so always use it
with `-k`.
