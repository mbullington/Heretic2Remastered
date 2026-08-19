#include "ref_soft_local.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FM_SKIN_NAME "skin"
#define FM_ST_NAME "st coord"
#define FM_TRI_NAME "tris"
#define FM_FRAME_NAME "frames"
#define FM_GLCMDS_NAME "glcmds"
#define FM_MESH_NAME "mesh nodes"
#define FM_SKELETON_NAME "skeleton"
#define FM_REFERENCES_NAME "references"
#define FM_SHORT_FRAME_NAME "short frames"
#define FM_NORMAL_NAME "normals"
#define FM_COMP_NAME "comp data"
#define FM_STANDARD_BLOCK_VERSION 1
#define FM_MESH_BLOCK_VERSION 3
#define FM_MESH_DISK_SIZE 516u
#define FM_DISK_BLOCK_HEADER_SIZE 40u

typedef struct flex_cursor_s {
	const byte *data;
	size_t remaining;
} flex_cursor_t;

typedef enum flex_block_e {
	FLEX_BLOCK_UNKNOWN,
	FLEX_BLOCK_HEADER,
	FLEX_BLOCK_SKIN,
	FLEX_BLOCK_ST,
	FLEX_BLOCK_TRI,
	FLEX_BLOCK_FRAME,
	FLEX_BLOCK_GLCMDS,
	FLEX_BLOCK_MESH,
	FLEX_BLOCK_SKELETON,
	FLEX_BLOCK_REFERENCES,
	FLEX_BLOCK_UNSUPPORTED,
	FLEX_BLOCK_COUNT
} flex_block_t;

typedef struct flex_block_view_s {
	const byte *payload;
	size_t size;
	int version;
	qboolean present;
} flex_block_view_t;

static qboolean checked_multiply(const size_t left, const size_t right, size_t *result)
{
	if (left != 0 && right > SIZE_MAX / left)
		return false;
	*result = left * right;
	return true;
}

static qboolean cursor_take(flex_cursor_t *cursor, const size_t bytes, const byte **output)
{
	if (bytes > cursor->remaining)
		return false;
	*output = cursor->data;
	cursor->data += bytes;
	cursor->remaining -= bytes;
	return true;
}

static qboolean read_i32(flex_cursor_t *cursor, int *value)
{
	const byte *source;
	if (!cursor_take(cursor, 4, &source))
		return false;
	memcpy(value, source, 4);
	*value = LittleLong(*value);
	return true;
}

static qboolean read_i16(flex_cursor_t *cursor, short *value)
{
	const byte *source;
	if (!cursor_take(cursor, 2, &source))
		return false;
	memcpy(value, source, 2);
	*value = LittleShort(*value);
	return true;
}

static qboolean read_float(flex_cursor_t *cursor, float *value)
{
	const byte *source;
	if (!cursor_take(cursor, 4, &source))
		return false;
	memcpy(value, source, 4);
	*value = LittleFloat(*value);
	return isfinite(*value);
}

static qboolean read_placement(flex_cursor_t *cursor, Placement_t *placement)
{
	for (int axis = 0; axis < 3; ++axis)
		if (!read_float(cursor, &placement->origin[axis])) return false;
	for (int axis = 0; axis < 3; ++axis)
		if (!read_float(cursor, &placement->direction[axis])) return false;
	for (int axis = 0; axis < 3; ++axis)
		if (!read_float(cursor, &placement->up[axis])) return false;
	return true;
}

static qboolean read_identifier(const byte ident[FMDL_BLOCK_IDENT_SIZE], char output[FMDL_BLOCK_IDENT_SIZE])
{
	const byte *terminator = memchr(ident, 0, FMDL_BLOCK_IDENT_SIZE);
	if (terminator == NULL)
		return false;
	const size_t length = (size_t)(terminator - ident);
	memcpy(output, ident, length);
	output[length] = '\0';
	return true;
}

