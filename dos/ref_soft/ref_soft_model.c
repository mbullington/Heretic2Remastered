#include "ref_soft_local.h"
#include "Reference.h"
#include "q_Surface.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef H2_DOS
#define BSP_REJECT_CODE() (sw_ri.Con_Printf(PRINT_ALL, "ref_soft: BSP semantic failure at ref_soft_model.c line %d\n", __LINE__), false)
#else
#define BSP_REJECT_CODE() false
#endif

static sw_model_t sw_models[SW_MAX_MODELS];
static int sw_num_models;
static sw_bsp_t *sw_world_bsp;

static qboolean lump_valid(const dheader_t *header, const int file_length, const int lump_index,
	const size_t element_size, const qboolean allow_empty)
{
	const int offset = LittleLong(header->lumps[lump_index].fileofs);
	const int length = LittleLong(header->lumps[lump_index].filelen);
	if (offset < 0 || length < 0 || offset > file_length || length > file_length - offset)
		return false;
	if (!allow_empty && length == 0)
		return false;
	return element_size == 0 || (size_t)length % element_size == 0;
}

static const void *lump_data(const sw_bsp_t *bsp, const int index)
{
	return bsp->file + LittleLong(bsp->header->lumps[index].fileofs);
}

static int lump_count(const sw_bsp_t *bsp, const int index, const size_t element_size)
{
	return LittleLong(bsp->header->lumps[index].filelen) / (int)element_size;
}

static int little_ushort(const ushort value)
{
	return (int)(ushort)LittleShort((short)value);
}

static qboolean range_contains(const int first, const int count, const int limit)
{
	return first >= 0 && count >= 0 && first <= limit && count <= limit - first;
}

static qboolean child_valid(const int child, const int num_nodes, const int num_leaves)
{
	if (child >= 0)
		return child < num_nodes;
	if (child == INT_MIN)
		return false;
	return -1 - child < num_leaves;
}

static qboolean vector_finite(const float *values, const int count)
{
	for (int i = 0; i < count; ++i)
		if (!isfinite(LittleFloat(values[i])))
			return false;
	return true;
}

static qboolean texture_animation_chains_valid(const sw_bsp_t *bsp)
{
	for (int root = 0; root < bsp->num_texinfo; ++root)
	{
		int current = root;
		for (int steps = 0; steps < bsp->num_texinfo; ++steps)
		{
			const int next = LittleLong(bsp->texinfo[current].nexttexinfo);
			if (next == -1 || next == root)
				break;
			if (next < 0 || next >= bsp->num_texinfo)
				return false;
			current = next;
			if (steps + 1 == bsp->num_texinfo)
				return false;
		}
	}
	return true;
}

static qboolean bsp_node_graph_valid(const sw_bsp_t *bsp)
{
	typedef struct node_visit_s {
		int node;
		int next_child;
	} node_visit_t;

	byte *state = calloc((size_t)bsp->num_nodes, 1);
	node_visit_t *stack = malloc((size_t)bsp->num_nodes * sizeof(*stack));
	if (state == NULL || stack == NULL)
	{
		free(state);
		free(stack);
		return false;
	}

	for (int root = 0; root < bsp->num_nodes; ++root)
	{
		if (state[root] != 0)
			continue;
		int depth = 1;
		state[root] = 1;
		stack[0] = (node_visit_t){ .node = root, .next_child = 0 };
		while (depth > 0)
		{
			node_visit_t *visit = &stack[depth - 1];
			if (visit->next_child == 2)
			{
				state[visit->node] = 2;
				--depth;
				continue;
			}
			const int child = LittleLong(bsp->nodes[visit->node].children[visit->next_child++]);
			if (child < 0 || state[child] == 2)
				continue;
			if (state[child] == 1)
			{
				free(state);
				free(stack);
				return false;
			}
			state[child] = 1;
			stack[depth++] = (node_visit_t){ .node = child, .next_child = 0 };
		}
	}

	free(state);
	free(stack);
	return true;
}

static qboolean visibility_row_valid(const byte *visibility, const int length, int offset,
	const int minimum_offset, const int row)
{
	if (offset < minimum_offset)
		return false;
	int output = 0;
	while (output < row)
	{
		if (offset >= length)
			return false;
		if (visibility[offset++] != 0)
		{
			++output;
			continue;
		}
		if (offset >= length || visibility[offset] == 0 || visibility[offset] > row - output)
			return false;
		output += visibility[offset++];
	}
	return true;
}

