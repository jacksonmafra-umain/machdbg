# cmdline_init

Port of the old `src/test/cmdline` scenario into the new framework.

The debuggee captures both CRT argv and `CommandLineToArgvW(GetCommandLineW())`
results, exports them, and breaks in `wmain`. The test plugin then asserts that
`init ... , alpha beta` produced the expected two arguments.
