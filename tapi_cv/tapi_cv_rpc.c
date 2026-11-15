/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief CV TAPI: RPC client wrappers
 *
 * The rcf_rpc_call() boilerplate behind tapi_cv. The RPCs return
 * te_errno; an RPC transport failure is mapped to TE_ECORRUPTED.
 */

#define TE_LGR_USER     "TAPI CV RPC"

#include "te_config.h"

#include <string.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "logger_api.h"
#include "tapi_rpc_internal.h"
#include "tarpc.h"

#include "tapi_cv_rpc.h"

#define CHECK_RPC_ERRNO_UNCHANGED(_func, _var) \
    CHECK_RETVAL_VAR_ERR_COND(_func, _var, false,                    \
                              TE_RC(TE_TAPI, TE_ECORRUPTED), false)

/* Append an RPC string result, when there is one. */
static void
take_string(te_string *dst, const char *src)
{
    if (dst != NULL && src != NULL)
        te_string_append(dst, "%s", src);
}

/* Store a scalar through a possibly-NULL pointer. */
static void
put(int *p, int v)
{
    if (p != NULL)
        *p = v;
}

/* See description in tapi_cv_rpc.h */
te_errno
rpc_cv_available(rcf_rpc_server *rpcs, unsigned int *features,
                 te_string *version)
{
    tarpc_cv_available_in in;
    tarpc_cv_available_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    rcf_rpc_call(rpcs, "cv_available", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(cv_available, out.retval);
    TAPI_RPC_LOG(rpcs, cv_available, "", "%r feats=0x%x", out.retval,
                 out.features);

    if (out.retval == 0 && features != NULL)
        *features = out.features;
    take_string(version, out.cv_version);

    RETVAL_TE_ERRNO(cv_available, out.retval);
}

/* See description in tapi_cv_rpc.h */
te_errno
rpc_cv_image_info(rcf_rpc_server *rpcs, const char *path, int *width,
                  int *height, int *channels, int *depth, te_string *format)
{
    tarpc_cv_image_info_in in;
    tarpc_cv_image_info_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.path = (char *)(path != NULL ? path : "");

    rcf_rpc_call(rpcs, "cv_image_info", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(cv_image_info, out.retval);
    TAPI_RPC_LOG(rpcs, cv_image_info, "%s", "%r %dx%d", path, out.retval,
                 out.width, out.height);

    if (out.retval == 0)
    {
        put(width, out.width);
        put(height, out.height);
        put(channels, out.channels);
        put(depth, out.depth);
        take_string(format, out.format);
    }

    RETVAL_TE_ERRNO(cv_image_info, out.retval);
}

/* See description in tapi_cv_rpc.h */
te_errno
rpc_cv_compare(rcf_rpc_server *rpcs, const char *path_a, const char *path_b,
               int tolerance, te_bool want_diff, unsigned int *diff_pixels,
               unsigned int *total_pixels, int *max_delta,
               int *mean_delta_milli, int *mse_milli, int *psnr_milli,
               int *ssim_milli, int *bbox_x, int *bbox_y, int *bbox_w,
               int *bbox_h, te_string *diff_path)
{
    tarpc_cv_compare_in in;
    tarpc_cv_compare_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.path_a = (char *)(path_a != NULL ? path_a : "");
    in.path_b = (char *)(path_b != NULL ? path_b : "");
    in.tolerance = tolerance;
    in.want_diff = want_diff;

    rcf_rpc_call(rpcs, "cv_compare", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(cv_compare, out.retval);
    TAPI_RPC_LOG(rpcs, cv_compare, "tol=%d", "%r diff=%u ssim_milli=%d",
                 tolerance, out.retval, out.diff_pixels, out.ssim_milli);

    if (out.retval == 0)
    {
        if (diff_pixels != NULL)
            *diff_pixels = out.diff_pixels;
        if (total_pixels != NULL)
            *total_pixels = out.total_pixels;
        put(max_delta, out.max_delta);
        put(mean_delta_milli, out.mean_delta_milli);
        put(mse_milli, out.mse_milli);
        put(psnr_milli, out.psnr_milli);
        put(ssim_milli, out.ssim_milli);
        put(bbox_x, out.bbox_x);
        put(bbox_y, out.bbox_y);
        put(bbox_w, out.bbox_w);
        put(bbox_h, out.bbox_h);
        take_string(diff_path, out.diff_path);
    }

    RETVAL_TE_ERRNO(cv_compare, out.retval);
}

/* See description in tapi_cv_rpc.h */
te_errno
rpc_cv_match(rcf_rpc_server *rpcs, const char *image, const char *templ,
             int method, int threshold_milli, int max_matches, int *best_milli,
             te_string *matches)
{
    tarpc_cv_match_in in;
    tarpc_cv_match_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.image = (char *)(image != NULL ? image : "");
    in.templ = (char *)(templ != NULL ? templ : "");
    in.method = method;
    in.threshold_milli = threshold_milli;
    in.max_matches = max_matches;

    rcf_rpc_call(rpcs, "cv_match", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(cv_match, out.retval);
    TAPI_RPC_LOG(rpcs, cv_match, "method=%d", "%r best_milli=%d", method,
                 out.retval, out.best_milli);

    if (out.retval == 0)
    {
        put(best_milli, out.best_milli);
        take_string(matches, out.matches);
    }

    RETVAL_TE_ERRNO(cv_match, out.retval);
}

/* See description in tapi_cv_rpc.h */
te_errno
rpc_cv_region(rcf_rpc_server *rpcs, const char *image, int x, int y, int w,
              int h, te_bool use_color, int h_lo, int s_lo, int v_lo, int h_hi,
              int s_hi, int v_hi, int *mean_b, int *mean_g, int *mean_r,
              int *min_v, int *max_v, int *in_range_milli, int *bbox_x,
              int *bbox_y, int *bbox_w, int *bbox_h)
{
    tarpc_cv_region_in in;
    tarpc_cv_region_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.image = (char *)(image != NULL ? image : "");
    in.x = x;
    in.y = y;
    in.w = w;
    in.h = h;
    in.use_color = use_color;
    in.h_lo = h_lo;
    in.s_lo = s_lo;
    in.v_lo = v_lo;
    in.h_hi = h_hi;
    in.s_hi = s_hi;
    in.v_hi = v_hi;

    rcf_rpc_call(rpcs, "cv_region", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(cv_region, out.retval);
    TAPI_RPC_LOG(rpcs, cv_region, "", "%r in_range_milli=%d", out.retval,
                 out.in_range_milli);

    if (out.retval == 0)
    {
        put(mean_b, out.mean_b);
        put(mean_g, out.mean_g);
        put(mean_r, out.mean_r);
        put(min_v, out.min_v);
        put(max_v, out.max_v);
        put(in_range_milli, out.in_range_milli);
        put(bbox_x, out.bbox_x);
        put(bbox_y, out.bbox_y);
        put(bbox_w, out.bbox_w);
        put(bbox_h, out.bbox_h);
    }

    RETVAL_TE_ERRNO(cv_region, out.retval);
}

/* See description in tapi_cv_rpc.h */
te_errno
rpc_cv_ocr(rcf_rpc_server *rpcs, const char *image, int x, int y, int w, int h,
           const char *lang, int *mean_conf, te_string *text, te_string *words)
{
    tarpc_cv_ocr_in in;
    tarpc_cv_ocr_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.image = (char *)(image != NULL ? image : "");
    in.x = x;
    in.y = y;
    in.w = w;
    in.h = h;
    in.lang = (char *)(lang != NULL ? lang : "");

    rcf_rpc_call(rpcs, "cv_ocr", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(cv_ocr, out.retval);
    TAPI_RPC_LOG(rpcs, cv_ocr, "", "%r conf=%d", out.retval, out.mean_conf);

    if (out.retval == 0)
    {
        put(mean_conf, out.mean_conf);
        take_string(text, out.text);
        take_string(words, out.words);
    }

    RETVAL_TE_ERRNO(cv_ocr, out.retval);
}

/* See description in tapi_cv_rpc.h */
te_errno
rpc_cv_decode(rcf_rpc_server *rpcs, const char *image, int kind, int *count,
              te_string *codes)
{
    tarpc_cv_decode_in in;
    tarpc_cv_decode_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    in.image = (char *)(image != NULL ? image : "");
    in.kind = kind;

    rcf_rpc_call(rpcs, "cv_decode", &in, &out);
    CHECK_RPC_ERRNO_UNCHANGED(cv_decode, out.retval);
    TAPI_RPC_LOG(rpcs, cv_decode, "kind=%d", "%r count=%d", kind, out.retval,
                 out.count);

    if (out.retval == 0)
    {
        put(count, out.count);
        take_string(codes, out.codes);
    }

    RETVAL_TE_ERRNO(cv_decode, out.retval);
}