static qboolean face_lightmap_range_valid(const sw_bsp_t *bsp, const dface_t *face)
{
	const int light_offset = LittleLong(face->lightofs);
	if (light_offset == -1)
		return true;

	const int first = LittleLong(face->firstedge);
	const int count = LittleShort(face->numedges);
	const texinfo_t *texture = &bsp->texinfo[LittleShort(face->texinfo)];
	float minimum_s = FLT_MAX;
	float minimum_t = FLT_MAX;
	float maximum_s = -FLT_MAX;
	float maximum_t = -FLT_MAX;
	for (int i = 0; i < count; ++i)
	{
		const int surface_edge = LittleLong(bsp->surfedges[first + i]);
		const int edge_index = abs(surface_edge);
		const int vertex_index = little_ushort(bsp->edges[edge_index].v[surface_edge >= 0 ? 0 : 1]);
		const dvertex_t *vertex = &bsp->vertices[vertex_index];
		const float s = LittleFloat(vertex->point[0]) * LittleFloat(texture->vecs[0][0]) +
			LittleFloat(vertex->point[1]) * LittleFloat(texture->vecs[0][1]) +
			LittleFloat(vertex->point[2]) * LittleFloat(texture->vecs[0][2]) + LittleFloat(texture->vecs[0][3]);
		const float t = LittleFloat(vertex->point[0]) * LittleFloat(texture->vecs[1][0]) +
			LittleFloat(vertex->point[1]) * LittleFloat(texture->vecs[1][1]) +
			LittleFloat(vertex->point[2]) * LittleFloat(texture->vecs[1][2]) + LittleFloat(texture->vecs[1][3]);
		if (!isfinite(s) || !isfinite(t))
			return false;
		minimum_s = min(minimum_s, s);
		minimum_t = min(minimum_t, t);
		maximum_s = max(maximum_s, s);
		maximum_t = max(maximum_t, t);
	}

	const float width_value = ceilf(maximum_s / 16.0f) - floorf(minimum_s / 16.0f) + 1.0f;
	const float height_value = ceilf(maximum_t / 16.0f) - floorf(minimum_t / 16.0f) + 1.0f;
	if (!isfinite(width_value) || !isfinite(height_value) || width_value < 1.0f || height_value < 1.0f ||
		width_value > 4096.0f || height_value > 4096.0f)
		return false;
	int styles = 0;
	while (styles < MAXLIGHTMAPS && face->styles[styles] != 255)
		++styles;
	if (styles == 0 || light_offset < 0)
		return false;
	const size_t width = (size_t)width_value;
	const size_t height = (size_t)height_value;
	if (width > SIZE_MAX / height || width * height > SIZE_MAX / 3u ||
		width * height * 3u > SIZE_MAX / (size_t)styles)
		return false;
	const size_t bytes = width * height * 3u * (size_t)styles;
	return (size_t)light_offset <= (size_t)bsp->lighting_length &&
		bytes <= (size_t)bsp->lighting_length - (size_t)light_offset;
}

