# Modelita in C

`modelita.h` is the whole language in one C99 header with no dependencies
beyond the standard library. It follows SPEC.md draft 1 and passes the
conformance suite.

```c
#define MODELITA_IMPLEMENTATION      /* in exactly one .c file */
#include "modelita.h"

mdl_error err;
mdl_shape *s = mdl_word("k[top](0.9) x[+z](0.6) C", &err);
if (!s) {
    printf("%s error: %s\n", mdl_status_name(err.status), err.message);
} else {
    const double *v = mdl_vertices(s);           /* x y z per vertex, z up */
    for (int i = 0; i < mdl_face_count(s); i++) {
        const int *c = mdl_face_corners(s, i);   /* counterclockwise from outside */
        /* mdl_face_size, mdl_face_role, mdl_face_step, mdl_face_label ... */
    }
    mdl_shape_free(s);
}
```

`mdl_run_program` runs a whole program (definitions, parameters, `use`),
calling back once for each statement that shows a word or fails.

## Building

```sh
make            # the modelita command: prints the dump of a program (tests/README.md)
make test       # the conformance suite against it
```

Compile with `-ffp-contract=off`, or your compiler may fuse multiplies and
adds and give slightly different numbers than other implementations.

## Notes for bindings

- The API is flat: opaque `mdl_shape *`, plain `int`, `double` and
  `const char *`. Every pointer a shape returns stays valid until
  `mdl_shape_free`.
- There is no global state. Calls on different threads don't interfere.
- Errors never escape as signals or exits: every failure comes back as an
  `mdl_error` with a category (`mdl_status_name`) and a message.
- Out of memory aborts the process.

## How it works

The file follows the spec's order: memory and maps, shapes, operators
(§5), seeds (§2), selectors (§7), reading programs (§3, §6, §8), and
evaluation. Each statement gets an arena that is freed when the statement
is done, and errors `longjmp` out to the statement, so nothing needs
cleaning up on the way.
