# USB Audio fatal traps for ASIO testing

`Error_Handler()` in `Board/LowCost/Generated/Src/main.c` disables interrupts,
executes `BKPT 0`, then loops forever. It neither resets nor resumes. Direct
callers do not automatically record their origin. The USB Audio traps use
`BRICK_FATAL_CONTEXT`, which writes `g_brick_fatal_record` before entering
`Error_Handler()`. Calls from IRQ do not persist to SD because
`crash_library_capture_and_persist()` deliberately returns when IPSR is not
zero. Read the RAM record with GDB after the breakpoint. Without a debugger,
the BKPT may escalate to a fault; this diagnostic firmware is intended to run
attached to GDB.

New fatal codes:

| Code | Condition | Record fields |
| --- | --- | --- |
| `0x5501` | OUT packet exceeds 392 bytes or is not a whole stereo PCM32 frame | `context`: 1=oversize, 2=partial frame; `requested`: bytes |
| `0x5502` | TinyUSB FIFO returns fewer bytes than the completed OUT packet | `requested`: expected bytes; `capacity`: returned bytes |
| `0x5503` | USB-to-AUDIO ring accepts fewer frames than supplied | `requested`: supplied frames; `capacity`: accepted frames |
| `0x5504` | AUDIO read fails after RUN was established | `context`: remaining ring fill; `requested`: needed frames; `capacity`: returned frames |
| `0x5505` | TinyUSB cannot rearm the active isochronous OUT endpoint after completion | `entity`: endpoint address; `context`: alternate setting; `requested`: endpoint size |

Zero-length OUT packets remain legal. All traps require a real active stream
or an OUT completion; none fires on a normal SOF or successful packet. Host
alt-setting changes, stream close, bus reset, role change, and device stop are
legal USB events, so they remain nonfatal. These traps test internal contract
failures and do not prove that the host-side glitch cannot occur for another
reason.

After `BKPT 0`, inspect the RAM record. The current Release/LTO build has no
source debug types, but the ELF retains `Error_Handler`, `brick_fatal_raise_at`,
and `g_brick_fatal_record`. At address `0x2400f54c` in this build, the record's
11 words are message pointer, file pointer, line, function pointer, code,
entity, context, requested, capacity, caller PC, and caller SP. In GDB:

```
x/11wx 0x2400f54c
x/s *(char **)0x2400f54c
x/s *(char **)0x2400f550
p/x *(unsigned int *)0x2400f55c
```

The last command reads the code. Addresses must be checked again after a
rebuild. Source functions may be inlined, so use the record fields rather
than the backtrace alone to identify a trap.