static qboolean bsp_semantics_valid(const sw_bsp_t *bsp)
{
	const int entities_length = LittleLong(bsp->header->lumps[LUMP_ENTITIES].filelen);
	const int num_leafbrushes = lump_count(bsp, LUMP_LEAFBRUSHES, sizeof(ushort));
	const int num_brushes = lump_count(bsp, LUMP_BRUSHES, sizeof(dbrush_t));
	const int num_brushsides = lump_count(bsp, LUMP_BRUSHSIDES, sizeof(dbrushside_t));
	const int num_areas = lump_count(bsp, LUMP_AREAS, sizeof(darea_t));
	const int num_areaportals = lump_count(bsp, LUMP_AREAPORTALS, sizeof(dareaportal_t));
	const ushort *leafbrushes = lump_data(bsp, LUMP_LEAFBRUSHES);
	const dbrush_t *brushes = lump_data(bsp, LUMP_BRUSHES);
	const dbrushside_t *brushsides = lump_data(bsp, LUMP_BRUSHSIDES);
	const darea_t *areas = lump_data(bsp, LUMP_AREAS);
	const dareaportal_t *areaportals = lump_data(bsp, LUMP_AREAPORTALS);
	int visibility_clusters = 0;

	if (entities_length > MAX_MAP_ENTSTRING || bsp->num_planes > MAX_MAP_PLANES ||
		bsp->num_vertices > MAX_MAP_VERTS || bsp->num_nodes > MAX_MAP_NODES ||
		bsp->num_texinfo > MAX_MAP_TEXINFO || bsp->num_faces > MAX_MAP_FACES ||
		bsp->lighting_length > MAX_MAP_LIGHTING || bsp->num_leaves > MAX_MAP_LEAFS ||
		bsp->num_leaffaces > MAX_MAP_LEAFFACES || num_leafbrushes > MAX_MAP_LEAFBRUSHES ||
		bsp->num_edges > MAX_MAP_EDGES || bsp->num_surfedges > MAX_MAP_SURFEDGES ||
		bsp->num_submodels > MAX_MAP_MODELS || num_brushes > MAX_MAP_BRUSHES ||
		num_brushsides > MAX_MAP_BRUSHSIDES || num_areas > MAX_MAP_AREAS ||
		num_areaportals > MAX_MAP_AREAPORTALS || bsp->visibility_length > MAX_MAP_VISIBILITY)
		return BSP_REJECT_CODE();

	if (bsp->visibility_length != 0)
	{
		if (bsp->visibility_length < 4)
			return BSP_REJECT_CODE();
		memcpy(&visibility_clusters, bsp->visibility, sizeof(visibility_clusters));
		visibility_clusters = LittleLong(visibility_clusters);
		if (visibility_clusters < 1 || visibility_clusters > MAX_MAP_LEAFS ||
			visibility_clusters > (bsp->visibility_length - 4) / 8)
			return BSP_REJECT_CODE();
		const int visibility_header_length = 4 + visibility_clusters * 8;
		for (int cluster = 0; cluster < visibility_clusters; ++cluster)
		{
			for (int kind = 0; kind < 2; ++kind)
			{
				int offset;
				memcpy(&offset, bsp->visibility + 4 + (cluster * 2 + kind) * 4, sizeof(offset));
				offset = LittleLong(offset);
				if (!visibility_row_valid(bsp->visibility, bsp->visibility_length, offset,
					visibility_header_length, (visibility_clusters + 7) / 8))
					return BSP_REJECT_CODE();
			}
		}
	}

	for (int i = 0; i < bsp->num_planes; ++i)
		if (!vector_finite(bsp->planes[i].normal, 3) || !isfinite(LittleFloat(bsp->planes[i].dist)) ||
			LittleLong(bsp->planes[i].type) < PLANE_X || LittleLong(bsp->planes[i].type) > PLANE_ANYZ)
			return BSP_REJECT_CODE();
	for (int i = 0; i < bsp->num_vertices; ++i)
		if (!vector_finite(bsp->vertices[i].point, 3))
			return BSP_REJECT_CODE();
	for (int i = 0; i < bsp->num_nodes; ++i)
	{
		const dnode_t *node = &bsp->nodes[i];
		const int first = little_ushort(node->firstface);
		const int count = little_ushort(node->numfaces);
		if (LittleLong(node->planenum) < 0 || LittleLong(node->planenum) >= bsp->num_planes ||
			!child_valid(LittleLong(node->children[0]), bsp->num_nodes, bsp->num_leaves) ||
			!child_valid(LittleLong(node->children[1]), bsp->num_nodes, bsp->num_leaves) ||
			!range_contains(first, count, bsp->num_faces))
			return BSP_REJECT_CODE();
		for (int axis = 0; axis < 3; ++axis)
			if (LittleShort(node->mins[axis]) > LittleShort(node->maxs[axis]))
				return BSP_REJECT_CODE();
	}
	if (!bsp_node_graph_valid(bsp))
		return BSP_REJECT_CODE();
	for (int i = 0; i < bsp->num_texinfo; ++i)
	{
		const int next = LittleLong(bsp->texinfo[i].nexttexinfo);
		if (!vector_finite(&bsp->texinfo[i].vecs[0][0], 8) || (next != -1 && (next < 0 || next >= bsp->num_texinfo)))
			return BSP_REJECT_CODE();
	}
	if (!texture_animation_chains_valid(bsp))
		return BSP_REJECT_CODE();
	for (int i = 0; i < bsp->num_edges; ++i)
		if (little_ushort(bsp->edges[i].v[0]) >= bsp->num_vertices || little_ushort(bsp->edges[i].v[1]) >= bsp->num_vertices)
			return BSP_REJECT_CODE();
	for (int i = 0; i < bsp->num_surfedges; ++i)
	{
		const int edge = LittleLong(bsp->surfedges[i]);
		if (edge == INT_MIN || abs(edge) >= bsp->num_edges)
			return BSP_REJECT_CODE();
	}
	for (int i = 0; i < bsp->num_faces; ++i)
	{
		const dface_t *face = &bsp->faces[i];
		const int first = LittleLong(face->firstedge);
		const int count = LittleShort(face->numedges);
		const int texinfo = LittleShort(face->texinfo);
		const int light_offset = LittleLong(face->lightofs);
		if (little_ushort(face->planenum) >= bsp->num_planes ||
			(LittleShort(face->side) != 0 && LittleShort(face->side) != 1) ||
			count < 3 || count > SW_MAX_FACE_VERTS || !range_contains(first, count, bsp->num_surfedges) ||
			texinfo < 0 || texinfo >= bsp->num_texinfo ||
			(light_offset != -1 && (light_offset < 0 || light_offset >= bsp->lighting_length)) ||
			!face_lightmap_range_valid(bsp, face))
			return BSP_REJECT_CODE();
	}
	for (int i = 0; i < bsp->num_leaves; ++i)
	{
		const dleaf_t *leaf = &bsp->leaves[i];
		const int cluster = LittleShort(leaf->cluster);
		const int area = LittleShort(leaf->area);
		if (cluster < -1 || (cluster >= 0 && (bsp->visibility_length == 0 || cluster >= visibility_clusters)) ||
			area < 0 || area >= num_areas || area >= MAX_MAP_AREAS ||
			!range_contains(little_ushort(leaf->firstleafface), little_ushort(leaf->numleaffaces), bsp->num_leaffaces) ||
			!range_contains(little_ushort(leaf->firstleafbrush), little_ushort(leaf->numleafbrushes), num_leafbrushes))
			return BSP_REJECT_CODE();
		for (int axis = 0; axis < 3; ++axis)
			if (LittleShort(leaf->mins[axis]) > LittleShort(leaf->maxs[axis]))
				return BSP_REJECT_CODE();
	}
	for (int i = 0; i < bsp->num_leaffaces; ++i)
		if (little_ushort(bsp->leaffaces[i]) >= bsp->num_faces)
			return BSP_REJECT_CODE();
	for (int i = 0; i < num_leafbrushes; ++i)
		if (little_ushort(leafbrushes[i]) >= num_brushes)
			return BSP_REJECT_CODE();
	for (int i = 0; i < num_brushes; ++i)
		if (!range_contains(LittleLong(brushes[i].firstside), LittleLong(brushes[i].numsides), num_brushsides))
			return BSP_REJECT_CODE();
	for (int i = 0; i < num_brushsides; ++i)
	{
		// BSP compilers emit brushsides with texinfo -1 as back sides that are never rendered.
		const int texinfo = LittleShort(brushsides[i].texinfo);
		if (little_ushort(brushsides[i].planenum) >= bsp->num_planes ||
			(texinfo != -1 && (texinfo < 0 || texinfo >= bsp->num_texinfo)))
			return BSP_REJECT_CODE();
	}
	for (int i = 0; i < bsp->num_submodels; ++i)
	{
		const dmodel_t *model = &bsp->submodels[i];
		const int headnode = LittleLong(model->headnode);
		if (!vector_finite(model->mins, 9) || headnode < 0 || headnode >= bsp->num_nodes ||
			!range_contains(LittleLong(model->firstface), LittleLong(model->numfaces), bsp->num_faces))
			return BSP_REJECT_CODE();
		for (int axis = 0; axis < 3; ++axis)
			if (LittleFloat(model->mins[axis]) > LittleFloat(model->maxs[axis]))
				return BSP_REJECT_CODE();
	}
	for (int i = 0; i < num_areas; ++i)
		if (!range_contains(LittleLong(areas[i].firstareaportal), LittleLong(areas[i].numareaportals), num_areaportals))
			return BSP_REJECT_CODE();
	for (int i = 0; i < num_areaportals; ++i)
		if (LittleLong(areaportals[i].portalnum) < 0 || LittleLong(areaportals[i].portalnum) >= MAX_MAP_PORTALS ||
			LittleLong(areaportals[i].otherarea) < 0 || LittleLong(areaportals[i].otherarea) >= num_areas)
			return BSP_REJECT_CODE();
	return true;
}

