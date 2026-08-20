// DOS software renderer internals. Uses only repository-owned Heretic II formats.
#pragma once

#include "ref.h"
#include "qfiles.h"
#include "q_Sprite.h"
#include "ParticleFlags.h"
#include "FlexModel.h"
#include "Reference.h"
#include "Skeletons.h"
#include "ref_soft_public.h"

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>

#define SW_MAX_IMAGES 512
#define SW_MAX_MODELS 256
#define SW_MAX_FACE_VERTS 64
#define SW_NEAR_CLIP 1.0f
// 31-bit linear fixed-point inverse-z domain: all depth comparisons and the
// z-buffer store keys in this domain (key = w * SW_DEPTH_SCALE, w = 1/view_z).
// Larger = closer; 0 = far/sky. Linear so span-wise integer stepping preserves
// true depth ordering (float bits are monotonic but not affine).
#define SW_DEPTH_SCALE (2147480000.0f * SW_NEAR_CLIP)
#define SW_MAX_FLEX_VERTS 2048
#define SW_MAX_FLEX_TRIS 2048
#define SW_MAX_FLEX_FRAMES 2048
#define SW_MAX_FLEX_MESH_NODES 16
#define SW_MAX_FLEX_SKINS 64
#define SW_MAX_FLEX_ST 8192
#define SW_MAX_FLEX_GLCMDS 65535
#define SW_MAX_FLEX_JOINTS 3

typedef enum sw_image_kind_e {
	SW_IMAGE_WALL,
	SW_IMAGE_PIC,
	SW_IMAGE_SKIN,
	SW_IMAGE_SPRITE
} sw_image_kind_t;

typedef struct image_s {
	char name[MAX_QPATH];
	int width;
	int height;
	int num_frames;
	uint16_t *pixels;
	// E4 (ideas #4): for m8 SKIN images, the original index8 texels and the
	// image's packed-565 palette row - the intensity-shade CLUT's raw inputs.
	byte *src8;           // NULL for non-m8 and non-skin images
	uint16_t pal565[256];
	qboolean has_alpha;
	qboolean no_zero_texel;    // E-D: provably no 0x0000 texels => skip colorkey test
	qboolean synthetic_fallback;
	int registration_sequence;
} sw_image_t;

typedef struct sw_sprite_frame_s {
	int width;
	int height;
	int origin_x;
	int origin_y;
	sw_image_t *image;
} sw_sprite_frame_t;

// E58: per-face baked render packets. Everything draw_face() reconstructs
// per frame that is actually map-static: vertex indices/local coords, the
// base_s/base_t texture projections (same float products the runtime loop
// would compute, so bits are identical), and light-grid minima/dimensions.
// Dynamic pieces (undulate/warp/flowing offsets, transforms, clipping,
// animation-frame selection) stay in draw_face(). Faces failing static
// validation get verts == NULL and take the original runtime path.
typedef struct sw_face_packet_s {
	float light_min_s, light_min_t;
	int light_width, light_height;
	int num_verts;
	float center[3];  // local-space bounds: sphere center + radius
	float radius;
	byte temporal_dynamic; // animation/flow/warp/undulate/quake/nonzero lightstyles
	float *verts;     // num_verts * 5: local x, y, z, base_s, base_t
} sw_face_packet_t;

typedef struct sw_bsp_s {
	byte *file;
	int length;
	const dheader_t *header;
	const dplane_t *planes;
	int num_planes;
	const dvertex_t *vertices;
	int num_vertices;
	const dnode_t *nodes;
	int num_nodes;
	const texinfo_t *texinfo;
	int num_texinfo;
	const dface_t *faces;
	int num_faces;
	const byte *lighting;
	int lighting_length;
	const dleaf_t *leaves;
	int num_leaves;
	const ushort *leaffaces;
	int num_leaffaces;
	const dedge_t *edges;
	int num_edges;
	const int *surfedges;
	int num_surfedges;
	const dmodel_t *submodels;
	int num_submodels;
	const byte *visibility;
	int visibility_length;
	sw_image_t **texinfo_images;
	sw_face_packet_t *packets; // E58: per-face baked static geometry, NULL until baked
} sw_bsp_t;

typedef struct sw_flex_vertex_s {
	byte v[3];
	byte lightnormalindex;
} sw_flex_vertex_t;

typedef struct sw_flex_frame_s {
	float scale[3];
	float translate[3];
	char name[16];
	sw_flex_vertex_t *vertices;
} sw_flex_frame_t;

typedef struct sw_flex_st_s {
	short s;
	short t;
} sw_flex_st_t;

typedef struct sw_flex_triangle_s {
	short xyz[3];
	short st[3];
} sw_flex_triangle_t;

typedef struct sw_flex_mesh_s {
	int start_glcmds;
	int num_glcmds;
	int start_tri;
	int num_tris;
} sw_flex_mesh_t;

// Load-expanded per-triangle records: fan/strip glcmd blocks flattened to flat
// (vi, s, t) triplets once per model so the runtime tri loop reads contiguous
// records instead of re-walking command selection per triangle.
typedef struct sw_flex_rtri_s {
	int vi[3];
	float st[3][2];
} sw_flex_rtri_t;

typedef struct sw_flex_s {
	fmheader_t header;
	int render_num_xyz;
	char *skin_names;
	sw_image_t *skins[SW_MAX_FLEX_SKINS];
	sw_flex_st_t *st;
	sw_flex_triangle_t *triangles;
	sw_flex_frame_t *frames;
	sw_flex_vertex_t *frame_vertices;
	int *glcmds;
	sw_flex_rtri_t *rtri;
	sw_flex_mesh_t *meshes;
	int skeletal_type;
	int num_clusters;
	int cluster_counts[SW_MAX_FLEX_JOINTS];
	int *cluster_vertices[SW_MAX_FLEX_JOINTS];
	Placement_t *skeleton_placements;
	int reference_type;
	int num_references;
	Placement_t *references;
} sw_flex_t;

