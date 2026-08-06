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

## Startup array audit

The failing DebugDiag image had two `.init_array` words at
`__init_array_start..end`: the `crtbegin.o` `frame_dummy` entry and one entry
attributed by the map to the LTO-generated `ltrans0.ltrans.o`. The only input
object that contains a C++ static-initialization unit is
`Src/Audio/fx_reverb_revb.cpp.obj`, whose LTO metadata contains
`_GLOBAL__sub_I_fx_reverb_revb_global_init`. The `Src/UI/ui_core_mute.c.obj`
object contains no constructor or init-array section; its relocations are
ordinary calls and data references only. Therefore `0x080AEDF8` is not a
deliberate UI callback or table entry: it is the corrupted resolved address of
the LTO-generated constructor entry.

The linker `KEEP`/`SORT` rules were not collecting a data section or padding:
the map assigned exactly four bytes from `crtbegin.o` and four bytes from the
LTO temporary object. DebugDiag now compiles only this constructor-bearing TU
with `-O3 -fno-lto`; all other Release-like DebugDiag LTO and optimization
flags remain unchanged. Its constructor is consequently emitted as a normal
`.init_array` input with an explicit `R_ARM_ABS32` relocation to
`.text.startup._GLOBAL__sub_I_fx_reverb_revb_global_init`.

The final DebugDiag array is:

```text
__init_array_start = 0x08106868
__init_array_end   = 0x08106870
0x08106868 : 0x080002C5  (frame_dummy, Thumb)
0x0810686C : 0x080624B5  (_GLOBAL__sub_I_fx_reverb_revb_global_init, Thumb)
```

`Release_lowcost_clean` and `Premium` retain their normal LTO-generated
constructor entries and both have valid Thumb pointers. The startup path still
calls `__libc_init_array()` and then `main`; no constructor or Multi runtime
logic was removed.
