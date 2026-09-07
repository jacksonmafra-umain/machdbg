# issue3808

Regression test for x64dbg issue #3808.

Source layout:

```text
src/tests/issue3808/
  plugin.cpp
  target.cpp
  test.txt
```

Runtime layout after build:

```text
bin/x64/tests/
  issue3808.exe
  issue3808/
    issue3808.dp64
    test.txt
```

The plugin performs the assertions through `_plugin_testassert(...)` and the generic
`src/tests/run.py` runner drives the test through `headless.exe -testing`.