static flex_block_t block_kind(const char *ident)
{
	if (Q_stricmp(ident, FM_HEADER_NAME) == 0) return FLEX_BLOCK_HEADER;
	if (Q_stricmp(ident, FM_SKIN_NAME) == 0) return FLEX_BLOCK_SKIN;
	if (Q_stricmp(ident, FM_ST_NAME) == 0) return FLEX_BLOCK_ST;
	if (Q_stricmp(ident, FM_TRI_NAME) == 0) return FLEX_BLOCK_TRI;
	if (Q_stricmp(ident, FM_FRAME_NAME) == 0) return FLEX_BLOCK_FRAME;
	if (Q_stricmp(ident, FM_GLCMDS_NAME) == 0) return FLEX_BLOCK_GLCMDS;
	if (Q_stricmp(ident, FM_MESH_NAME) == 0) return FLEX_BLOCK_MESH;
	if (Q_stricmp(ident, FM_SKELETON_NAME) == 0) return FLEX_BLOCK_SKELETON;
	if (Q_stricmp(ident, FM_REFERENCES_NAME) == 0) return FLEX_BLOCK_REFERENCES;
	if (Q_stricmp(ident, FM_SHORT_FRAME_NAME) == 0 || Q_stricmp(ident, FM_NORMAL_NAME) == 0 ||
		Q_stricmp(ident, FM_COMP_NAME) == 0)
		return FLEX_BLOCK_UNSUPPORTED;
	return FLEX_BLOCK_UNKNOWN;
}

static int expected_version(const flex_block_t kind)
{
	if (kind == FLEX_BLOCK_HEADER) return FM_HEADER_VER;
	if (kind == FLEX_BLOCK_MESH) return FM_MESH_BLOCK_VERSION;
	return FM_STANDARD_BLOCK_VERSION;
}

static qboolean scan_blocks(const byte *file, const int length, flex_block_view_t blocks[FLEX_BLOCK_COUNT])
{
	if (file == NULL || length < (int)FM_DISK_BLOCK_HEADER_SIZE)
		return false;
	flex_cursor_t stream = { file, (size_t)length };
	while (stream.remaining != 0)
	{
		const byte *raw_header;
		if (!cursor_take(&stream, FM_DISK_BLOCK_HEADER_SIZE, &raw_header))
			return false;
		char ident[FMDL_BLOCK_IDENT_SIZE];
		int version;
		int signed_size;
		memcpy(&version, raw_header + FMDL_BLOCK_IDENT_SIZE, 4);
		memcpy(&signed_size, raw_header + FMDL_BLOCK_IDENT_SIZE + 4, 4);
		version = LittleLong(version);
		signed_size = LittleLong(signed_size);
		if (!read_identifier(raw_header, ident) || signed_size < 0 || (size_t)signed_size > stream.remaining)
			return false;
		const byte *payload;
		if (!cursor_take(&stream, (size_t)signed_size, &payload))
			return false;
		const flex_block_t kind = block_kind(ident);
		if (kind == FLEX_BLOCK_UNKNOWN)
			continue;
		if (kind == FLEX_BLOCK_UNSUPPORTED || blocks[kind].present || version != expected_version(kind))
			return false;
		blocks[kind].payload = payload;
		blocks[kind].size = (size_t)signed_size;
		blocks[kind].version = version;
		blocks[kind].present = true;
	}
	return true;
}

static qboolean header_valid(const fmheader_t *header)
{
	size_t bytes;
	return header->skinwidth >= 1 && header->skinwidth <= 640 &&
		header->skinheight >= 1 && header->skinheight <= 480 &&
		header->num_skins >= 0 && header->num_skins <= SW_MAX_FLEX_SKINS &&
		header->num_xyz >= 1 && header->num_xyz <= SW_MAX_FLEX_VERTS &&
		header->num_st >= 1 && header->num_st <= SW_MAX_FLEX_ST &&
		header->num_tris >= 1 && header->num_tris <= SW_MAX_FLEX_TRIS &&
		header->num_glcmds >= 1 && header->num_glcmds <= SW_MAX_FLEX_GLCMDS &&
		header->num_frames >= 1 && header->num_frames <= SW_MAX_FLEX_FRAMES &&
		header->num_mesh_nodes >= 1 && header->num_mesh_nodes <= SW_MAX_FLEX_MESH_NODES &&
		header->num_xyz <= (INT_MAX - 40) / 4 && header->framesize == 40 + 4 * header->num_xyz &&
		checked_multiply((size_t)header->num_frames, (size_t)header->framesize, &bytes) &&
		checked_multiply((size_t)header->num_glcmds, 4, &bytes) &&
		checked_multiply((size_t)header->num_tris, 12, &bytes) &&
		checked_multiply((size_t)header->num_st, 4, &bytes);
}

