# Sequencer page loops

`TRACK + PAGE 1–4` opens that physical page for editing, including pages beyond
the current LEN. A single tap leaves playback unchanged. While TRACK remains
held, pressing another PAGE before releasing the first creates a Page Loop;
further PAGE presses add pages. A double tap on one PAGE within 300 ms selects
that page alone. Existing TRACK shortcuts retain priority over this gesture.

Each track stores a four bit page mask. Zero means the legacy contiguous
`1..LEN` playback window. A nonzero mask plays selected physical pages in
ascending order. The engine traverses a compact logical sequence, then maps
the resolved logical step to a physical step before reading trigs, PLAY,
locks, and recording destinations. LEN is the number of selected pages times
16. DIV and the edit page are independent. Changing LEN directly restores
the contiguous window.

The immutable engine pattern captures the mask and length together. On a
pattern generation change, the engine normalizes the traversal phase and
resolves a new physical playhead before processing steps. Pattern and project
serialization store the mask in the upper nibble of the sequence direction
byte; older data has a zero upper nibble and keeps its original behavior.
