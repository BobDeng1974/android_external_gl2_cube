/*
 * This proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2012 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef CUBE_H
#define CUBE_H

#define GLES_VERSION 2

#include <GLES2/gl2.h>

/* These indices describe the cube triangle strips, separated by degenerate triangles where necessary. */
static const GLubyte cubeIndices[] =
{
    0, 1, 2, 3,   3, 4,   4, 5, 6, 7,   7, 8,   8, 9, 10, 11,   11, 12,   12, 13, 14, 15,   15, 16,   16, 17, 18, 19,   19, 20,   20, 21, 22, 23,
};

/* Tri strips, so quads are in this order:
 *
 * 2 ----- 3
 * | \     |
 * |   \   |6 - 7
 * |     \ || \ |
 * 0 ----- 14 - 5
 */
static const float cubeVertices[] =
{
    /* Front. */
    -0.5f, -0.5f,  0.5f, /* 0 */
     0.5f, -0.5f,  0.5f, /* 1 */
    -0.5f,  0.5f,  0.5f, /* 2 */
     0.5f,  0.5f,  0.5f, /* 3 */

    /* Right. */
     0.5f, -0.5f,  0.5f, /* 4 */
     0.5f, -0.5f, -0.5f, /* 5 */
     0.5f,  0.5f,  0.5f, /* 6 */
     0.5f,  0.5f, -0.5f, /* 7 */

    /* Back. */
     0.5f, -0.5f, -0.5f, /* 8 */
    -0.5f, -0.5f, -0.5f, /* 9 */
     0.5f,  0.5f, -0.5f, /* 10 */
    -0.5f,  0.5f, -0.5f, /* 11 */

    /* Left. */
    -0.5f, -0.5f, -0.5f, /* 12 */
    -0.5f, -0.5f,  0.5f, /* 13 */
    -0.5f,  0.5f, -0.5f, /* 14 */
    -0.5f,  0.5f,  0.5f, /* 15 */

    /* Top. */
    -0.5f,  0.5f,  0.5f, /* 16 */
     0.5f,  0.5f,  0.5f, /* 17 */
    -0.5f,  0.5f, -0.5f, /* 18 */
     0.5f,  0.5f, -0.5f, /* 19 */

    /* Bottom. */
    -0.5f, -0.5f, -0.5f, /* 20 */
     0.5f, -0.5f, -0.5f, /* 21 */
    -0.5f, -0.5f,  0.5f, /* 22 */
     0.5f, -0.5f,  0.5f, /* 23 */
};

static const float cubeTextureCoordinates[] =
{
    /* Front. */
    0.0f, 0.0f, /* 0 */
    1.0f, 0.0f, /* 1 */
    0.0f, 1.0f, /* 2 */
    1.0f, 1.0f, /* 3 */

    /* Right. */
    0.0f, 0.0f, /* 4 */
    1.0f, 0.0f, /* 5 */
    0.0f, 1.0f, /* 6 */
    1.0f, 1.0f, /* 7 */

    /* Back. */
    0.0f, 0.0f, /* 8 */
    1.0f, 0.0f, /* 9 */
    0.0f, 1.0f, /* 10 */
    1.0f, 1.0f, /* 11 */

    /* Left. */
    0.0f, 0.0f, /* 12 */
    1.0f, 0.0f, /* 13 */
    0.0f, 1.0f, /* 14 */
    1.0f, 1.0f, /* 15 */

    /* Top. */
    0.0f, 0.0f, /* 16 */
    1.0f, 0.0f, /* 17 */
    0.0f, 1.0f, /* 18 */
    1.0f, 1.0f, /* 19 */

    /* Bottom. */
    0.0f, 0.0f, /* 20 */
    1.0f, 0.0f, /* 21 */
    0.0f, 1.0f, /* 22 */
    1.0f, 1.0f, /* 23 */

};

static const float cubeColors[] =
{
    /* Front. */
    0.0f, 0.0f, 0.0f, 1.0f, /* 0 */
    1.0f, 0.0f, 0.0f, 1.0f, /* 1 */
    0.0f, 1.0f, 0.0f, 1.0f, /* 2 */
    1.0f, 1.0f, 0.0f, 1.0f, /* 3 */

    /* Right. */
    1.0f, 0.0f, 0.0f, 1.0f, /* 4 */
    0.0f, 0.0f, 1.0f, 1.0f, /* 5 */
    1.0f, 1.0f, 0.0f, 1.0f, /* 6 */
    0.0f, 1.0f, 1.0f, 1.0f, /* 7 */

    /* Back. */
    0.0f, 0.0f, 1.0f, 1.0f, /* 8 */
    1.0f, 0.0f, 1.0f, 1.0f, /* 9 */
    0.0f, 1.0f, 1.0f, 1.0f, /* 10 */
    1.0f, 1.0f, 1.0f, 1.0f, /* 11 */

    /* Left. */
    1.0f, 0.0f, 1.0f, 1.0f, /* 12 */
    0.0f, 0.0f, 0.0f, 1.0f, /* 13 */
    1.0f, 1.0f, 1.0f, 1.0f, /* 14 */
    0.0f, 1.0f, 0.0f, 1.0f, /* 15 */

    /* Top. */
    0.0f, 1.0f, 0.0f, 1.0f, /* 16 */
    1.0f, 1.0f, 0.0f, 1.0f, /* 17 */
    1.0f, 1.0f, 1.0f, 1.0f, /* 18 */
    0.0f, 1.0f, 1.0f, 1.0f, /* 19 */

    /* Bottom. */
    1.0f, 0.0f, 1.0f, 1.0f, /* 20 */
    0.0f, 0.0f, 1.0f, 1.0f, /* 21 */
    0.0f, 0.0f, 0.0f, 1.0f, /* 22 */
    1.0f, 0.0f, 0.0f, 1.0f, /* 23 */
};

#endif /* CUBE_H */