static qboolean load_header(sw_flex_t *flex, const flex_block_view_t *block)
{
	if (!block->present || block->size != 40)
		return false;
	flex_cursor_t cursor = { block->payload, block->size };
	int *fields = &flex->header.skinwidth;
	for (int i = 0; i < 10; ++i)
		if (!read_i32(&cursor, &fields[i])) return false;
	flex->render_num_xyz = flex->header.num_xyz;
	return cursor.remaining == 0 && header_valid(&flex->header);
}

static qboolean load_skins(sw_flex_t *flex, const flex_block_view_t *block)
{
	size_t expected;
	if (!block->present || !checked_multiply((size_t)flex->header.num_skins, MAX_FRAMENAME, &expected) ||
		block->size != expected)
		return false;
	if (expected == 0)
		return true;
	flex->skin_names = malloc(expected);
	if (flex->skin_names == NULL)
		return false;
	memcpy(flex->skin_names, block->payload, expected);
	for (int i = 0; i < flex->header.num_skins; ++i)
	{
		char *name = flex->skin_names + (size_t)i * MAX_FRAMENAME;
		if (memchr(name, 0, MAX_FRAMENAME) == NULL)
			return false;
	}
	return true;
}

static qboolean load_st(sw_flex_t *flex, const flex_block_view_t *block)
{
	size_t expected;
	if (!block->present || !checked_multiply((size_t)flex->header.num_st, 4, &expected) || block->size != expected)
		return false;
	flex->st = calloc((size_t)flex->header.num_st, sizeof(*flex->st));
	if (flex->st == NULL)
		return false;
	flex_cursor_t cursor = { block->payload, block->size };
	for (int i = 0; i < flex->header.num_st; ++i)
		if (!read_i16(&cursor, &flex->st[i].s) || !read_i16(&cursor, &flex->st[i].t)) return false;
	return cursor.remaining == 0;
}

static qboolean load_triangles(sw_flex_t *flex, const flex_block_view_t *block)
{
	size_t expected;
	if (!block->present || !checked_multiply((size_t)flex->header.num_tris, 12, &expected) || block->size != expected)
		return false;
	flex->triangles = calloc((size_t)flex->header.num_tris, sizeof(*flex->triangles));
	if (flex->triangles == NULL)
		return false;
	flex_cursor_t cursor = { block->payload, block->size };
	for (int i = 0; i < flex->header.num_tris; ++i)
	{
		for (int corner = 0; corner < 3; ++corner)
			if (!read_i16(&cursor, &flex->triangles[i].xyz[corner]) || flex->triangles[i].xyz[corner] < 0 ||
				flex->triangles[i].xyz[corner] >= flex->render_num_xyz) return false;
		for (int corner = 0; corner < 3; ++corner)
			if (!read_i16(&cursor, &flex->triangles[i].st[corner]) || flex->triangles[i].st[corner] < 0 ||
				flex->triangles[i].st[corner] >= flex->header.num_st) return false;
	}
	return cursor.remaining == 0;
}

static qboolean load_frames(sw_flex_t *flex, const flex_block_view_t *block)
{
	size_t expected;
	if (!block->present || !checked_multiply((size_t)flex->header.num_frames, (size_t)flex->header.framesize, &expected) ||
		block->size != expected)
		return false;
	flex->frames = calloc((size_t)flex->header.num_frames, sizeof(*flex->frames));
	if (flex->frames == NULL)
		return false;
	size_t vertex_count;
	if (!checked_multiply((size_t)flex->header.num_frames, (size_t)flex->header.num_xyz, &vertex_count) ||
		vertex_count > SIZE_MAX / sizeof(*flex->frame_vertices))
		return false;
	flex->frame_vertices = malloc(vertex_count * sizeof(*flex->frame_vertices));
	if (flex->frame_vertices == NULL)
		return false;
	flex_cursor_t cursor = { block->payload, block->size };
	for (int frame_index = 0; frame_index < flex->header.num_frames; ++frame_index)
	{
		sw_flex_frame_t *frame = &flex->frames[frame_index];
		frame->vertices = flex->frame_vertices + (size_t)frame_index * flex->header.num_xyz;
		for (int axis = 0; axis < 3; ++axis)
			if (!read_float(&cursor, &frame->scale[axis])) return false;
		for (int axis = 0; axis < 3; ++axis)
			if (!read_float(&cursor, &frame->translate[axis])) return false;
		const byte *frame_name;
		if (!cursor_take(&cursor, sizeof(frame->name), &frame_name)) return false;
		memcpy(frame->name, frame_name, sizeof(frame->name));
		for (int vertex = 0; vertex < flex->header.num_xyz; ++vertex)
		{
			const byte *source;
			if (!cursor_take(&cursor, 4, &source)) return false;
			memcpy(&frame->vertices[vertex], source, 4);
		}
	}
	return cursor.remaining == 0;
}

