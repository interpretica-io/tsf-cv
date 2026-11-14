/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief CV TAPI: RPC client wrappers
 *
 * Client wrappers of the cv_* RPCs, see cv_rpc.x.m4. Tests use
 * tapi_cv.h; these are the calls behind it, one per RPC. Scalars come
 * back by pointer, text lists in a te_string the engine side parses;
 * real-valued metrics are the RPC's @c *_milli integers, left as they
 * are on the wire here and divided by the TAPI above.
 */

#ifndef __TAPI_CV_RPC_H__
#define __TAPI_CV_RPC_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "rcf_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** cv_available(): features bitmask and version; 0 when usable. */
extern te_errno rpc_cv_available(rcf_rpc_server *rpcs, unsigned int *features,
                                 te_string *version);

/** cv_image_info(): dimensions and encoding of an image file. */
extern te_errno rpc_cv_image_info(rcf_rpc_server *rpcs, const char *path,
                                  int *width, int *height, int *channels,
                                  int *depth, te_string *format);

/** cv_compare(): pixel difference; metrics in milli-units. */
extern te_errno rpc_cv_compare(rcf_rpc_server *rpcs, const char *path_a,
                               const char *path_b, int tolerance,
                               te_bool want_diff, unsigned int *diff_pixels,
                               unsigned int *total_pixels, int *max_delta,
                               int *mean_delta_milli, int *mse_milli,
                               int *psnr_milli, int *ssim_milli, int *bbox_x,
                               int *bbox_y, int *bbox_w, int *bbox_h,
                               te_string *diff_path);

/** cv_match(): template matches; best in milli, matches as text. */
extern te_errno rpc_cv_match(rcf_rpc_server *rpcs, const char *image,
                             const char *templ, int method,
                             int threshold_milli, int max_matches,
                             int *best_milli, te_string *matches);

/** cv_region(): rectangle statistics and HSV in-range share. */
extern te_errno rpc_cv_region(rcf_rpc_server *rpcs, const char *image, int x,
                              int y, int w, int h, te_bool use_color, int h_lo,
                              int s_lo, int v_lo, int h_hi, int s_hi, int v_hi,
                              int *mean_b, int *mean_g, int *mean_r, int *min_v,
                              int *max_v, int *in_range_milli, int *bbox_x,
                              int *bbox_y, int *bbox_w, int *bbox_h);

/** cv_ocr(): recognised text, words as text; TE_EOPNOTSUPP when absent. */
extern te_errno rpc_cv_ocr(rcf_rpc_server *rpcs, const char *image, int x,
                           int y, int w, int h, const char *lang,
                           int *mean_conf, te_string *text, te_string *words);

/** cv_decode(): decoded codes as text; TE_EOPNOTSUPP when absent. */
extern te_errno rpc_cv_decode(rcf_rpc_server *rpcs, const char *image, int kind,
                              int *count, te_string *codes);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TAPI_CV_RPC_H__ */