static void free_bsp(sw_bsp_t *bsp)
{
	if (bsp == NULL)
		return;
	free(bsp->texinfo_images);
	if (bsp->file != NULL)
		sw_ri.FS_FreeFile(bsp->file);
	free(bsp);
}

static sw_bsp_t *load_bsp(const char *name)
{
	void *file = NULL;
	const int length = sw_ri.FS_LoadFile(name, &file);
	if (file == NULL || length < (int)sizeof(dheader_t))
		return NULL;

	const dheader_t *header = file;
	const qboolean valid = LittleLong(header->ident) == IDBSPHEADER &&
		LittleLong(header->version) == BSPVERSION &&
		lump_valid(header, length, LUMP_PLANES, sizeof(dplane_t), false) &&
		lump_valid(header, length, LUMP_VERTEXES, sizeof(dvertex_t), false) &&
		lump_valid(header, length, LUMP_NODES, sizeof(dnode_t), false) &&
		lump_valid(header, length, LUMP_TEXINFO, sizeof(texinfo_t), false) &&
		lump_valid(header, length, LUMP_ENTITIES, 1, true) &&
		lump_valid(header, length, LUMP_FACES, sizeof(dface_t), true) &&
		lump_valid(header, length, LUMP_LIGHTING, 1, true) &&
		lump_valid(header, length, LUMP_LEAFS, sizeof(dleaf_t), false) &&
		lump_valid(header, length, LUMP_LEAFFACES, sizeof(ushort), true) &&
		lump_valid(header, length, LUMP_LEAFBRUSHES, sizeof(ushort), true) &&
		lump_valid(header, length, LUMP_EDGES, sizeof(dedge_t), false) &&
		lump_valid(header, length, LUMP_SURFEDGES, sizeof(int), true) &&
		lump_valid(header, length, LUMP_MODELS, sizeof(dmodel_t), false) &&
		lump_valid(header, length, LUMP_BRUSHES, sizeof(dbrush_t), true) &&
		lump_valid(header, length, LUMP_BRUSHSIDES, sizeof(dbrushside_t), true) &&
		lump_valid(header, length, LUMP_POP, 1, true) &&
		lump_valid(header, length, LUMP_AREAS, sizeof(darea_t), true) &&
		lump_valid(header, length, LUMP_AREAPORTALS, sizeof(dareaportal_t), true) &&
		lump_valid(header, length, LUMP_VISIBILITY, 1, true);
	if (!valid)
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected malformed BSP '%s'\n", name);
		sw_ri.FS_FreeFile(file);
		return NULL;
	}

	sw_bsp_t *bsp = calloc(1, sizeof(*bsp));
	if (bsp == NULL)
	{
		sw_ri.FS_FreeFile(file);
		return NULL;
	}
	bsp->file = file;
	bsp->length = length;
	bsp->header = header;
	bsp->planes = lump_data(bsp, LUMP_PLANES);
	bsp->num_planes = lump_count(bsp, LUMP_PLANES, sizeof(dplane_t));
	bsp->vertices = lump_data(bsp, LUMP_VERTEXES);
	bsp->num_vertices = lump_count(bsp, LUMP_VERTEXES, sizeof(dvertex_t));
	bsp->nodes = lump_data(bsp, LUMP_NODES);
	bsp->num_nodes = lump_count(bsp, LUMP_NODES, sizeof(dnode_t));
	bsp->texinfo = lump_data(bsp, LUMP_TEXINFO);
	bsp->num_texinfo = lump_count(bsp, LUMP_TEXINFO, sizeof(texinfo_t));
	bsp->faces = lump_data(bsp, LUMP_FACES);
	bsp->num_faces = lump_count(bsp, LUMP_FACES, sizeof(dface_t));
	bsp->lighting = lump_data(bsp, LUMP_LIGHTING);
	bsp->lighting_length = LittleLong(header->lumps[LUMP_LIGHTING].filelen);
	bsp->leaves = lump_data(bsp, LUMP_LEAFS);
	bsp->num_leaves = lump_count(bsp, LUMP_LEAFS, sizeof(dleaf_t));
	bsp->leaffaces = lump_data(bsp, LUMP_LEAFFACES);
	bsp->num_leaffaces = lump_count(bsp, LUMP_LEAFFACES, sizeof(ushort));
	bsp->edges = lump_data(bsp, LUMP_EDGES);
	bsp->num_edges = lump_count(bsp, LUMP_EDGES, sizeof(dedge_t));
	bsp->surfedges = lump_data(bsp, LUMP_SURFEDGES);
	bsp->num_surfedges = lump_count(bsp, LUMP_SURFEDGES, sizeof(int));
	bsp->submodels = lump_data(bsp, LUMP_MODELS);
	bsp->num_submodels = lump_count(bsp, LUMP_MODELS, sizeof(dmodel_t));
	bsp->visibility = lump_data(bsp, LUMP_VISIBILITY);
	bsp->visibility_length = LittleLong(header->lumps[LUMP_VISIBILITY].filelen);
	if (!bsp_semantics_valid(bsp))
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected semantically invalid BSP '%s'\n", name);
		free_bsp(bsp);
		return NULL;
	}
	bsp->texinfo_images = calloc((size_t)bsp->num_texinfo, sizeof(*bsp->texinfo_images));
	if (bsp->texinfo_images == NULL)
	{
		free_bsp(bsp);
		return NULL;
	}

	for (int i = 0; i < bsp->num_texinfo; ++i)
	{
		char texture_name[MAX_QPATH];
		char base[33];
		memcpy(base, bsp->texinfo[i].texture, 32);
		base[32] = '\0';
		Com_sprintf(texture_name, sizeof(texture_name), "textures/%s.m8", base);
		bsp->texinfo_images[i] = SW_FindImage(texture_name, SW_IMAGE_WALL);
	}
	return bsp;
}

