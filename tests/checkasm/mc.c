/*
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "tests/checkasm/checkasm.h"

#include <assert.h>

#include "src/levels.h"
#include "src/mc.h"

static const char *const filter_names[] = {
    "8tap_regular",        "8tap_regular_smooth", "8tap_regular_sharp",
    "8tap_sharp_regular",  "8tap_sharp_smooth",   "8tap_sharp",
    "8tap_smooth_regular", "8tap_smooth",         "8tap_smooth_sharp",
    "bilinear"
};

static const char *const mxy_names[] = { "0", "h", "v", "hv" };

static void check_mc(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(pixel, src_buf, 135 * 135,);
    ALIGN_STK_32(pixel, c_dst,   128 * 128,);
    ALIGN_STK_32(pixel, a_dst,   128 * 128,);
    const pixel *src = src_buf + 135 * 3 + 3;

    declare_func(void, pixel *dst, ptrdiff_t dst_stride, const pixel *src,
                 ptrdiff_t src_stride, int w, int h, int mx, int my
                 HIGHBD_DECL_SUFFIX);

    for (int filter = 0; filter < N_2D_FILTERS; filter++)
        for (int w = 2; w <= 128; w <<= 1)
            for (int mxy = 0; mxy < 4; mxy++)
                if (check_func(c->mc[filter], "mc_%s_w%d_%s_%dbpc",
                    filter_names[filter], w, mxy_names[mxy], BITDEPTH))
                {
                    const int min = w <= 32 ? 2 : w / 4;
                    const int max = imax(imin(w * 4, 128), 32);
                    for (int h = min; h <= max; h <<= 1) {
                        const int mx = (mxy & 1) ? rnd() % 15 + 1 : 0;
                        const int my = (mxy & 2) ? rnd() % 15 + 1 : 0;
#if BITDEPTH == 16
                        const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
                        const int bitdepth_max = 0xff;
#endif

                        for (int i = 0; i < 135 * 135; i++)
                            src_buf[i] = rnd() & bitdepth_max;

                        call_ref(c_dst, w, src, w, w, h, mx, my HIGHBD_TAIL_SUFFIX);
                        call_new(a_dst, w, src, w, w, h, mx, my HIGHBD_TAIL_SUFFIX);
                        if (memcmp(c_dst, a_dst, w * h * sizeof(*c_dst)))
                            fail();

                        if (filter == FILTER_2D_8TAP_REGULAR ||
                            filter == FILTER_2D_BILINEAR)
                            bench_new(a_dst, w, src, w, w, h, mx, my HIGHBD_TAIL_SUFFIX);
                    }
                }
    report("mc");
}

static void check_mct(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(pixel, src_buf, 135 * 135,);
    ALIGN_STK_32(int16_t, c_tmp,   128 * 128,);
    ALIGN_STK_32(int16_t, a_tmp,   128 * 128,);
    const pixel *src = src_buf + 135 * 3 + 3;

    declare_func(void, int16_t *tmp, const pixel *src, ptrdiff_t src_stride,
                 int w, int h, int mx, int my HIGHBD_DECL_SUFFIX);

    for (int filter = 0; filter < N_2D_FILTERS; filter++)
        for (int w = 4; w <= 128; w <<= 1)
            for (int mxy = 0; mxy < 4; mxy++)
                if (check_func(c->mct[filter], "mct_%s_w%d_%s_%dbpc",
                    filter_names[filter], w, mxy_names[mxy], BITDEPTH))
                    for (int h = imax(w / 4, 4); h <= imin(w * 4, 128); h <<= 1)
                    {
                        const int mx = (mxy & 1) ? rnd() % 15 + 1 : 0;
                        const int my = (mxy & 2) ? rnd() % 15 + 1 : 0;
#if BITDEPTH == 16
                        const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
                        const int bitdepth_max = 0xff;
#endif

                        for (int i = 0; i < 135 * 135; i++)
                            src_buf[i] = rnd() & bitdepth_max;

                        call_ref(c_tmp, src, w, w, h, mx, my HIGHBD_TAIL_SUFFIX);
                        call_new(a_tmp, src, w, w, h, mx, my HIGHBD_TAIL_SUFFIX);
                        if (memcmp(c_tmp, a_tmp, w * h * sizeof(*c_tmp)))
                            fail();

                        if (filter == FILTER_2D_8TAP_REGULAR ||
                            filter == FILTER_2D_BILINEAR)
                            bench_new(a_tmp, src, w, w, h, mx, my HIGHBD_TAIL_SUFFIX);
                    }
    report("mct");
}

static void init_tmp(Dav1dMCDSPContext *const c, pixel *const buf,
                     int16_t (*const tmp)[128 * 128], const int bitdepth_max)
{
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 135 * 135; j++)
            buf[j] = rnd() & bitdepth_max;
        c->mct[rnd() % N_2D_FILTERS](tmp[i], buf + 135 * 3 + 3,
                                      128 * sizeof(pixel), 128, 128,
                                      rnd() & 15, rnd() & 15
                                      HIGHBD_TAIL_SUFFIX);
    }
}

static void check_avg(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(int16_t, tmp, 2, [128 * 128]);
    ALIGN_STK_32(pixel, c_dst, 135 * 135,);
    ALIGN_STK_32(pixel, a_dst, 128 * 128,);

    declare_func(void, pixel *dst, ptrdiff_t dst_stride, const int16_t *tmp1,
                 const int16_t *tmp2, int w, int h HIGHBD_DECL_SUFFIX);

    for (int w = 4; w <= 128; w <<= 1)
        if (check_func(c->avg, "avg_w%d_%dbpc", w, BITDEPTH))
            for (int h = imax(w / 4, 4); h <= imin(w * 4, 128); h <<= 1)
            {
#if BITDEPTH == 16
                const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
                const int bitdepth_max = 0xff;
#endif
                init_tmp(c, c_dst, tmp, bitdepth_max);
                call_ref(c_dst, w, tmp[0], tmp[1], w, h HIGHBD_TAIL_SUFFIX);
                call_new(a_dst, w, tmp[0], tmp[1], w, h HIGHBD_TAIL_SUFFIX);
                if (memcmp(c_dst, a_dst, w * h * sizeof(*c_dst)))
                    fail();

                bench_new(a_dst, w, tmp[0], tmp[1], w, h HIGHBD_TAIL_SUFFIX);
            }
    report("avg");
}

static void check_w_avg(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(int16_t, tmp, 2, [128 * 128]);
    ALIGN_STK_32(pixel, c_dst, 135 * 135,);
    ALIGN_STK_32(pixel, a_dst, 128 * 128,);

    declare_func(void, pixel *dst, ptrdiff_t dst_stride, const int16_t *tmp1,
                 const int16_t *tmp2, int w, int h, int weight HIGHBD_DECL_SUFFIX);

    for (int w = 4; w <= 128; w <<= 1)
        if (check_func(c->w_avg, "w_avg_w%d_%dbpc", w, BITDEPTH))
            for (int h = imax(w / 4, 4); h <= imin(w * 4, 128); h <<= 1)
            {
                int weight = rnd() % 15 + 1;
#if BITDEPTH == 16
                const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
                const int bitdepth_max = 0xff;
#endif
                init_tmp(c, c_dst, tmp, bitdepth_max);

                call_ref(c_dst, w, tmp[0], tmp[1], w, h, weight HIGHBD_TAIL_SUFFIX);
                call_new(a_dst, w, tmp[0], tmp[1], w, h, weight HIGHBD_TAIL_SUFFIX);
                if (memcmp(c_dst, a_dst, w * h * sizeof(*c_dst)))
                    fail();

                bench_new(a_dst, w, tmp[0], tmp[1], w, h, weight HIGHBD_TAIL_SUFFIX);
            }
    report("w_avg");
}

static void check_mask(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(int16_t, tmp, 2, [128 * 128]);
    ALIGN_STK_32(pixel,   c_dst, 135 * 135,);
    ALIGN_STK_32(pixel,   a_dst, 128 * 128,);
    ALIGN_STK_32(uint8_t, mask,  128 * 128,);

    for (int i = 0; i < 128 * 128; i++)
        mask[i] = rnd() % 65;

    declare_func(void, pixel *dst, ptrdiff_t dst_stride, const int16_t *tmp1,
                 const int16_t *tmp2, int w, int h, const uint8_t *mask
                 HIGHBD_DECL_SUFFIX);

    for (int w = 4; w <= 128; w <<= 1)
        if (check_func(c->mask, "mask_w%d_%dbpc", w, BITDEPTH))
            for (int h = imax(w / 4, 4); h <= imin(w * 4, 128); h <<= 1)
            {
#if BITDEPTH == 16
                const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
                const int bitdepth_max = 0xff;
#endif
                init_tmp(c, c_dst, tmp, bitdepth_max);
                call_ref(c_dst, w, tmp[0], tmp[1], w, h, mask HIGHBD_TAIL_SUFFIX);
                call_new(a_dst, w, tmp[0], tmp[1], w, h, mask HIGHBD_TAIL_SUFFIX);
                if (memcmp(c_dst, a_dst, w * h * sizeof(*c_dst)))
                    fail();

                bench_new(a_dst, w, tmp[0], tmp[1], w, h, mask HIGHBD_TAIL_SUFFIX);
            }
    report("mask");
}

static void check_w_mask(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(int16_t, tmp, 2, [128 * 128]);
    ALIGN_STK_32(pixel,   c_dst,  135 * 135,);
    ALIGN_STK_32(pixel,   a_dst,  128 * 128,);
    ALIGN_STK_32(uint8_t, c_mask, 128 * 128,);
    ALIGN_STK_32(uint8_t, a_mask, 128 * 128,);

    declare_func(void, pixel *dst, ptrdiff_t dst_stride, const int16_t *tmp1,
                 const int16_t *tmp2, int w, int h, uint8_t *mask, int sign
                 HIGHBD_DECL_SUFFIX);

    static const uint16_t ss[] = { 444, 422, 420 };

    for (int i = 0; i < 3; i++)
        for (int w = 4; w <= 128; w <<= 1)
            if (check_func(c->w_mask[i], "w_mask_%d_w%d_%dbpc", ss[i], w,
                           BITDEPTH))
                for (int h = imax(w / 4, 4); h <= imin(w * 4, 128); h <<= 1)
                {
                    int sign = rnd() & 1;
#if BITDEPTH == 16
                    const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
                    const int bitdepth_max = 0xff;
#endif
                    init_tmp(c, c_dst, tmp, bitdepth_max);

                    call_ref(c_dst, w, tmp[0], tmp[1], w, h, c_mask, sign
                             HIGHBD_TAIL_SUFFIX);
                    call_new(a_dst, w, tmp[0], tmp[1], w, h, a_mask, sign
                             HIGHBD_TAIL_SUFFIX);
                    if (memcmp(c_dst, a_dst, w * h * sizeof(*c_dst)) ||
                        memcmp(c_mask, a_mask, (w * h * sizeof(*c_mask)) >> i))
                    {
                        fail();
                    }

                    bench_new(a_dst, w, tmp[0], tmp[1], w, h, a_mask, sign
                              HIGHBD_TAIL_SUFFIX);
                }
    report("w_mask");
}

static void check_blend(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(pixel, tmp, 32 * 32,);
    ALIGN_STK_32(pixel, c_dst, 32 * 32,);
    ALIGN_STK_32(pixel, a_dst, 32 * 32,);
    ALIGN_STK_32(uint8_t, mask, 32 * 32,);

    declare_func(void, pixel *dst, ptrdiff_t dst_stride, const pixel *tmp,
                 int w, int h, const uint8_t *mask);

    for (int w = 4; w <= 32; w <<= 1) {
        const ptrdiff_t dst_stride = w * sizeof(pixel);
        if (check_func(c->blend, "blend_w%d_%dbpc", w, BITDEPTH))
            for (int h = imax(w / 2, 4); h <= imin(w * 2, 32); h <<= 1) {
#if BITDEPTH == 16
                const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
                const int bitdepth_max = 0xff;
#endif
                for (int i = 0; i < 32 * 32; i++) {
                    tmp[i] = rnd() & bitdepth_max;
                    mask[i] = rnd() % 65;
                }
                for (int i = 0; i < w * h; i++)
                    c_dst[i] = a_dst[i] = rnd() & bitdepth_max;

                call_ref(c_dst, dst_stride, tmp, w, h, mask);
                call_new(a_dst, dst_stride, tmp, w, h, mask);
                if (memcmp(c_dst, a_dst, w * h * sizeof(*c_dst)))
                    fail();

                bench_new(a_dst, dst_stride, tmp, w, h, mask);
            }
    }
    report("blend");
}

static void check_blend_v(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(pixel, tmp,   32 * 128,);
    ALIGN_STK_32(pixel, c_dst, 32 * 128,);
    ALIGN_STK_32(pixel, a_dst, 32 * 128,);

    declare_func(void, pixel *dst, ptrdiff_t dst_stride, const pixel *tmp,
                 int w, int h);

    for (int w = 2; w <= 32; w <<= 1) {
        const ptrdiff_t dst_stride = w * sizeof(pixel);
        if (check_func(c->blend_v, "blend_v_w%d_%dbpc", w, BITDEPTH))
            for (int h = 2; h <= (w == 2 ? 64 : 128); h <<= 1) {
#if BITDEPTH == 16
                const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
                const int bitdepth_max = 0xff;
#endif

                for (int i = 0; i < w * h; i++)
                    c_dst[i] = a_dst[i] = rnd() & bitdepth_max;
                for (int i = 0; i < 32 * 128; i++)
                    tmp[i] = rnd() & bitdepth_max;

                call_ref(c_dst, dst_stride, tmp, w, h);
                call_new(a_dst, dst_stride, tmp, w, h);
                if (memcmp(c_dst, a_dst, w * h * sizeof(*c_dst)))
                    fail();

                bench_new(a_dst, dst_stride, tmp, w, h);
            }
    }
    report("blend_v");
}

static void check_blend_h(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(pixel, tmp,   128 * 32,);
    ALIGN_STK_32(pixel, c_dst, 128 * 32,);
    ALIGN_STK_32(pixel, a_dst, 128 * 32,);

    declare_func(void, pixel *dst, ptrdiff_t dst_stride, const pixel *tmp,
                 int w, int h);

    for (int w = 2; w <= 128; w <<= 1) {
        const ptrdiff_t dst_stride = w * sizeof(pixel);
        if (check_func(c->blend_h, "blend_h_w%d_%dbpc", w, BITDEPTH))
            for (int h = (w == 128 ? 4 : 2); h <= 32; h <<= 1) {
#if BITDEPTH == 16
                const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
                const int bitdepth_max = 0xff;
#endif
                for (int i = 0; i < w * h; i++)
                    c_dst[i] = a_dst[i] = rnd() & bitdepth_max;
                for (int i = 0; i < 128 * 32; i++)
                    tmp[i] = rnd() & bitdepth_max;

                call_ref(c_dst, dst_stride, tmp, w, h);
                call_new(a_dst, dst_stride, tmp, w, h);
                if (memcmp(c_dst, a_dst, w * h * sizeof(*c_dst)))
                    fail();

                bench_new(a_dst, dst_stride, tmp, w, h);
            }
    }
    report("blend_h");
}

static void check_warp8x8(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(pixel, src_buf, 15 * 15,);
    ALIGN_STK_32(pixel, c_dst,    8 *  8,);
    ALIGN_STK_32(pixel, a_dst,    8 *  8,);
    int16_t abcd[4];
    const pixel *src = src_buf + 15 * 3 + 3;
    const ptrdiff_t dst_stride =  8 * sizeof(pixel);
    const ptrdiff_t src_stride = 15 * sizeof(pixel);

    declare_func(void, pixel *dst, ptrdiff_t dst_stride, const pixel *src,
                 ptrdiff_t src_stride, const int16_t *abcd, int mx, int my
                 HIGHBD_DECL_SUFFIX);

    if (check_func(c->warp8x8, "warp_8x8_%dbpc", BITDEPTH)) {
        const int mx = (rnd() & 0x1fff) - 0x800;
        const int my = (rnd() & 0x1fff) - 0x800;
#if BITDEPTH == 16
        const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
        const int bitdepth_max = 0xff;
#endif

        for (int i = 0; i < 4; i++)
            abcd[i] = (rnd() & 0x1fff) - 0x800;

        for (int i = 0; i < 15 * 15; i++)
            src_buf[i] = rnd() & bitdepth_max;

        call_ref(c_dst, dst_stride, src, src_stride, abcd, mx, my HIGHBD_TAIL_SUFFIX);
        call_new(a_dst, dst_stride, src, src_stride, abcd, mx, my HIGHBD_TAIL_SUFFIX);
        if (memcmp(c_dst, a_dst, 8 * 8 * sizeof(*c_dst)))
            fail();

        bench_new(a_dst, dst_stride, src, src_stride, abcd, mx, my HIGHBD_TAIL_SUFFIX);
    }
    report("warp8x8");
}

static void check_warp8x8t(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(pixel, src_buf, 15 * 15,);
    ALIGN_STK_32(int16_t, c_tmp,    8 *  8,);
    ALIGN_STK_32(int16_t, a_tmp,    8 *  8,);
    int16_t abcd[4];
    const pixel *src = src_buf + 15 * 3 + 3;
    const ptrdiff_t src_stride = 15 * sizeof(pixel);

    declare_func(void, int16_t *tmp, ptrdiff_t tmp_stride, const pixel *src,
                 ptrdiff_t src_stride, const int16_t *abcd, int mx, int my
                 HIGHBD_DECL_SUFFIX);

    if (check_func(c->warp8x8t, "warp_8x8t_%dbpc", BITDEPTH)) {
        const int mx = (rnd() & 0x1fff) - 0x800;
        const int my = (rnd() & 0x1fff) - 0x800;
#if BITDEPTH == 16
        const int bitdepth_max = rnd() & 1 ? 0x3ff : 0xfff;
#else
        const int bitdepth_max = 0xff;
#endif

        for (int i = 0; i < 4; i++)
            abcd[i] = (rnd() & 0x1fff) - 0x800;

        for (int i = 0; i < 15 * 15; i++)
            src_buf[i] = rnd() & bitdepth_max;

        call_ref(c_tmp, 8, src, src_stride, abcd, mx, my HIGHBD_TAIL_SUFFIX);
        call_new(a_tmp, 8, src, src_stride, abcd, mx, my HIGHBD_TAIL_SUFFIX);
        if (memcmp(c_tmp, a_tmp, 8 * 8 * sizeof(*c_tmp)))
            fail();

        bench_new(a_tmp, 8, src, src_stride, abcd, mx, my HIGHBD_TAIL_SUFFIX);
    }
    report("warp8x8t");
}

static int cmp2d(const pixel *a, const pixel *b, const ptrdiff_t stride,
                 const int w, const int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            if (a[x] != b[x]) return (y << 16) | x;
        a += PXSTRIDE(stride);
        b += PXSTRIDE(stride);
    }
    return -1;
}

enum EdgeFlags {
    HAVE_TOP = 1,
    HAVE_BOTTOM = 2,
    HAVE_LEFT = 4,
    HAVE_RIGHT = 8,
};

static void random_offset_for_edge(int *const x, int *const y,
                                   const int bw, const int bh,
                                   int *const iw, int *const ih,
                                   const enum EdgeFlags edge)
{
#define set_off(edge1, edge2, pos, dim) \
    *i##dim = edge & (HAVE_##edge1 | HAVE_##edge2) ? 160 : 1 + (rnd() % (b##dim - 2)); \
    switch (edge & (HAVE_##edge1 | HAVE_##edge2)) { \
    case HAVE_##edge1 | HAVE_##edge2: \
        assert(b##dim <= *i##dim); \
        *pos = rnd() % (*i##dim - b##dim + 1); \
        break; \
    case HAVE_##edge1: \
        *pos = (*i##dim - b##dim) + 1 + (rnd() % (b##dim - 1)); \
        break; \
    case HAVE_##edge2: \
        *pos = -(1 + (rnd() % (b##dim - 1))); \
        break; \
    case 0: \
        assert(b##dim - 1 > *i##dim); \
        *pos = -(1 + (rnd() % (b##dim - *i##dim - 1))); \
        break; \
    }
    set_off(LEFT, RIGHT, x, w);
    set_off(TOP, BOTTOM, y, h);
}

static void check_emuedge(Dav1dMCDSPContext *const c) {
    ALIGN_STK_32(pixel, c_dst, 135 * 192,);
    ALIGN_STK_32(pixel, a_dst, 135 * 192,);
    ALIGN_STK_32(pixel, src,   160 * 160,);

    for (int i = 0; i < 160 * 160; i++)
        src[i] = rnd() & ((1U << BITDEPTH) - 1);

    declare_func(void, intptr_t bw, intptr_t bh, intptr_t iw, intptr_t ih,
                 intptr_t x, intptr_t y,
                 pixel *dst, ptrdiff_t dst_stride,
                 const pixel *src, ptrdiff_t src_stride);

    int x, y, iw, ih;
    for (int w = 4; w <= 128; w <<= 1)
        if (check_func(c->emu_edge, "emu_edge_w%d_%dbpc", w, BITDEPTH)) {
            for (int h = imax(w / 4, 4); h <= imin(w * 4, 128); h <<= 1) {
                // we skip 0xf, since it implies that we don't need emu_edge
                for (enum EdgeFlags edge = 0; edge < 0xf; edge++) {
                    const int bw = w + (rnd() & 7);
                    const int bh = h + (rnd() & 7);
                    random_offset_for_edge(&x, &y, bw, bh, &iw, &ih, edge);
                    call_ref(bw, bh, iw, ih, x, y,
                             c_dst, 192 * sizeof(pixel), src, 160 * sizeof(pixel));
                    call_new(bw, bh, iw, ih, x, y,
                             a_dst, 192 * sizeof(pixel), src, 160 * sizeof(pixel));
                    const int res = cmp2d(c_dst, a_dst, 192 * sizeof(pixel), bw, bh);
                    if (res != -1) fail();
                }
            }
            for (enum EdgeFlags edge = 1; edge < 0xf; edge <<= 1) {
                random_offset_for_edge(&x, &y, w + 7, w + 7, &iw, &ih, edge);
                bench_new(w + 7, w + 7, iw, ih, x, y,
                          a_dst, 192 * sizeof(pixel), src, 160 * sizeof(pixel));
            }
        }
    report("emu_edge");
}

void bitfn(checkasm_check_mc)(void) {
    Dav1dMCDSPContext c;
    bitfn(dav1d_mc_dsp_init)(&c);

    check_mc(&c);
    check_mct(&c);
    check_avg(&c);
    check_w_avg(&c);
    check_mask(&c);
    check_w_mask(&c);
    check_blend(&c);
    check_blend_v(&c);
    check_blend_h(&c);
    check_warp8x8(&c);
    check_warp8x8t(&c);
    check_emuedge(&c);
}
