# issue3803

Regression test for x64dbg issue #3803.

This test now covers the cleaned-up setter split:

- scalar assignment APIs reject raw whole-register destinations such as `_K0` and `_ZMM0` instead of treating integers as pointers;
- raw whole-register writes still work through `DbgValSetBuffer` with exact-size buffers;
- expression/command paths like `_K0=5`, `_ZMM0=5`, `mov _K0, 5`, and `mov _ZMM0, 5` fail cleanly instead of creating variables or crashing.

The assertions are implemented in `plugin.cpp`, which exercises both the command layer and the new bridge setter APIs on x64 and x86. The ZMM verification checks the low lane through `vmovdqu`, which keeps the test reliable on hosts without AVX-512 hardware where only the AVX subset is preserved.