static sw_model_t *new_model(const char *name)
{
	if (sw_num_models == SW_MAX_MODELS)
		return NULL;
	sw_model_t *model = &sw_models[sw_num_models++];
	memset(model, 0, sizeof(*model));
	strcpy_s(model->name, sizeof(model->name), name);
	model->registration_sequence = sw_registration_sequence;
	return model;
}

static sw_model_t *find_model(const char *name)
{
	for (int i = 0; i < sw_num_models; ++i)
		if (Q_stricmp(sw_models[i].name, name) == 0)
			return &sw_models[i];
	return NULL;
}

static qboolean book_source_valid(const book_t *source, const int length, size_t *required_out)
{
	if (source == NULL || length < (int)sizeof(bookheader_t))
		return false;
	const int segments = LittleLong(source->bheader.num_segments);
	const int total_width = LittleLong(source->bheader.total_w);
	const int total_height = LittleLong(source->bheader.total_h);
	if (LittleLong(source->bheader.ident) != IDBOOKHEADER ||
		LittleLong(source->bheader.version) != BOOK_VERSION || segments < 1 || segments > MAX_FRAMES ||
		total_width < 1 || total_width > 32768 || total_height < 1 || total_height > 32768)
		return false;
	const size_t required = sizeof(bookheader_t) + (size_t)segments * sizeof(bookframe_t);
	if (required > (size_t)length)
		return false;
	for (int i = 0; i < segments; ++i)
	{
		const bookframe_t *frame = &source->bframes[i];
		if (!range_contains(LittleLong(frame->x), LittleLong(frame->w), total_width) ||
			!range_contains(LittleLong(frame->y), LittleLong(frame->h), total_height) ||
			LittleLong(frame->w) == 0 || LittleLong(frame->h) == 0 || frame->name[0] == '\0')
			return false;
	}
	*required_out = required;
	return true;
}

static sw_model_t *load_book(const char *name)
{
	void *file = NULL;
	const int length = sw_ri.FS_LoadFile(name, &file);
	if (file == NULL || length < (int)sizeof(bookheader_t))
		return NULL;
	const book_t *source = file;
	const int segments = LittleLong(source->bheader.num_segments);
	size_t required;
	if (!book_source_valid(source, length, &required))
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected malformed book '%s'\n", name);
		sw_ri.FS_FreeFile(file);
		return NULL;
	}

	sw_model_t *model = new_model(name);
	if (model == NULL)
	{
		sw_ri.FS_FreeFile(file);
		return NULL;
	}
	model->book = calloc(1, sizeof(*model->book));
	if (model->book == NULL)
	{
		--sw_num_models;
		sw_ri.FS_FreeFile(file);
		return NULL;
	}
	memcpy(model->book, source, required);
	model->kind = SW_MODEL_BOOK;
	model->num_frames = segments;
	for (int i = 0; i < segments; ++i)
	{
		char frame_path[MAX_QPATH];
		char frame_name[MAX_FRAMENAME + 1];
		memcpy(frame_name, source->bframes[i].name, MAX_FRAMENAME);
		frame_name[MAX_FRAMENAME] = '\0';
		Com_sprintf(frame_path, sizeof(frame_path), "Book/%s", frame_name);
		model->book_images[i] = SW_FindImage(frame_path, SW_IMAGE_PIC);
	}
	sw_ri.FS_FreeFile(file);
	return model;
}

