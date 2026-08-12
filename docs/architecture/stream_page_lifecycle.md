# Stream page lifecycle

## States

Runtime streaming pages follow one explicit physical pipeline:

```text
FREE -> RESERVED -> LOADING -> READY
                         `----> FAILED
```

`RESERVED` is an allocated destination awaiting the service. `LOADING` means
that one I/O command has the final SDRAM destination. Such a page is neither
selectable by another command nor eligible for eviction or slot reuse.
`FAILED` is a failed physical attempt; it is never a logical need state.

## Transfer token

The `RESERVED -> LOADING` transition returns a load token containing key, page,
slot, page generation and registration epoch. Completion validates every field
before publishing a result. A stale completion therefore cannot publish into a
slot reused for another page or another registration of the same sample.

Only `sample_page_cache_finish_loading()` publishes runtime streaming pages as
`READY` or `FAILED`. The ready transition executes the memory barrier after
decoding has completed in the final SDRAM page.

## Cancellation

An in-flight transfer is cancelled logically by setting a flag on its page. The
physical operation may finish, but a successful late result is converted to a
failure or reclaimed instead of becoming ready. Key clearing does not recycle
an in-flight destination; it defers reclamation until the matching token
finishes.

The current synchronous service normally completes before a main-context
cancellation can interleave. The token and deferred-cancellation rules
establish the safety contract required by the later inter-core transport.

## Multi bulk loader

Bulk preparation leaves pages reserved. Each bounded plan batch transitions its
pages to loading, then sends each page through the common transport, backend,
decoder and token publication path. Multi READs are serialized by the common SD
scheduler; the loader has no separate bulk gate or block-device owner. It keeps
its existing start/loop guarantees.
