# Every implementation against the conformance suite. Run before committing
# a change to the spec, and before deploying the playground.
check:
	$(MAKE) -C c modelita
	python3 tests/run.py -- c/modelita
	python3 tests/run.py -- racket racket/cli.rkt
	$(MAKE) -C web check

.PHONY: check