static qboolean load_glcmds(sw_flex_t *flex, const flex_block_view_t *block)
{
	size_t expected;
	if (!block->present || !checked_multiply((size_t)flex->header.num_glcmds, 4, &expected) || block->size != expected)
		return false;
	flex->glcmds = malloc(expected);
	if (flex->glcmds == NULL)
		return false;
	flex_cursor_t cursor = { block->payload, block->size };
	for (int i = 0; i < flex->header.num_glcmds; ++i)
		if (!read_i32(&cursor, &flex->glcmds[i])) return false;
	return cursor.remaining == 0;
}

static qboolean load_meshes(sw_flex_t *flex, const flex_block_view_t *block)
{
	size_t expected;
	if (!block->present || !checked_multiply((size_t)flex->header.num_mesh_nodes, FM_MESH_DISK_SIZE, &expected) ||
		block->size != expected)
		return false;
	flex->meshes = calloc((size_t)flex->header.num_mesh_nodes, sizeof(*flex->meshes));
	if (flex->meshes == NULL)
		return false;
	for (int i = 0; i < flex->header.num_mesh_nodes; ++i)
	{
		flex_cursor_t cursor = { block->payload + (size_t)i * FM_MESH_DISK_SIZE + 512, 4 };
		short start;
		short count;
		if (!read_i16(&cursor, &start) || !read_i16(&cursor, &count) || start < 0 || count < 1)
			return false;
		flex->meshes[i].start_glcmds = start;
		flex->meshes[i].num_glcmds = count;
	}
	return true;
}

static qboolean load_skeleton(sw_flex_t *flex, const flex_block_view_t *block)
{
	static const int topology_counts[NUM_SKELETONS] = { 3, 1, 2, 2, 3, 3 };
	if (!block->present)
		return true;
	flex_cursor_t cursor = { block->payload, block->size };
	int exclusive[SW_MAX_FLEX_JOINTS] = { 0 };
	int total = 0;
	if (!read_i32(&cursor, &flex->skeletal_type) || flex->skeletal_type < 0 || flex->skeletal_type >= NUM_SKELETONS ||
		!read_i32(&cursor, &flex->num_clusters) || flex->num_clusters != topology_counts[flex->skeletal_type])
		return false;
	for (int cluster = flex->num_clusters - 1; cluster >= 0; --cluster)
	{
		if (!read_i32(&cursor, &exclusive[cluster]) || exclusive[cluster] < 0 ||
			exclusive[cluster] > flex->render_num_xyz - total)
			return false;
		total += exclusive[cluster];
		flex->cluster_counts[cluster] = total;
	}
	if (total < 1)
		return false;
	for (int cluster = 0; cluster < flex->num_clusters; ++cluster)
	{
		if (flex->cluster_counts[cluster] == 0)
			continue;
		flex->cluster_vertices[cluster] = malloc((size_t)flex->cluster_counts[cluster] * sizeof(int));
		if (flex->cluster_vertices[cluster] == NULL)
			return false;
	}
	byte *seen = calloc((size_t)flex->render_num_xyz, 1);
	if (seen == NULL)
		return false;
	int positions[SW_MAX_FLEX_JOINTS] = { 0 };
	qboolean valid = true;
	for (int cluster = flex->num_clusters - 1; cluster >= 0 && valid; --cluster)
	{
		for (int i = 0; i < exclusive[cluster]; ++i)
		{
			int vertex;
			if (!read_i32(&cursor, &vertex) || vertex < 0 || vertex >= flex->render_num_xyz || seen[vertex])
			{
				valid = false;
				break;
			}
			seen[vertex] = 1;
			for (int ancestor = 0; ancestor <= cluster; ++ancestor)
				flex->cluster_vertices[ancestor][positions[ancestor]++] = vertex;
		}
	}
	free(seen);
	int have_skeleton;
	if (!valid || !read_i32(&cursor, &have_skeleton) || have_skeleton != 1)
		return false;
	size_t placement_count;
	if (!checked_multiply((size_t)flex->header.num_frames, (size_t)flex->num_clusters, &placement_count) ||
		placement_count > SIZE_MAX / sizeof(Placement_t) || cursor.remaining != placement_count * 36u)
		return false;
	flex->skeleton_placements = calloc(placement_count, sizeof(*flex->skeleton_placements));
	if (flex->skeleton_placements == NULL)
		return false;
	for (size_t i = 0; i < placement_count; ++i)
		if (!read_placement(&cursor, &flex->skeleton_placements[i])) return false;
	return cursor.remaining == 0;
}

