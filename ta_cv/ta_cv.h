/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Agent-side computer vision
 *
 * Analysing an image in the agent's own process through OpenCV's C++
 * library API (@c opencv2/..., @c -lopencv_*) - the library is linked
 * and called, nothing is spawned. The agent and its RPC server both
 * link this; the RPCs (see cv_rpc.x.m4) are thin wrappers over these
 * functions.
 *
 * OpenCV is C++, so all the vision work lives in one C++ translation
 * unit, ta_cv_opencv.cpp, behind a plain-C seam: those @c *_raw
 * functions include no TE headers (TE's C headers are not C++-safe),
 * take plain C scalars and @c char** out-strings (heap, caller frees),
 * and return @c 0 ok / @c 1 bad image / @c 2 engine failure / @c 3 the
 * feature is not compiled in. This file's @c ta_cv_* functions, defined
 * in ta_cv.c (C), adapt that seam to the @c te_string / @c te_errno
 * world that rpcs_cv wants.
 *
 * XDR has no portable float, so every real-valued metric is a scaled
 * integer in milli-units (value * 1000, named @c *_milli); the TAPI on
 * the engine turns it back into a double. Lists come back as
 * newline-separated records, one per line, the same shape tsf-smt uses.
 */

#ifndef __TA_CV_H__
#define __TA_CV_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Optional backends, reported by ta_cv_available(). Wire values. */
enum {
    TA_CV_FEAT_OCR     = 1,  /**< OCR, via libtesseract. */
    TA_CV_FEAT_QR      = 2,  /**< QR decode, via cv::QRCodeDetector. */
    TA_CV_FEAT_BARCODE = 4,  /**< 1-D barcode decode, via cv::barcode. */
};

/** What symbols cv_decode()/ta_cv_decode() looks for. Wire values. */
enum {
    TA_CV_DECODE_QR      = 1, /**< QR codes only. */
    TA_CV_DECODE_BARCODE = 2, /**< 1-D barcodes only. */
    TA_CV_DECODE_ANY     = 3, /**< Both. */
};

/*
 * ---- TE-typed surface (ta_cv.c) ------------------------------------
 *
 * What rpcs_cv calls. Each adapts the raw C++ seam below: a heap
 * out-string becomes an appended te_string, and the raw 0/1/2/3 return
 * becomes 0 / TE_ESHCMD / TE_EFAIL / TE_EOPNOTSUPP.
 */

/**
 * Is OpenCV usable in this agent, and which optional backends are in?
 *
 * @param[out] features  Bitmask of @c TA_CV_FEAT_*.
 * @param[out] version   OpenCV version string.
 *
 * @return @c true when a trivial operation could be run.
 */
extern te_bool ta_cv_available(unsigned int *features, te_string *version);

/** Dimensions and encoding of an image file. */
extern te_errno ta_cv_image_info(const char *path, int *width, int *height,
                                 int *channels, int *depth,
                                 te_string *format);

/** Pixel difference of two equal-sized images; see cv_rpc.x.m4. */
extern te_errno ta_cv_compare(const char *path_a, const char *path_b,
                              int tolerance, te_bool want_diff,
                              unsigned int *diff_pixels,
                              unsigned int *total_pixels, int *max_delta,
                              int *mean_delta_milli, int *mse_milli,
                              int *psnr_milli, int *ssim_milli, int *bbox_x,
                              int *bbox_y, int *bbox_w, int *bbox_h,
                              te_string *diff_path);

/** Find a template inside an image; matches as text, see cv_rpc.x.m4. */
extern te_errno ta_cv_match(const char *image, const char *templ, int method,
                            int threshold_milli, int max_matches,
                            int *best_milli, te_string *matches);

/** Rectangle statistics and HSV in-range share; see cv_rpc.x.m4. */
extern te_errno ta_cv_region(const char *image, int x, int y, int w, int h,
                             te_bool use_color, int h_lo, int s_lo, int v_lo,
                             int h_hi, int s_hi, int v_hi, int *mean_b,
                             int *mean_g, int *mean_r, int *min_v, int *max_v,
                             int *in_range_milli, int *bbox_x, int *bbox_y,
                             int *bbox_w, int *bbox_h);

/** Recognise text; TE_EOPNOTSUPP when OCR is not compiled in. */
extern te_errno ta_cv_ocr(const char *image, int x, int y, int w, int h,
                          const char *lang, int *mean_conf, te_string *text,
                          te_string *words);

/** Decode QR/barcodes; TE_EOPNOTSUPP when the backend is not in. */
extern te_errno ta_cv_decode(const char *image, int kind, int *count,
                             te_string *codes);

/*
 * ---- Raw C++ seam (ta_cv_opencv.cpp) -------------------------------
 *
 * Plain C: no TE headers, int flags, char** out-strings (heap, caller
 * frees), return 0 ok / 1 bad image / 2 engine failure / 3 unsupported.
 */

extern int ta_cv_available_raw(unsigned int *features, char **version);

extern int ta_cv_image_info_raw(const char *path, int *width, int *height,
                                int *channels, int *depth, char **format);

extern int ta_cv_compare_raw(const char *path_a, const char *path_b,
                             int tolerance, int want_diff,
                             unsigned int *diff_pixels,
                             unsigned int *total_pixels, int *max_delta,
                             int *mean_delta_milli, int *mse_milli,
                             int *psnr_milli, int *ssim_milli, int *bbox_x,
                             int *bbox_y, int *bbox_w, int *bbox_h,
                             char **diff_path);

extern int ta_cv_match_raw(const char *image, const char *templ, int method,
                           int threshold_milli, int max_matches,
                           int *best_milli, char **matches);

extern int ta_cv_region_raw(const char *image, int x, int y, int w, int h,
                            int use_color, int h_lo, int s_lo, int v_lo,
                            int h_hi, int s_hi, int v_hi, int *mean_b,
                            int *mean_g, int *mean_r, int *min_v, int *max_v,
                            int *in_range_milli, int *bbox_x, int *bbox_y,
                            int *bbox_w, int *bbox_h);

extern int ta_cv_ocr_raw(const char *image, int x, int y, int w, int h,
                         const char *lang, int *mean_conf, char **text,
                         char **words);

extern int ta_cv_decode_raw(const char *image, int kind, int *count,
                            char **codes);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TA_CV_H__ */
