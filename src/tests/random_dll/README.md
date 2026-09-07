# random_dll

Port of `src/test/random_dll` as an automated regression test scaffold for issue #3599.

Intended coverage:

- breakpoint command text that starts with `$` and must be formatted before execution
- dynamically generated `bpdll` commands using the current DLL path
- `scriptcmd call onrandomdll` callback execution while the script is running
- the resulting DLL-breakpoint / DllMain break sequence from the dynamically created librarian breakpoints

Current status:

- scaffolded, but **not enabled in the default suite yet**
- the automated source breakpoint/command path still does not reproduce the manual README scenario deterministically in headless
- keep this folder as WIP for a later dedicated pass on issue #3599
