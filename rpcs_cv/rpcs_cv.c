/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief CV RPC server library
 *
 * The cv_* RPCs (see cv_rpc.x.m4) on top of ta_cv. TARPC_FUNC_STATIC()
 * binds an RPC to the function of the same name, so each RPC has a
 * plain C function first and the wrapper after it. The functions keep
 * the text-list results in a te_string and hand them to the RPC's
 * string fields; the scalar metrics go straight into the out struct.
 */

#define TE_LGR_USER     "RPC CV"

#include "te_config.h"

#include <string.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "rpc_server.h"

#include "ta_cv.h"

/* Hand a te_string result over to an RPC string field (never NULL). */
static char *
take(te_string *str)
{
    return str->ptr != NULL ? str->ptr : TE_STRDUP("");
}

/* ---- available ---------------------------------------------------- */

static te_errno
cv_available(unsigned int *features, char **version)
{
    te_string version_s = TE_STRING_INIT;
    te_bool ok = ta_cv_available(features, &version_s);

    *version = take(&version_s);

    return ok ? 0 : TE_RC(TE_TA_UNIX, TE_EOPNOTSUPP);
}

TARPC_FUNC_STATIC(cv_available, {},
{
    unsigned int features = 0;

    MAKE_CALL(out->retval = func(&features, &out->cv_version));
    out->features = features;
    out->common.errno_changed = false;
})

/* ---- image_info --------------------------------------------------- */

static te_errno
cv_image_info(const char *path, int *width, int *height, int *channels,
              int *depth, char **format)
{
    te_string format_s = TE_STRING_INIT;
    te_errno rc = ta_cv_image_info(path, width, height, channels, depth,
                                   &format_s);

    *format = take(&format_s);

    return rc;
}

TARPC_FUNC_STATIC(cv_image_info, {},
{
    int width = 0, height = 0, channels = 0, depth = 0;

    MAKE_CALL(out->retval = func(in->path, &width, &height, &channels,
                                 &depth, &out->format));
    out->width = width;
    out->height = height;
    out->channels = channels;
    out->depth = depth;
    out->common.errno_changed = false;
})

/* ---- compare ------------------------------------------------------ */

static te_errno
cv_compare(const char *path_a, const char *path_b, int tolerance,
           te_bool want_diff, unsigned int *diff_pixels,
           unsigned int *total_pixels, int *max_delta, int *mean_delta_milli,
           int *mse_milli, int *psnr_milli, int *ssim_milli, int *bbox_x,
           int *bbox_y, int *bbox_w, int *bbox_h, char **diff_path)
{
    te_string diff_s = TE_STRING_INIT;
    te_errno rc = ta_cv_compare(path_a, path_b, tolerance, want_diff,
                                diff_pixels, total_pixels, max_delta,
                                mean_delta_milli, mse_milli, psnr_milli,
                                ssim_milli, bbox_x, bbox_y, bbox_w, bbox_h,
                                &diff_s);

    *diff_path = take(&diff_s);

    return rc;
}

TARPC_FUNC_STATIC(cv_compare, {},
{
    unsigned int diff_pixels = 0, total_pixels = 0;
    int max_delta = 0, mean_delta = 0, mse = 0, psnr = 0, ssim = 0;
    int bx = -1, by = -1, bw = -1, bh = -1;

    MAKE_CALL(out->retval = func(in->path_a, in->path_b, in->tolerance,
                                 in->want_diff, &diff_pixels, &total_pixels,
                                 &max_delta, &mean_delta, &mse, &psnr, &ssim,
                                 &bx, &by, &bw, &bh, &out->diff_path));
    out->diff_pixels = diff_pixels;
    out->total_pixels = total_pixels;
    out->max_delta = max_delta;
    out->mean_delta_milli = mean_delta;
    out->mse_milli = mse;
    out->psnr_milli = psnr;
    out->ssim_milli = ssim;
    out->bbox_x = bx;
    out->bbox_y = by;
    out->bbox_w = bw;
    out->bbox_h = bh;
    out->common.errno_changed = false;
})

