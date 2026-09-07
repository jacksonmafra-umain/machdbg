# Stale software-breakpoint events

This test starts 64 threads on the same short `INT3` breakpoint and disables it after the first hit.

On x64, the restored instruction is a blocking `syscall`. Its internal single-step takes 250 ms, exceeding the old two-timeout lifetime of the recently-deleted-breakpoint cache while other breakpoint events are deferred by safe stepping.

The run and step variants fail if a deferred breakpoint event reaches x64dbg's generic exception handler or the debuggee's VEH. The x86 build exercises the same multithreaded deletion path with a fast instruction.
