# Stream page lifecycle

## States

Runtime streaming pages follow one explicit pipeline:

```text
EMPTY -> QUEUED -> IN_FLIGHT -> READY
                         `----> ERROR
```

`IN_FLIGHT` means that an I/O command owns the final SDRAM destination. Such a
page is neither selectable by another request nor eligible for eviction or slot
reuse. The former ambiguous `LOADING` state no longer exists.

## Transfer token

The `QUEUED -> IN_FLIGHT` transition returns a load token containing key, page,
slot, page generation and registration epoch. Completion validates every field
before publishing a result. A stale completion therefore cannot publish into a
slot reused for another page or another registration of the same sample.

Only `sample_page_cache_finish_in_flight()` publishes runtime streaming pages as
`READY` or `ERROR`. The ready transition executes the existing memory barrier
after decoding has completed in the final SDRAM page.

## Cancellation

An in-flight transfer is cancelled logically by setting a flag on its page. The
physical operation may finish, but a successful late result is converted to an
error or reclaimed instead of becoming ready. Key clearing does not recycle an
in-flight destination; it defers reclamation until the matching token finishes.

The current synchronous service normally completes before a main-context
cancellation can interleave. The token and deferred-cancellation rules establish
the safety contract required by later asynchronous and inter-core stages.

## Multi bulk loader

Bulk preparation leaves pages queued. Each batch transitions its pages to
in-flight immediately before the physical read, and completes every token as
ready or error. The bulk loader remains exclusive on SD and keeps its existing
start/loop guarantees.
