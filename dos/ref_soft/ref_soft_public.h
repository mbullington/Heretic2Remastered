#pragma once

#include "q_Typedef.h"

typedef struct h2dos_soft_stats_s {
	unsigned frames_presented;
	unsigned ui_draws;
	unsigned world_faces;
	unsigned triangles;
	unsigned depth_writes;
	unsigned sprite_entities;
	unsigned dynamic_sprites;
	unsigned variable_sprites;
	unsigned line_sprites;
	unsigned brush_entities;
	unsigned particles;
	unsigned texture_particles;
	unsigned dynamic_lights;
	unsigned flex_models;
	unsigned flex_entities;
	unsigned flex_triangles;
	unsigned flex_shadow_entities;
	unsigned reference_updates;
	unsigned sky_pixels;
	unsigned sky_face_mask;
	unsigned sky_cache_hits;
	unsigned sky_cache_misses;
	unsigned animated_faces;
	unsigned flowing_faces;
	unsigned turbulent_faces;
	unsigned undulate_faces;
	unsigned quake_faces;
	unsigned surface_queries;
	unsigned m8_images;
	unsigned m32_images;
	unsigned missing_assets;
	// Benchmark instrumentation: cumulative microseconds per pass and work counters.
	unsigned long long bench_us_clear;
	unsigned long long bench_us_sky;
	unsigned long long bench_us_world;
	unsigned long long bench_us_entities;
	unsigned long long bench_us_particles;
	unsigned long long bench_us_lights;
	unsigned long long bench_us_blend;
	unsigned long long bench_us_present;
	unsigned long long bench_us_frame;
	unsigned bench_frames;
	unsigned bench_pixels_world;   // pixels written by the world pass
	unsigned bench_pixels_flex;    // pixels written by flexible-model triangles
	unsigned bench_pixels_sprite;  // pixels written by sprite triangles
	unsigned bench_pixels_other;   // walkers inside-triangle count not attributed elsewhere
	unsigned bench_walked_world;   // bounding-box pixels visited (waste accounting)
	unsigned bench_world_faces;    // draw_face() invocations
	unsigned bench_world_spans;    // rasterized rows
	unsigned bench_world_blocks;   // 16-px subdivided blocks
	unsigned bench_part_calls;     // draw_particle calls (both passes)
	unsigned bench_part_px_point;  // px visited in the point/disc path (accepted circle px)
	unsigned bench_part_px_tex;    // px visited in the textured-quad path (all)
	unsigned bench_part_px_add;    // of the above, additive-class px
	unsigned bench_part_lmcolor;   // particles taking the PFL_LM_COLOR light sample
	unsigned bench_world_lightbuild_px; // face-light miniature pixels rebuilt
	unsigned long long bench_us_world_setup;  // draw_face work before span emission
	unsigned long long bench_us_world_spans;  // span rasterization (draw_face_spans)
	unsigned long long bench_us_flex_swaprefs; // flex: swap-frame + reference bones
	unsigned long long bench_us_flex_tris;     // flex: triangle dispatch loop
	unsigned long long bench_us_flex_setup;    // flex: per-tri corner/proj setup
	unsigned long long bench_us_flex_dispatch; // flex: dispatch_flex_screen_triangle
	unsigned bench_flex_tris;                  // flex triangles dispatched
	unsigned bench_flex_tris_fast;             //   ... via dispatch (screen-space)
	unsigned bench_flex_tris_clip;             //   ... via draw_flex_triangle (clip path)
	unsigned bench_flex_tris_bbox;             // dispatch culled: offscreen bbox
	unsigned bench_flex_tris_behind;           // dispatch culled: all corners w<=0
	unsigned long long bench_us_ent_flex;   // entity pass: flex models
	unsigned long long bench_us_ent_sprite; // entity pass: sprites
	unsigned long long bench_us_ent_brush;  // entity pass: inline/bsp model faces
	unsigned bench_brush_faces;             // brush/inline faces walked
	unsigned bench_brush_culled;            // brush entities frustum-culled whole
	unsigned bench_flex_spans;              // flex span-scanline iterations
	unsigned bench_flex_blocks;             // flex span-block iterations
	unsigned bench_model_flex;              // flex models drawn (walked, not culled-early)
	unsigned bench_model_sprite;            // sprite models drawn
	unsigned bench_walked_flex;
	unsigned bench_walked_sprite;
	unsigned long long bench_flex_vertex_lerps; // vertex posing work units in the entity pass
	unsigned long long bench_us_flex_prologue;  // entity-pass time before triangle setup begins
	unsigned long long bench_zcollide_eq;       // attempted writes at exactly the same depth key
	unsigned long long bench_zcollide_near;     // attempted writes within +/-4 depth keys
	unsigned bench_flex_tris_wind;              // flex triangles culled by GL-equivalent winding
	unsigned bench_magenta_px;                  // total magenta framebuffer pixels across frames
	unsigned bench_magenta_max;                 // largest magenta pixel count in one frame
	unsigned bench_magenta_saved;               // MAGENTA.PPM capture written
	unsigned bench_mag_world;                   // magenta written by the opaque world span loop
	unsigned bench_mag_world_alpha;             // magenta written by the alpha world span loop
	unsigned bench_mag_flex;                    // magenta written by flex span loops
	unsigned bench_mag_putpixel;                // magenta written through SW_PutPixel (sprites/naive)
	unsigned bench_mag_frame;                   // first frame that contained magenta
	unsigned long long bench_mag_surface;       // magenta found in the converted present surface
	unsigned bench_holes_topband;               // depth==0 pixels in top 60 rows this frame
	unsigned bench_world_zaccepted;             // world span px that won the z-test (true coverage)
	unsigned bench_world_zreject;               // world span px REJECTED by the z test (E59 pool probe)
	unsigned bench_sky_holes;                   // px with zb==0 at sky time (sky-visible holes)
	unsigned bench_flex_tris_affine;            // flex triangles drawn by the opaque fast path
} h2dos_soft_stats_t;

const h2dos_soft_stats_t *H2DOS_SoftGetStats(void);
qboolean H2DOS_SoftSavePPM(const char *path);
qboolean H2DOS_SoftMalformedFixturesRejected(void);
unsigned H2DOS_SoftFramebufferChecksum(void);
