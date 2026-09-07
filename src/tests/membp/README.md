# memory breakpoint test fixture

This directory contains the shared executable and plugin harness for both documented `membp` behavior tests and `bpmrange` regression tests.

## Why

`SetMemoryBPX/membp` is documented to watch the whole memory region containing the supplied address, so the old report in `issue.txt` is not a valid `membp` bug by itself.

The same fixture is therefore reused for:

- positive `membp` region-behavior checks
- `bpmrange` exact-range install/hit/lifecycle regression tests
- access-type match/mismatch regression tests
- repeated-hit, single-shot, callback-mutation, and cross-page coverage

## Executable layout

`target.cpp` builds one executable/readable/writable merged PE section so code and data live in the same continuous region.

Exports used by the scripts:

- `ReadSequence`
- `ReadTwiceSequence`
- `WriteSequence`
- `ReadThenWriteSequence`
- `ExecSequence`
- `ExecLeaf`
- `ExecTwiceSequence`
- `ReadTarget`
- `WriteTarget`
- `WriteHeap`
- `StartHeap`
- `CrossPagePointer`
- `CrossPageReadSequence`
- `StartCrossPageRead`
- `CrossPageWriteSequence`
- `StartCrossPageWrite`
- `ReadTwoTargetsSequence`
- `WriteReadTargetSequence`
- `WriteTwoTargetsSequence`
- `AdjacentPointer`
- `AdjacentReadSequence`
- `StartAdjacentRead`
- `LargePointer`
- `LargeRangeSequence`
- `StartLargeRange`

A large padding block forces `ReadTarget` and `WriteTarget` onto a later page while keeping them in the same merged region as the code. Additional helpers cover repeated accesses, callback mutation scenarios, exact ranges that cross a page boundary, adjacent independent allocations, and a 32K-page range.

## Variant scripts

The directory now uses multiple scripts handled by `src/tests/run.py`:

- `test.txt` -> `membp`
- `test.write.txt` -> `membp/write`
- `test.range-read.txt` -> `membp/range-read`
- `test.range-write.txt` -> `membp/range-write`
- `test.range-execute.txt` -> `membp/range-execute`
- `test.range-read-ignore-write.txt` -> `membp/range-read-ignore-write`
- `test.range-read-ignore-execute.txt` -> `membp/range-read-ignore-execute`
- `test.range-write-ignore-read.txt` -> `membp/range-write-ignore-read`
- `test.range-write-ignore-execute.txt` -> `membp/range-write-ignore-execute`
- `test.range-execute-ignore-read.txt` -> `membp/range-execute-ignore-read`
- `test.range-execute-ignore-write.txt` -> `membp/range-execute-ignore-write`
- `test.range-access-read.txt` -> `membp/range-access-read`
- `test.range-access-write.txt` -> `membp/range-access-write`
- `test.range-access-execute.txt` -> `membp/range-access-execute`
- `test.range-repeat-read.txt` -> `membp/range-repeat-read`
- `test.range-repeat-execute.txt` -> `membp/range-repeat-execute`
- `test.range-singleshot-read.txt` -> `membp/range-singleshot-read`
- `test.range-singleshot-execute.txt` -> `membp/range-singleshot-execute`
- `test.range-delete.txt` -> `membp/range-delete`
- `test.range-reenable.txt` -> `membp/range-reenable`
- `test.range-reinit.txt` -> `membp/range-reinit`
- `test.range-callback-delete.txt` -> `membp/range-callback-delete`
- `test.range-callback-add.txt` -> `membp/range-callback-add`
- `test.range-same-page-delete-one.txt` -> `membp/range-same-page-delete-one`
- `test.range-same-page-same-type.txt` -> `membp/range-same-page-same-type`
- `test.range-same-page-mixed-ignore-read.txt` -> `membp/range-same-page-mixed-ignore-read`
- `test.range-same-page-mixed-ignore-write.txt` -> `membp/range-same-page-mixed-ignore-write`
- `test.range-shared-boundary-type-isolation.txt` -> `membp/range-shared-boundary-type-isolation`
- `test.range-cross-page-read.txt` -> `membp/range-cross-page-read`
- `test.range-cross-page-write.txt` -> `membp/range-cross-page-write`
- `test.range-adjacent-cross-page-read.txt` -> `membp/range-adjacent-cross-page-read`
- `test.range-three-page-read.txt` -> `membp/range-three-page-read`
- `test.range-adjacent-allocations.txt` -> `membp/range-adjacent-allocations`
- `test.range-large-32k-pages.txt` -> `membp/range-large-32k-pages`
- `test.range-multithread-read.txt` -> `membp/range-multithread-read`
- `test.range-stress-access.txt` -> `membp/range-stress-access`
- `test.exitprocess-assert.txt` -> `membp/exitprocess-assert`
- `test.range-heap.txt` -> `membp/range-heap`

The plugin in `plugin.cpp` provides shared assertions for installation metadata, hit sequences, deletion, re-enable, reinit, callback-driven mutations, exit-time verification, and start-mode based test staging for multi-step fixtures.
