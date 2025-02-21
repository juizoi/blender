/* SPDX-FileCopyrightText: 2025 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw
 */

#ifndef GPU_SHADER
#  pragma once
#endif

#define SUBDIV_GROUP_SIZE 64

/* Uniform buffer bindings */
#define SHADER_DATA_BUF_SLOT 0

/* Storage buffer bindings */
#define SUBDIV_FACE_OFFSET_BUF_SLOT 0

#define LINES_INPUT_EDGE_DRAW_FLAG_BUF_SLOT 1
#define LINES_EXTRA_COARSE_FACE_DATA_BUF_SLOT 2
#define LINES_OUTPUT_LINES_BUF_SLOT 3
#define LINES_LINES_LOOSE_FLAGS 4

#define TRIS_EXTRA_COARSE_FACE_DATA_BUF_SLOT 1
#define TRIS_OUTPUT_TRIS_BUF_SLOT 2
#define TRIS_FACE_MAT_OFFSET 3

#define NORMALS_ACCUMULATE_POS_NOR_BUF_SLOT 0
#define NORMALS_ACCUMULATE_FACE_ADJACENCY_OFFSETS_BUF_SLOT 1
#define NORMALS_ACCUMULATE_FACE_ADJACENCY_LISTS_BUF_SLOT 2
#define NORMALS_ACCUMULATE_VERTEX_LOOP_MAP_BUF_SLOT 3
#define NORMALS_ACCUMULATE_NORMALS_BUF_SLOT 4

#define NORMALS_FINALIZE_VERTEX_NORMALS_BUF_SLOT 0
#define NORMALS_FINALIZE_VERTEX_LOOP_MAP_BUF_SLOT 1
#define NORMALS_FINALIZE_POS_NOR_BUF_SLOT 2
#define NORMALS_FINALIZE_CUSTOM_NORMALS_BUF_SLOT 0

#define LOOP_NORMALS_POS_NOR_BUF_SLOT 1
#define LOOP_NORMALS_EXTRA_COARSE_FACE_DATA_BUF_SLOT 2
#define LOOP_NORMALS_INPUT_VERT_ORIG_INDEX_BUF_SLOT 3
#define LOOP_NORMALS_OUTPUT_LNOR_BUF_SLOT 4