static qboolean load_references(sw_flex_t *flex, const flex_block_view_t *block)
{
	static const int reference_counts[NUM_REFERENCED] = { 7, 5, 6, 7 };
	if (!block->present)
		return true;
	flex_cursor_t cursor = { block->payload, block->size };
	int have_references;
	if (!read_i32(&cursor, &flex->reference_type) || flex->reference_type < 0 ||
		flex->reference_type >= NUM_REFERENCED || !read_i32(&cursor, &have_references) || have_references != 1)
		return false;
	flex->num_references = reference_counts[flex->reference_type];
	size_t count;
	if (!checked_multiply((size_t)flex->header.num_frames, (size_t)flex->num_references, &count) ||
		count > SIZE_MAX / sizeof(Placement_t) || cursor.remaining != count * 36u)
		return false;
	flex->references = calloc(count, sizeof(*flex->references));
	if (flex->references == NULL)
		return false;
	for (size_t i = 0; i < count; ++i)
		if (!read_placement(&cursor, &flex->references[i])) return false;
	return cursor.remaining == 0;
}

static qboolean glcmds_valid(const sw_flex_t *flex)
{
	for (int mesh_index = 0; mesh_index < flex->header.num_mesh_nodes; ++mesh_index)
	{
		const sw_flex_mesh_t *mesh = &flex->meshes[mesh_index];
		if (mesh->start_glcmds > flex->header.num_glcmds ||
			mesh->num_glcmds > flex->header.num_glcmds - mesh->start_glcmds)
			return false;
		int position = mesh->start_glcmds;
		const int end = position + mesh->num_glcmds;
		qboolean terminated = false;
		while (position < end)
		{
			const int signed_count = flex->glcmds[position++];
			if (signed_count == 0)
			{
				terminated = true;
				break;
			}
			if (signed_count == INT_MIN)
				return false;
			const int count = signed_count < 0 ? -signed_count : signed_count;
			if (count < 3 || count > (end - position) / 3)
				return false;
			for (int vertex = 0; vertex < count; ++vertex)
			{
				float s;
				float t;
				memcpy(&s, &flex->glcmds[position], 4);
				memcpy(&t, &flex->glcmds[position + 1], 4);
				if (!isfinite(s) || !isfinite(t) || flex->glcmds[position + 2] < 0 ||
					flex->glcmds[position + 2] >= flex->render_num_xyz)
					return false;
				position += 3;
			}
		}
		if (!terminated || position != end)
			return false;
	}
	return true;
}

static qboolean expand_flex_triangles(sw_flex_t *flex)
{
	int total_tris = 0;
	for (int m = 0; m < flex->header.num_mesh_nodes; ++m)
	{
		const sw_flex_mesh_t *mesh = &flex->meshes[m];
		int position = mesh->start_glcmds;
		const int end = position + mesh->num_glcmds;
		while (position < end)
		{
			const int signed_count = flex->glcmds[position++];
			if (signed_count == 0)
				break;
			const int count = signed_count < 0 ? -signed_count : signed_count;
			position += count * 3;
			total_tris += count - 2;
		}
	}
	if (total_tris == 0)
		return true;
	sw_flex_rtri_t *rtri = malloc((size_t)total_tris * sizeof(*rtri));
	if (rtri == NULL)
		return false;
	int out = 0;
	for (int m = 0; m < flex->header.num_mesh_nodes; ++m)
	{
		sw_flex_mesh_t *mesh = &flex->meshes[m];
		mesh->start_tri = out;
		int position = mesh->start_glcmds;
		const int end = position + mesh->num_glcmds;
		while (position < end)
		{
			const int signed_count = flex->glcmds[position++];
			if (signed_count == 0)
				break;
			const int count = signed_count < 0 ? -signed_count : signed_count;
			const int command_start = position;
			for (int triangle = 0; triangle < count - 2; ++triangle)
			{
				int command_vertices[3];
				if (signed_count < 0)
				{
					command_vertices[0] = 0;
					command_vertices[1] = triangle + 1;
					command_vertices[2] = triangle + 2;
				}
				else if (triangle & 1)
				{
					command_vertices[0] = triangle + 1;
					command_vertices[1] = triangle;
					command_vertices[2] = triangle + 2;
				}
				else
				{
					command_vertices[0] = triangle;
					command_vertices[1] = triangle + 1;
					command_vertices[2] = triangle + 2;
				}
				sw_flex_rtri_t *rt = &rtri[out++];
				for (int corner = 0; corner < 3; ++corner)
				{
					const int command = command_start + command_vertices[corner] * 3;
					memcpy(&rt->st[corner][0], &flex->glcmds[command], 4);
					memcpy(&rt->st[corner][1], &flex->glcmds[command + 1], 4);
					rt->vi[corner] = flex->glcmds[command + 2];
				}
			}
			position += count * 3;
		}
		mesh->num_tris = out - mesh->start_tri;
	}
	flex->rtri = rtri;
	return true;
}