/* ---- match -------------------------------------------------------- */

static te_errno
cv_match(const char *image, const char *templ, int method,
         int threshold_milli, int max_matches, int *best_milli, char **matches)
{
    te_string matches_s = TE_STRING_INIT;
    te_errno rc = ta_cv_match(image, templ, method, threshold_milli,
                              max_matches, best_milli, &matches_s);

    *matches = take(&matches_s);

    return rc;
}

TARPC_FUNC_STATIC(cv_match, {},
{
    int best = 0;

    MAKE_CALL(out->retval = func(in->image, in->templ, in->method,
                                 in->threshold_milli, in->max_matches,
                                 &best, &out->matches));
    out->best_milli = best;
    out->common.errno_changed = false;
})

/* ---- region ------------------------------------------------------- */

static te_errno
cv_region(const char *image, int x, int y, int w, int h, te_bool use_color,
          int h_lo, int s_lo, int v_lo, int h_hi, int s_hi, int v_hi,
          int *mean_b, int *mean_g, int *mean_r, int *min_v, int *max_v,
          int *in_range_milli, int *bbox_x, int *bbox_y, int *bbox_w,
          int *bbox_h)
{
    return ta_cv_region(image, x, y, w, h, use_color, h_lo, s_lo, v_lo, h_hi,
                        s_hi, v_hi, mean_b, mean_g, mean_r, min_v, max_v,
                        in_range_milli, bbox_x, bbox_y, bbox_w, bbox_h);
}

TARPC_FUNC_STATIC(cv_region, {},
{
    int mean_b = 0, mean_g = 0, mean_r = 0, min_v = 0, max_v = 0, inr = 0;
    int bx = -1, by = -1, bw = -1, bh = -1;

    MAKE_CALL(out->retval = func(in->image, in->x, in->y, in->w, in->h,
                                 in->use_color, in->h_lo, in->s_lo, in->v_lo,
                                 in->h_hi, in->s_hi, in->v_hi, &mean_b,
                                 &mean_g, &mean_r, &min_v, &max_v, &inr,
                                 &bx, &by, &bw, &bh));
    out->mean_b = mean_b;
    out->mean_g = mean_g;
    out->mean_r = mean_r;
    out->min_v = min_v;
    out->max_v = max_v;
    out->in_range_milli = inr;
    out->bbox_x = bx;
    out->bbox_y = by;
    out->bbox_w = bw;
    out->bbox_h = bh;
    out->common.errno_changed = false;
})

/* ---- ocr ---------------------------------------------------------- */

static te_errno
cv_ocr(const char *image, int x, int y, int w, int h, const char *lang,
       int *mean_conf, char **text, char **words)
{
    te_string text_s = TE_STRING_INIT;
    te_string words_s = TE_STRING_INIT;
    te_errno rc = ta_cv_ocr(image, x, y, w, h, lang, mean_conf, &text_s,
                            &words_s);

    *text = take(&text_s);
    *words = take(&words_s);

    return rc;
}

TARPC_FUNC_STATIC(cv_ocr, {},
{
    int mean_conf = 0;

    MAKE_CALL(out->retval = func(in->image, in->x, in->y, in->w, in->h,
                                 in->lang, &mean_conf, &out->text,
                                 &out->words));
    out->mean_conf = mean_conf;
    out->common.errno_changed = false;
})

/* ---- decode ------------------------------------------------------- */

static te_errno
cv_decode(const char *image, int kind, int *count, char **codes)
{
    te_string codes_s = TE_STRING_INIT;
    te_errno rc = ta_cv_decode(image, kind, count, &codes_s);

    *codes = take(&codes_s);

    return rc;
}

TARPC_FUNC_STATIC(cv_decode, {},
{
    int count = 0;

    MAKE_CALL(out->retval = func(in->image, in->kind, &count, &out->codes));
    out->count = count;
    out->common.errno_changed = false;
})
