# guard_page

Port of `src/test/guard_page` into the new framework.

The plugin counts `STATUS_GUARD_PAGE_VIOLATION` exceptions through the callback
API and asserts after process exit that the target triggered exactly one such
first-chance exception and exited successfully.