void SW_FreeFlex(sw_flex_t *flex)
{
	if (flex == NULL)
		return;
	for (int cluster = 0; cluster < SW_MAX_FLEX_JOINTS; ++cluster)
		free(flex->cluster_vertices[cluster]);
	free(flex->rtri);
	free(flex->references);
	free(flex->skeleton_placements);
	free(flex->meshes);
	free(flex->glcmds);
	free(flex->frame_vertices);
	free(flex->frames);
	free(flex->triangles);
	free(flex->st);
	free(flex->skin_names);
	free(flex);
}

static sw_flex_t *parse_flex(const char *name, const byte *file, const int length, const qboolean register_skins)
{
	flex_block_view_t blocks[FLEX_BLOCK_COUNT];
	memset(blocks, 0, sizeof(blocks));
	if (!scan_blocks(file, length, blocks))
		goto rejected;
	if (!blocks[FLEX_BLOCK_HEADER].present || !blocks[FLEX_BLOCK_SKIN].present ||
		!blocks[FLEX_BLOCK_ST].present || !blocks[FLEX_BLOCK_TRI].present ||
		!blocks[FLEX_BLOCK_FRAME].present || !blocks[FLEX_BLOCK_GLCMDS].present ||
		!blocks[FLEX_BLOCK_MESH].present)
		goto rejected;

	sw_flex_t *flex = calloc(1, sizeof(*flex));
	if (flex == NULL)
		return NULL;
	flex->skeletal_type = SKEL_NULL;
	flex->reference_type = REF_NULL;
	if (!load_header(flex, &blocks[FLEX_BLOCK_HEADER]) ||
		!load_skins(flex, &blocks[FLEX_BLOCK_SKIN]) ||
		!load_st(flex, &blocks[FLEX_BLOCK_ST]) ||
		!load_triangles(flex, &blocks[FLEX_BLOCK_TRI]) ||
		!load_frames(flex, &blocks[FLEX_BLOCK_FRAME]) ||
		!load_glcmds(flex, &blocks[FLEX_BLOCK_GLCMDS]) ||
		!load_meshes(flex, &blocks[FLEX_BLOCK_MESH]) ||
		!load_skeleton(flex, &blocks[FLEX_BLOCK_SKELETON]) ||
		!load_references(flex, &blocks[FLEX_BLOCK_REFERENCES]) ||
		!glcmds_valid(flex) || !expand_flex_triangles(flex))
	{
		SW_FreeFlex(flex);
		goto rejected;
	}
	if (register_skins)
	{
		for (int i = 0; i < flex->header.num_skins; ++i)
			flex->skins[i] = SW_FindImage(flex->skin_names + (size_t)i * MAX_FRAMENAME, SW_IMAGE_SKIN);
		if (flex->header.num_skins == 0)
			flex->skins[0] = SW_FindImage("models/_missing_skin.m8", SW_IMAGE_SKIN);
	}
	return flex;

rejected:
	sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected malformed or unsupported FM '%s'\n", name);
	return NULL;
}

sw_flex_t *SW_LoadFlexFile(const char *name, const qboolean register_skins)
{
	void *file = NULL;
	const int length = sw_ri.FS_LoadFile(name, &file);
	sw_flex_t *flex = parse_flex(name, file, length, register_skins);
	if (file != NULL)
		sw_ri.FS_FreeFile(file);
	return flex;
}
