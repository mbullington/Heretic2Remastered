# Notices

This tree's DOS software renderer (`dos/ref_soft/`) was informed by reviewing
the following open-source projects. No code has been mechanically copied;
architectural ideas (span engines, surface caching, DOS video presentation)
were studied and re-implemented. Their licenses are acknowledged below.

## q2DOS — Quake II for MS-DOS

- Authors: Neozeed, [HCI]Mara'akate, sezero.
- Upstream: https://github.com/maraakate/q2dos
- Based on id Software's Quake II source release, which is licensed under the
  GNU General Public License version 2 (GPLv2). The full GPLv2 text is shipped
  in that project's `licenses/gpl.txt`.
- Studied for: DJGPP build flags, x86 assembly span loops (`ref_soft/block8.inc`,
  `ref_soft/block16.inc`), the Quake II surface cache (`ref_soft/r_surf.c`
  `D_CacheSurface` + `R_BuildLightMap`), span inner loop structure
  (`ref_soft/r_scan.c` `D_DrawSpans16`), and the direct-to-VGA-framebuffer
  presentation path (`dos/swimp_dos.c`).

## Hexen II: Hammer of Thyrion (uHexen2)

- Maintainer: Ozkan Sezer (sezero) and contributors.
- Upstream: https://github.com/sezero/uhexen2 · http://uhexen2.sourceforge.net/
- Licensed under the terms of the GNU General Public License as published by the
  Free Software Foundation, either version 2 of the License, or (at your option)
  any later version (GPL-2.0-or-later). The license text is shipped in that
  project's `docs/COPYING`.
- Studied for: the Quake-family software-renderer surface cache and edge
  rasterizer lineage (`engine/h2shared/r_surf.c`, `r_edge.c`, `d_edge.c`), and
  its DJGPP cross-build setup (`engine/hexen2/build_cross_dos.sh`,
  `engine/hexen2/dos/`).

Copies of the GNU General Public License version 2 are available from both
upstream repositories and from <https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>.
