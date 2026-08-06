# Multi stream DebugDiag audit

## Configuration

`DebugDiag` is a LowCost Debug build with `BRICK_TEST_BUILD=OFF`,
`BRICK6_MULTI_STREAM_DIAG=ON`, and
`BRICK6_MULTI_STREAM_DIAG_RELEASE_CODEGEN=ON`.

The latter option removes the toolchain Debug `-Og` and applies the Release
generator expressions to the audio, sampler, and streamer sources: base
`-O1`, `-O3/-flto` on hot sources, `-flto` on the selected audio sources,
`-O0` on Storage/U8g2, and link-time `-flto`. CPU/FPU flags, the LowCost
linker, and the page/lock/pending/reader/voice pools are unchanged.

The effective commands were checked in
`build/DebugDiag/compile_commands.json`: `-g3` remains, but `-Og` and
`-DDEBUG` are absent. DebugDiag therefore preserves DWARF without restoring
the Debug code-generation mode.

## Image comparison

The diagnostic snapshot adds 400 bytes in SDRAM and 8 bytes of flags. Pool
addresses and sizes are unchanged. The diagnostic branches remain in the
image, so this is not functionally identical to Release; it is intended to
exercise the Release optimization and LTO decisions while retaining GDB
symbols.

Verified targets:

- `build/DebugDiag/BRICK6_CUBE.elf`
- `build/Debug/BRICK6_CUBE.elf`
- `build/Release/BRICK6_CUBE.elf`

The diagnostic capture and fault-capture functions are kept out of inline
and are externally visible for GDB.
