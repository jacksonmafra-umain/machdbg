# cmdline_setcommandline

Port of the old `src/test/cmdline` scenario into a focused pre-entry
`setcommandline` regression test.

The plugin rewrites the command line at the system breakpoint, the target
captures the resulting argv/command line in `wmain`, and the plugin then
asserts the rewritten values.
