# Stream EDF scheduler

## Selection rule

The scheduler is now a module independent from FatFs, page decoding and the SD
backend. It consumes active request-queue entries and returns an entry index plus
an explicit scheduling decision.

The stored consumption deadline remains absolute and immutable. Runtime priority
labels (`urgent`, `normal`, `prefetch`) no longer precede time in the selection
order. They remain available for admission, service-budget policy and tracing.

For each request the scheduler computes:

```text
dispatch_deadline = min(consume_deadline, created_at + max_wait)
```

The smallest dispatch deadline wins. Ties use the real consumption deadline,
then creation time, publication sequence and queue index. A continuous stream of
new urgent requests therefore cannot postpone an older request forever.

The default maximum wait is 24,000 audio frames (500 ms at 48 kHz) and is a
configuration value, not a hidden priority threshold. This value is deliberately
conservative for migration; material measurements and admission policy will set
the product value in later stages.

## Validation and stale entries

The scheduler deliberately knows nothing about cache slots. After selection the
manager validates key, page geometry, page generation and registration epoch. A
stale entry is removed and selection is retried without touching SD.

The Release trace records the real deadline, effective dispatch deadline, waited
audio frames and whether the starvation guard affected the decision. Owner and
source round-robin state has been removed from selection.

## Remaining work

This step changes ordering only. The service is still synchronous and driven by
the ordinary superloop, so a scheduling bound is not yet a service-time guarantee.
Cadence, I/O separation and bounded same-file grouping belong to later stages.