typedef enum sw_model_kind_e {
	SW_MODEL_NONE,
	SW_MODEL_BSP,
	SW_MODEL_INLINE,
	SW_MODEL_SPRITE,
	SW_MODEL_BOOK,
	SW_MODEL_FLEX
} sw_model_kind_t;

typedef struct model_s {
	char name[MAX_QPATH];
	sw_model_kind_t kind;
	int registration_sequence;
	sw_bsp_t *bsp;
	int first_face;
	int num_faces;
	vec3_t mins, maxs;
	int num_frames;
	sw_sprite_frame_t *sprite_frames;
	book_t *book;
	sw_image_t *book_images[MAX_FRAMES];
	sw_flex_t *flex;
} sw_model_t;

typedef struct sw_vertex_s {
	float x;
	float y;
	float z;
	float s;
	float t;
	float light_s;
	float light_t;
} sw_vertex_t;

extern refimport_t sw_ri;
extern uint16_t *sw_color;
extern uint32_t *sw_depth; // depth as non-negative float bits (ordered as int)
extern int sw_width;
extern int sw_scan_phase;
qboolean sw_scan2_active(void);
extern qboolean sw_blend_pending; // census F: fd->blend pulse ran this frame; UI writes follow
extern int sw_height;
extern refdef_t sw_refdef;
extern sw_model_t *sw_world_model;
extern qboolean sw_synthetic_fixture;
extern int sw_registration_sequence;
extern sw_image_t *sw_sky_images[6];
extern float sw_sky_rotate;
extern vec3_t sw_sky_axis;
extern unsigned sw_sky_generation;
extern h2dos_soft_stats_t sw_stats;
extern int sw_dbg_clear_cyan;
extern int sw_dbg_zcollide;
extern int sw_dbg_zviz;
extern int sw_dbg_zstrict;
extern int sw_dbg_skydiag;
extern int sw_dbg_nospan;
extern int sw_dbg_tag_x;
extern int sw_dbg_tag_y;
extern int sw_dbg_tag_face;
extern int sw_dbg_tag_src; // current painter class/face for tagxy attribution (>=0: world face_index)
void SW_TagProbe(const char *where);

qboolean SW_FrameBufferInit(void *window);
void SW_FrameBufferShutdown(void);
void SW_Clear(uint16_t color);
void SW_Present(void);
// B2: the internal framebuffer and all textures are RGB565. SW_RGB565 packs
// a pixel; transparent texels are encoded as 0x0000 (opaque pure black is
// nudged to 0x0001 at decode time).
uint16_t SW_RGB565(unsigned r, unsigned g, unsigned b);
void SW_PutPixel(int x, int y, float depth, uint16_t color, byte alpha, qboolean depth_write, qboolean additive);
void SW_BlendRect(int x, int y, int w, int h, uint16_t color, byte alpha);
qboolean SW_SavePPM(const char *path);

void SW_ImageInit(void);
void SW_ImageShutdown(void);
sw_image_t *SW_FindImage(const char *name, sw_image_kind_t kind);
void SW_FreeUnusedImages(void);
void SW_DrawImage(int x, int y, int w, int h, const sw_image_t *image, float alpha);
void SW_DrawGetPicSize(int *w, int *h, const char *name);
void SW_DrawPic(int x, int y, int scale, const char *name, float alpha);
void SW_DrawStretchPic(int x, int y, int w, int h, const char *name, float alpha, DrawStretchPicScaleMode_t mode);
void SW_DrawChar(int x, int y, int scale, int c, paletteRGBA_t color, qboolean shadow);
void SW_DrawTileClear(int x, int y, int w, int h, const char *name);
void SW_DrawFill(int x, int y, int w, int h, paletteRGBA_t color);
void SW_DrawFadeScreen(paletteRGBA_t color);
void SW_DrawBigFont(int x, int y, const char *text, float alpha);
int SW_BigFontLength(const char *text);
void SW_BookDrawPic(const char *name, float scale, float alpha);
void SW_DrawInitCinematic(int width, int height);
void SW_DrawCloseCinematic(void);
void SW_DrawCinematic(const byte *data, const paletteRGB_t *palette);
qboolean SW_TestFontFile(const char *name);

void SW_ModelInit(void);
void SW_ModelShutdown(void);
void SW_BeginRegistration(const char *map);
struct model_s *SW_RegisterModel(const char *name);
struct image_s *SW_RegisterSkin(const char *name, qboolean *loaded);
struct image_s *SW_RegisterPic(const char *name);
void SW_EndRegistration(void);
int SW_GetReferencedID(const struct model_s *model);
qboolean SW_TestBSPFile(const char *name);
qboolean SW_TestBookFile(const char *name);
int SW_FindSurface(const vec3_t start, const vec3_t end, struct Surface_s *surface);
sw_flex_t *SW_LoadFlexFile(const char *name, qboolean register_skins);
void SW_FreeFlex(sw_flex_t *flex);

int SW_RenderFrame(const refdef_t *fd);
void SW_RasterSky(const refdef_t *fd);
void SW_SkyCacheShutdown(void);
void SW_RasterWorld(const refdef_t *fd);
void SW_RasterEntities(const refdef_t *fd);
void SW_RasterParticles(const refdef_t *fd);
void SW_RasterLights(const refdef_t *fd);
void SW_ParticleTexturesInit(void);
void SW_ParticleTexturesShutdown(void);
void SW_RasterFlexEntity(const entity_t *entity, const sw_model_t *model, qboolean shadow_pass);
void SW_DrawName(const vec3_t origin, const char *name, paletteRGBA_t color);
