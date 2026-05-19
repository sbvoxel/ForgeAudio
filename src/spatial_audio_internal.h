/*
 * ForgeAudio
 * Forked from FAudio.
 *
 * Copyright (c) 2026 ForgeAudio contributors.
 * Portions copyright (c) 2011-2024 Ethan Lee, Luigi Auriemma,
 * and the MonoGame Team.
 *
 * Licensed under the zlib license.
 * See LICENSE for full terms.
 */

#ifndef FORGE_SPATIAL_AUDIO_INTERNAL_H
#define FORGE_SPATIAL_AUDIO_INTERNAL_H

#include <forge/spatial_audio.h>
#include "common_internal.h"

#include <math.h>  /* ONLY USE THIS FOR isnan! */
#include <float.h> /* ONLY USE THIS FOR FLT_MIN/FLT_MAX! */

#define PARAM_CHECK_OK 1
#define PARAM_CHECK_FAIL (!PARAM_CHECK_OK)

#define ARRAY_COUNT(x) (sizeof(x) / sizeof(x[0]))

#define LERP(a, x, y) ((1.0f - a) * x + a * y)

#define PARAM_CHECK(cond, msg) forge_assert(cond &&msg)

#define POINTER_CHECK(p) PARAM_CHECK(p != NULL, "Pointer " #p " must be != NULL")

#define FLOAT_BETWEEN_CHECK(f, a, b)                                                                                   \
    PARAM_CHECK(f >= a, "Value" #f " is too low");                                                                     \
    PARAM_CHECK(f <= b, "Value" #f " is too big")

static inline ForgeVector3 vector_make(float x, float y, float z) {
    ForgeVector3 res;
    res.x = x;
    res.y = y;
    res.z = z;
    return res;
}

#define VECTOR_ADD(u, v) vector_make(u.x + v.x, u.y + v.y, u.z + v.z)

#define VECTOR_SUB(u, v) vector_make(u.x - v.x, u.y - v.y, u.z - v.z)

#define VECTOR_SCALE(u, s) vector_make(u.x *s, u.y *s, u.z *s)

#define VECTOR_CROSS(u, v)                                                                                             \
    vector_make((u.y * v.z) - (u.z * v.y), (u.z * v.x) - (u.x * v.z), (u.x * v.y) - (u.y * v.x))

#define VECTOR_LENGTH(v) forge_sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z))

#define VECTOR_DOT(u, v) ((u.x * v.x) + (u.y * v.y) + (u.z * v.z))

/* Spatial vectors are treated as orthonormal when their magnitude is within
 * 1e-5 of 1.0 and their dot product is within 1e-5 of zero.
 */

/* Potential optimization: compare squared length to avoid sqrt in vector validation. */
#define VECTOR_NORMAL_CHECK(v) PARAM_CHECK(forge_fabsf(VECTOR_LENGTH(v) - 1.0f) <= 1e-5f, "Vector " #v " isn't normal")

#define VECTOR_BASE_CHECK(u, v)                                                                                        \
    PARAM_CHECK(forge_fabsf(VECTOR_DOT(u, v)) <= 1e-5f, "Vector u and v have non-negligible dot product")

/* This structure represents a tuple of vectors that form a left-handed basis.
 * That is, all vectors are normal, orthogonal to each other, and taken in the
 * order front, right, top they follow the left-hand rule.
 */
typedef struct ForgeSpatialBasis {
    ForgeVector3 front;
    ForgeVector3 right;
    ForgeVector3 top;
} ForgeSpatialBasis;

#endif /* FORGE_SPATIAL_AUDIO_INTERNAL_H */
