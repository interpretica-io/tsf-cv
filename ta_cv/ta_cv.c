/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Agent-side computer vision: the C seam over the OpenCV backend
 *
 * The vision work is all in ta_cv_opencv.cpp (OpenCV is C++). This unit
 * is the C adapter the RPC server wants: it calls the plain-C @c *_raw
 * seam, moves each heap out-string into a @c te_string, and maps the
 * seam's @c 0/1/2/3 return to a @c te_errno. Nothing here includes an
 * OpenCV header.
 */

#define TE_LGR_USER     "TA CV"

#include "te_config.h"

#include <stdlib.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "logger_api.h"

#include "ta_cv.h"

/** Map the raw seam's 0/1/2/3 return to a te_errno. */
static te_errno
rc_from_raw(int raw)
{
    switch (raw)
    {
        case 0:
            return 0;
        case 1:
            return TE_RC(TE_TA_UNIX, TE_ESHCMD);     /* unreadable image */
        case 3:
            return TE_RC(TE_TA_UNIX, TE_EOPNOTSUPP); /* feature not built in */
        default:
            return TE_RC(TE_TA_UNIX, TE_EFAIL);
    }
}

/** Move a heap string from the raw seam into @p dst, then free it. */
static void
take(te_string *dst, char *heap)
{
    if (heap == NULL)
        return;
    if (dst != NULL)
        te_string_append(dst, "%s", heap);
    free(heap);
}

/* See description in ta_cv.h */
te_bool
ta_cv_available(unsigned int *features, te_string *version)
{
    char *v = NULL;
    unsigned int feats = 0;
    int raw = ta_cv_available_raw(&feats, &v);

    if (features != NULL)
        *features = feats;
    take(version, v);

    return raw == 0;
}

/* See description in ta_cv.h */
te_errno
ta_cv_image_info(const char *path, int *width, int *height, int *channels,
                 int *depth, te_string *format)
{
    char *fmt = NULL;
    int raw = ta_cv_image_info_raw(path, width, height, channels, depth, &fmt);

    take(format, fmt);

    return rc_from_raw(raw);
}

/* See description in ta_cv.h */
te_errno
ta_cv_compare(const char *path_a, const char *path_b, int tolerance,
              te_bool want_diff, unsigned int *diff_pixels,
              unsigned int *total_pixels, int *max_delta, int *mean_delta_milli,
              int *mse_milli, int *psnr_milli, int *ssim_milli, int *bbox_x,
              int *bbox_y, int *bbox_w, int *bbox_h, te_string *diff_path)
{
    char *dpath = NULL;
    int raw = ta_cv_compare_raw(path_a, path_b, tolerance, want_diff,
                                diff_pixels, total_pixels, max_delta,
                                mean_delta_milli, mse_milli, psnr_milli,
                                ssim_milli, bbox_x, bbox_y, bbox_w, bbox_h,
                                &dpath);

    take(diff_path, dpath);

    return rc_from_raw(raw);
}

/* See description in ta_cv.h */
te_errno
ta_cv_match(const char *image, const char *templ, int method,
            int threshold_milli, int max_matches, int *best_milli,
            te_string *matches)
{
    char *m = NULL;
    int raw = ta_cv_match_raw(image, templ, method, threshold_milli,
                              max_matches, best_milli, &m);

    take(matches, m);

    return rc_from_raw(raw);
}

/* See description in ta_cv.h */
te_errno
ta_cv_region(const char *image, int x, int y, int w, int h, te_bool use_color,
             int h_lo, int s_lo, int v_lo, int h_hi, int s_hi, int v_hi,
             int *mean_b, int *mean_g, int *mean_r, int *min_v, int *max_v,
             int *in_range_milli, int *bbox_x, int *bbox_y, int *bbox_w,
             int *bbox_h)
{
    int raw = ta_cv_region_raw(image, x, y, w, h, use_color, h_lo, s_lo, v_lo,
                               h_hi, s_hi, v_hi, mean_b, mean_g, mean_r, min_v,
                               max_v, in_range_milli, bbox_x, bbox_y, bbox_w,
                               bbox_h);

    return rc_from_raw(raw);
}

/* See description in ta_cv.h */
te_errno
ta_cv_ocr(const char *image, int x, int y, int w, int h, const char *lang,
          int *mean_conf, te_string *text, te_string *words)
{
    char *t = NULL;
    char *wds = NULL;
    int raw = ta_cv_ocr_raw(image, x, y, w, h, lang, mean_conf, &t, &wds);

    take(text, t);
    take(words, wds);

    return rc_from_raw(raw);
}

/* See description in ta_cv.h */
te_errno
ta_cv_decode(const char *image, int kind, int *count, te_string *codes)
{
    char *c = NULL;
    int raw = ta_cv_decode_raw(image, kind, count, &c);

    take(codes, c);

    return rc_from_raw(raw);
}