static sw_model_t *load_sprite(const char *name)
{
	void *file = NULL;
	const int length = sw_ri.FS_LoadFile(name, &file);
	if (file == NULL || length < (int)offsetof(dsprite_t, frames))
		return NULL;
	const dsprite_t *sprite = file;
	const int frames = LittleLong(sprite->numframes);
	const size_t required = offsetof(dsprite_t, frames) + (size_t)max(0, frames) * sizeof(dsprframe_t);
	if (LittleLong(sprite->ident) != IDSPRITEHEADER || LittleLong(sprite->version) != SPRITE_VERSION ||
		frames < 1 || frames > MAX_FRAMES || required > (size_t)length)
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected malformed sprite '%s'\n", name);
		sw_ri.FS_FreeFile(file);
		return NULL;
	}

	sw_model_t *model = new_model(name);
	if (model == NULL)
	{
		sw_ri.FS_FreeFile(file);
		return NULL;
	}
	model->kind = SW_MODEL_SPRITE;
	model->num_frames = frames;
	model->sprite_frames = calloc((size_t)frames, sizeof(*model->sprite_frames));
	if (model->sprite_frames == NULL)
	{
		--sw_num_models;
		sw_ri.FS_FreeFile(file);
		return NULL;
	}

	for (int i = 0; i < frames; ++i)
	{
		const dsprframe_t *source = &sprite->frames[i];
		sw_sprite_frame_t *destination = &model->sprite_frames[i];
		destination->width = LittleLong(source->width);
		destination->height = LittleLong(source->height);
		destination->origin_x = LittleLong(source->origin_x);
		destination->origin_y = LittleLong(source->origin_y);
		if (destination->width <= 0 || destination->height <= 0 ||
			destination->width > 4096 || destination->height > 4096)
		{
			sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected malformed sprite frame in '%s'\n", name);
			free(model->sprite_frames);
			model->sprite_frames = NULL;
			--sw_num_models;
			sw_ri.FS_FreeFile(file);
			return NULL;
		}
		char frame_name[MAX_QPATH];
		char base[MAX_FRAMENAME + 1];
		memcpy(base, source->name, MAX_FRAMENAME);
		base[MAX_FRAMENAME] = '\0';
		Com_sprintf(frame_name, sizeof(frame_name), "Sprites/%s", base);
		destination->image = SW_FindImage(frame_name, SW_IMAGE_SPRITE);
	}
	sw_ri.FS_FreeFile(file);
	return model;
}

void SW_ModelInit(void)
{
	sw_num_models = 0;
	sw_world_model = NULL;
	sw_world_bsp = NULL;
}

void SW_ModelShutdown(void)
{
	for (int i = 0; i < sw_num_models; ++i)
	{
		free(sw_models[i].sprite_frames);
		free(sw_models[i].book);
		SW_FreeFlex(sw_models[i].flex);
	}
	free_bsp(sw_world_bsp);
	memset(sw_models, 0, sizeof(sw_models));
	sw_num_models = 0;
	sw_world_model = NULL;
	sw_world_bsp = NULL;
}

void SW_BeginRegistration(const char *map)
{
	char path[MAX_QPATH];
	++sw_registration_sequence;
	SW_ModelShutdown();
	SW_ModelInit();
	Com_sprintf(path, sizeof(path), "maps/%s.bsp", map);
	sw_world_bsp = load_bsp(path);
	if (sw_world_bsp == NULL)
	{
		sw_ri.Com_Error(ERR_DROP, "ref_soft: could not load world '%s'", path);
		return;
	}
	sw_world_model = new_model(path);
	if (sw_world_model == NULL)
	{
		free_bsp(sw_world_bsp);
		sw_world_bsp = NULL;
		sw_ri.Com_Error(ERR_DROP, "ref_soft: model registry is full");
		return;
	}
	sw_world_model->kind = SW_MODEL_BSP;
	sw_world_model->bsp = sw_world_bsp;
	sw_world_model->first_face = LittleLong(sw_world_bsp->submodels[0].firstface);
	sw_world_model->num_faces = LittleLong(sw_world_bsp->submodels[0].numfaces);
}

