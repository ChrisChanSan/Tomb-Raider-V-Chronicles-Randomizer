# TRForge — notice

The files under `src/` are copies of TRForge modules, verbatim but for three
comment passages left out of `trf_remaster/Trg.h` ([VERSION.txt](VERSION.txt)
names them), and are covered by the MIT licence in [LICENSE](LICENSE):

> MIT License
> Copyright (c) 2026 TRForge contributors

This is an excerpt of TRForge's own `NOTICE.md`, kept to the part that
applies to what was copied. TRForge itself bundles Dear ImGui, zlib and
stb_image under `external/`; **none of those directories was copied here**,
so their notices are not reproduced. This project carries its own copies of
zlib and Dear ImGui with their own licence files — see the repository's
[THIRD-PARTY.md](../../../../THIRD-PARTY.md).

TRForge's asset rule is also ours: **no game content in the repository.**
The `.TRG`, `.TRM`, `.TEX`, `.MAP` and `.DDS` files these readers open are
read at run time out of the player's own Remaster installation and are never
copied here, extracted here, or shipped in the release package.
