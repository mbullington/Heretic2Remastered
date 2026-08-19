# Untried hypotheses (remove when attempted)

## SEG-7 handoff-agenda dispositions (2026-08-18, baseline 15239169 total 206,764)
KEPT: E59.1 FTB BSP draw order (-15.9ms total / world -9.3; overwrote 14.6K->2.0K px/frame — handoff's "39 screenfuls" was a 32x counter misread, real overdraw was 1.25x; E59b HZB FALSIFIED by pool math ~1.5ms).
KEPT: E55c bit-exact opaque flex path (entities -8.2ms): vcolor per-vertex hoist + alpha-chain drop + colorkey test. s/t/r/g/b float chains untouched.
KEPT: E58 baked render packets (-5.0ms total): map-static vertex/base_s/t + light dims per face at load.
FALSIFIED: E60 lit-surface cache — gate red 4.54% (warp-class faces composite wrong) AND world +4.5ms: 565+pow2 modulate already cheap (Quake-era win came from 8-bit colormap math), 7 faces/frame flicker-rebuild, composite loop carries IMUL+4 clamp-branches; ceiling ~+2.5ms even perfected. DO NOT re-open without pow2-stride + flicker-bypass redesign.
SKIPPED (contract-illegal): E61 temporal reprojection — cannot be bit-exact vs full-render baselines (fidelity-ceiling class); user-gated turbo est -25..40ms.
WASH: E57 coverage-driven sky (40-tile span masks bit-exact): sky -3.7 == coverage tax +3.6 in world. Learning: sky floor 8.4ms at holes=0 is row-basis+block machinery, NOT classification/zb-walk; deadband irrelevant at holes=0.
WASH: E62 PVS decode-once + gen stamps: per-leaf RLE re-decode was small vs leaf struct loads; +45 lines for noise.
POOL-FALSIFIED: E63 (flex px only 11.3K/frame: tile-z pool ~1.5ms == build cost; self-occlusion needs per-px anyway), E64 (45% tris already bbox-culled pre-fill; B3-D: rejects free; multi-monster play only), E65/E66 (lights+particles = 3us each quiescent; combat-scene workload needed — new-workload gate).
USER-GATED TURBO BANK (fidelity-ceiling class, per step-64 precedent): rgb-affine flex variant A measured -12.1ms flex pool / frame 157.1 vs 160.7 bit-exact — GATE RED 210-211px (0.27%, 4.6x budget) on synthetic flex fixture; UV-affine measured bit-NEUTRAL (210 vs 211 same pixels => ceiling break is rgb chain physics: 565->888 quantum = 8/255 > 2LSB).
E61 SHIPPED DEFAULT-ON (e9b207fa) per user directive ('default n good, or killed'): all alternative
options REMOVED from the codebase/ledger. Recipients of the 'remove' list: segment-affine warp (was
fastest per-osp but 1D-inexact), 2D tile quad warp (slower per-px, dropped), overscan full-quality
guard band (measured LOSS: frame 187.6 > 158.9 OFF on moving camera; env infra deleted), dilation
fill (superseded by hole-tile repair), strict parity alternation and r_swtemporal=2 (folded into the
single motion scheduler). Final architecture: motion-gated scheduler (real if >3deg rot or >24 u
translation per frame; else warp, cap 3 fakes/real) + backward homography warp (T = M_s^T M_d,
rotation-exact at 8px block ends, world/flex fixed-point span conventions inside; destination-driven,
1px-exact map: no per-px z compare, holes can't persist) + refresh pass per fake frame: world walk
face-culled by (baked sphere center+radius vs 8x8 hole-tile mask) OR temporal_dynamic-baked class
(anim/flow/warp/undulate/quake/anim-chain/nonzero lightstyles re-render every frame regardless).
sw_world_repair_pass flag is the cull driver (empty mask MUST NOT degrade to 'no cull'; measured cost
of that bug: full 2nd world pass = 193ms). snapshot = world-only color+depth post-sky. Gate evidence:
RENDER+EFFECTS diff=0 with temporal DEFAULT ON; bench total 157,858 (-23.7% vs 206,764 seg-7 base),
world 78.5, fake-frame repair walked px ~1/3 of full. Backlog: zb-16 snapshot, warp block tune, combat
scene for E65/E66, radius-2 dilate only for >1px reveal cracks.

