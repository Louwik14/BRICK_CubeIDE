# Stream request producers

## Boundary introduced in step 3

Classic and Multi still describe their active 3/6-page windows through
`sample_stream_active_desc_t`. The manager converts each missing logical page
into a pointer-free `sample_stream_request_contract_t` and publishes it to the
request queue. Publication does not open a file, seek, read, decode or change a
page to `IN_FLIGHT`.

The request queue owns the bounded pending storage. It is shared by Classic and
Multi and performs physical-page deduplication by `(domain, object, page)`. A
duplicate can tighten the absolute deadline and raise urgency, but it can never
postpone the existing deadline. The owner attached to the earliest requirement
is retained. Start, loop, current, neighbor and anticipation remain explicit
request metadata.

## Queue contract

The producer-facing payload is the fixed-width 40-byte contract established in
step 1. Cache geometry is supplied separately and remains local validation
metadata. The queue entry itself contains no voice pointer, reader pointer,
FatFs object or SDRAM destination pointer.

This is the monocore implementation of the future M7-to-M4 seam: replacing the
local publication call with a shared-memory transport will not change the wire
request. The queue remains consumed synchronously by the current manager until
the scheduler and I/O stages are separated in later steps.

## Ownership and cancellation

Owner kind, owner id and owner generation travel with every published request.
Existing deferred owner release paths continue to cancel or transfer ownership
without relying on voice object addresses. Page allocation, locks, start/loop
pins and the Multi bulk-loader contract are unchanged by this step.