struct model_s *SW_RegisterModel(const char *name)
{
	if (name == NULL || name[0] == '\0')
		return NULL;
	sw_model_t *existing = find_model(name);
	if (existing != NULL)
	{
		existing->registration_sequence = sw_registration_sequence;
		return existing;
	}
	if (name[0] == '*')
	{
		const int index = atoi(name + 1);
		if (sw_world_bsp == NULL || index < 1 || index >= sw_world_bsp->num_submodels)
			return NULL;
		sw_model_t *model = new_model(name);
		if (model == NULL)
			return NULL;
		model->kind = SW_MODEL_INLINE;
		model->bsp = sw_world_bsp;
		model->first_face = LittleLong(sw_world_bsp->submodels[index].firstface);
		model->num_faces = LittleLong(sw_world_bsp->submodels[index].numfaces);
		for (int axis = 0; axis < 3; ++axis)
		{
			model->mins[axis] = LittleFloat(sw_world_bsp->submodels[index].mins[axis]);
			model->maxs[axis] = LittleFloat(sw_world_bsp->submodels[index].maxs[axis]);
		}
		return model;
	}
	const char *extension = strrchr(name, '.');
	if (extension != NULL && Q_stricmp(extension, ".sp2") == 0)
		return load_sprite(name);
	if (extension != NULL && Q_stricmp(extension, ".bk") == 0)
		return load_book(name);
	if (extension != NULL && Q_stricmp(extension, ".fm") == 0)
	{
		// Remaster-layout requests that exist in the original H2 paks only under
		// the retail layout. Paks are read-only, so aliasing is the mechanism.
		static const struct {
			const char *requested;
			const char *alias;
		} k_model_aliases[] = {
			{ "players/male/tris.fm", "models/player/c_watcher/tris.fm" },
		};
		const char *load_name = name;
		for (size_t i = 0; i < sizeof(k_model_aliases) / sizeof(k_model_aliases[0]); ++i)
			if (Q_stricmp(name, k_model_aliases[i].requested) == 0)
			{
				load_name = k_model_aliases[i].alias;
				break;
			}
		sw_flex_t *flex = SW_LoadFlexFile(load_name, true);
		if (flex == NULL)
			return NULL;
		sw_model_t *model = new_model(name);
		if (model == NULL)
		{
			SW_FreeFlex(flex);
			return NULL;
		}
		model->kind = SW_MODEL_FLEX;
		model->flex = flex;
		model->num_frames = flex->header.num_frames;
		++sw_stats.flex_models;
		return model;
	}
	sw_ri.Con_Printf(PRINT_ALL, "ref_soft: unsupported or missing model '%s'\n", name);
	return NULL;
}

struct image_s *SW_RegisterSkin(const char *name, qboolean *loaded)
{
	sw_image_t *image = SW_FindImage(name, SW_IMAGE_SKIN);
	if (loaded != NULL)
		*loaded = image != NULL && !image->synthetic_fallback;
	return image;
}

struct image_s *SW_RegisterPic(const char *name)
{
	char path[MAX_QPATH];
	if (name == NULL)
		return NULL;
	if (name[0] == '/' || name[0] == '\\')
		return SW_FindImage(name + 1, SW_IMAGE_PIC);
	Com_sprintf(path, sizeof(path), "pics/%s", name);
	return SW_FindImage(path, SW_IMAGE_PIC);
}

void SW_EndRegistration(void)
{
	SW_FreeUnusedImages();
}

qboolean SW_TestBSPFile(const char *name)
{
	sw_bsp_t *bsp = load_bsp(name);
	if (bsp == NULL)
		return false;
	free_bsp(bsp);
	return true;
}

qboolean SW_TestBookFile(const char *name)
{
	void *file = NULL;
	const int length = sw_ri.FS_LoadFile(name, &file);
	size_t required;
	const qboolean valid = book_source_valid(file, length, &required);
	(void)required;
	if (file != NULL)
		sw_ri.FS_FreeFile(file);
	return valid;
}

static float query_dot(const vec3_t a, const vec3_t b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static qboolean query_point_inside(const sw_bsp_t *bsp, const dface_t *face,
	const vec3_t point, const vec3_t normal)
{
	const int first = LittleLong(face->firstedge);
	const int count = LittleShort(face->numedges);
	float winding_sign = 0.0f;
	for (int i = 0; i < count; ++i)
	{
		const int surface_edge = LittleLong(bsp->surfedges[first + i]);
		const int next_surface_edge = LittleLong(bsp->surfedges[first + (i + 1) % count]);
		const int vertex_index = little_ushort(bsp->edges[abs(surface_edge)].v[surface_edge >= 0 ? 0 : 1]);
		const int next_vertex_index = little_ushort(bsp->edges[abs(next_surface_edge)].v[next_surface_edge >= 0 ? 0 : 1]);
		vec3_t edge_vector;
		vec3_t point_vector;
		for (int axis = 0; axis < 3; ++axis)
		{
			edge_vector[axis] = LittleFloat(bsp->vertices[next_vertex_index].point[axis]) -
				LittleFloat(bsp->vertices[vertex_index].point[axis]);
			point_vector[axis] = point[axis] - LittleFloat(bsp->vertices[vertex_index].point[axis]);
		}
		const vec3_t cross = {
			edge_vector[1] * point_vector[2] - edge_vector[2] * point_vector[1],
			edge_vector[2] * point_vector[0] - edge_vector[0] * point_vector[2],
			edge_vector[0] * point_vector[1] - edge_vector[1] * point_vector[0]
		};
		const float value = query_dot(cross, normal);
		if (fabsf(value) <= 0.01f)
			continue;
		if (winding_sign == 0.0f)
			winding_sign = value;
		else if ((value > 0.0f) != (winding_sign > 0.0f))
			return false;
	}
	return true;
}

int SW_FindSurface(const vec3_t start, const vec3_t end, struct Surface_s *surface_pointer)
{
	Surface_t *surface = surface_pointer;
	if (surface == NULL)
		return 0;
	memset(surface, 0, sizeof(*surface));
	if (sw_world_bsp == NULL)
		return -1;
	for (int axis = 0; axis < 3; ++axis)
		if (!isfinite(start[axis]) || !isfinite(end[axis]))
			return 0;

	vec3_t direction = {
		end[0] - start[0], end[1] - start[1], end[2] - start[2]
	};
	if (query_dot(direction, direction) <= 0.000001f)
		return -1;

	const sw_bsp_t *bsp = sw_world_bsp;
	float nearest_fraction = FLT_MAX;
	int nearest_face = -1;
	vec3_t nearest_point = { 0.0f, 0.0f, 0.0f };
	vec3_t nearest_normal = { 0.0f, 0.0f, 0.0f };
	float nearest_dist = 0.0f;
	for (int face_index = 0; face_index < bsp->num_faces; ++face_index)
	{
		const dface_t *face = &bsp->faces[face_index];
		const int texinfo_index = LittleShort(face->texinfo);
		const int flags = LittleLong(bsp->texinfo[texinfo_index].flags);
		if (flags & (SURF_SKY | SURF_WARP | SURF_NODRAW))
			continue;
		const dplane_t *plane = &bsp->planes[little_ushort(face->planenum)];
		vec3_t normal = {
			LittleFloat(plane->normal[0]), LittleFloat(plane->normal[1]), LittleFloat(plane->normal[2])
		};
		float dist = LittleFloat(plane->dist);
		if (LittleShort(face->side) != 0)
		{
			for (int axis = 0; axis < 3; ++axis)
				normal[axis] = -normal[axis];
			dist = -dist;
		}
		const float denominator = query_dot(direction, normal);
		if (fabsf(denominator) < 0.000001f)
			continue;
		const float fraction = (dist - query_dot(start, normal)) / denominator;
		if (!isfinite(fraction) || fraction < 0.0f || fraction > 1.0f || fraction >= nearest_fraction)
			continue;
		vec3_t point;
		for (int axis = 0; axis < 3; ++axis)
			point[axis] = start[axis] + direction[axis] * fraction;
		if (!query_point_inside(bsp, face, point, normal))
			continue;
		nearest_fraction = fraction;
		nearest_face = face_index;
		nearest_dist = dist;
		memcpy(nearest_point, point, sizeof(nearest_point));
		memcpy(nearest_normal, normal, sizeof(nearest_normal));
	}
	if (nearest_face < 0)
		return -1;

	static cplane_t result_plane;
	static float result_vertices[SW_MAX_FACE_VERTS][7];
	const dface_t *face = &bsp->faces[nearest_face];
	const texinfo_t *texture = &bsp->texinfo[LittleShort(face->texinfo)];
	const int first = LittleLong(face->firstedge);
	const int count = LittleShort(face->numedges);
	memset(&result_plane, 0, sizeof(result_plane));
	memcpy(result_plane.normal, nearest_normal, sizeof(result_plane.normal));
	result_plane.dist = nearest_dist;
	result_plane.type = (byte)LittleLong(bsp->planes[little_ushort(face->planenum)].type);
	for (int axis = 0; axis < 3; ++axis)
		if (result_plane.normal[axis] < 0.0f)
			result_plane.signbits |= (byte)(1u << axis);
	for (int i = 0; i < count; ++i)
	{
		const int surface_edge = LittleLong(bsp->surfedges[first + i]);
		const int vertex_index = little_ushort(bsp->edges[abs(surface_edge)].v[surface_edge >= 0 ? 0 : 1]);
		for (int axis = 0; axis < 3; ++axis)
			result_vertices[i][axis] = LittleFloat(bsp->vertices[vertex_index].point[axis]);
		for (int coordinate = 0; coordinate < 2; ++coordinate)
		{
			const float value = result_vertices[i][0] * LittleFloat(texture->vecs[coordinate][0]) +
				result_vertices[i][1] * LittleFloat(texture->vecs[coordinate][1]) +
				result_vertices[i][2] * LittleFloat(texture->vecs[coordinate][2]) +
				LittleFloat(texture->vecs[coordinate][3]);
			result_vertices[i][3 + coordinate] = value;
			result_vertices[i][5 + coordinate] = value / 16.0f;
		}
	}
	surface->plane = &result_plane;
	memcpy(surface->normal, nearest_normal, sizeof(surface->normal));
	memcpy(surface->point, nearest_point, sizeof(surface->point));
	surface->poly.fraction = nearest_fraction;
	surface->poly.numverts = count;
	surface->poly.flags = LittleLong(texture->flags);
	surface->poly.verts = result_vertices;
	++sw_stats.surface_queries;
	return 1;
}

int SW_GetReferencedID(const struct model_s *model)
{
	if (model == NULL || model->kind != SW_MODEL_FLEX || model->flex == NULL)
		return REF_NULL;
	return model->flex->reference_type;
}
