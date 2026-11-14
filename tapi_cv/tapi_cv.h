/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Computer vision from a test
 *
 * @defgroup tapi_cv Computer vision (tapi_cv)
 * @{
 *
 * Analysing an image on a Test Agent with OpenCV, and reading the
 * result back as structured data: the dimensions of an image, a pixel
 * difference of two of them (for a visual-regression check), finding a
 * template inside one (an icon or a button on a screen), the colour of
 * a region (is the indicator green?), the text in it (OCR), and the QR
 * or barcode symbols it carries.
 *
 * The image is analysed where it already is - on the agent - so this
 * pairs with whatever puts an image there: a screenshot from
 * tsf-playwright, tsf-android or tsf-apple, a framebuffer dump from
 * tsf-emu, a frame grabbed from a device. OpenCV runs in the agent's
 * RPC server process, called through its own C++ library API: the
 * library is linked into the agent, not run as a program, so nothing
 * of the image leaves the agent - there is no service, no key, no
 * network, unlike tsf-ai's vision-through-an-LLM.
 *
 * OCR (libtesseract) and 1-D barcode decoding (cv::barcode) are
 * optional backends; tapi_cv_supports() says whether the agent has
 * them, and the call returns @c TE_EOPNOTSUPP when it does not, so a
 * test skips rather than fails. QR decoding is always available.
 *
 * @code
 * tapi_cv_matches found;
 *
 * if (!tapi_cv_available(pco))
 *     TEST_SKIP("The agent has no OpenCV");
 *
 * // Is the OK button on the screenshot, and where?
 * CHECK_RC(tapi_cv_match(pco, "/tmp/screen.png", "/tmp/ok_button.png",
 *                        NULL, &found));
 * if (found.best < 0.9)
 *     TEST_VERDICT("OK button not found (best score %.2f)", found.best);
 * tapi_cv_matches_free(&found);
 * @endcode
 */

#ifndef __TAPI_CV_H__
#define __TAPI_CV_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "te_vector.h"
#include "rcf_rpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Optional backends, as a bitmask. The values are the wire values. */
typedef enum tapi_cv_feature {
    TAPI_CV_FEAT_OCR     = 1,  /**< OCR, via libtesseract. */
    TAPI_CV_FEAT_QR      = 2,  /**< QR decode, via cv::QRCodeDetector. */
    TAPI_CV_FEAT_BARCODE = 4,  /**< 1-D barcode decode, via cv::barcode. */
} tapi_cv_feature;

/**
 * Template-match method. It selects the family; the score tapi_cv
 * reports is always the normalised one, in @c [0,1] (CCOEFF may go
 * slightly negative), with higher meaning a better match whichever
 * method is chosen.
 */
typedef enum tapi_cv_match_method {
    TAPI_CV_SQDIFF        = 0,  /**< Squared difference. */
    TAPI_CV_SQDIFF_NORMED = 1,  /**< Squared difference (same family). */
    TAPI_CV_CCORR         = 2,  /**< Cross-correlation. */
    TAPI_CV_CCORR_NORMED  = 3,  /**< Cross-correlation (same family). */
    TAPI_CV_CCOEFF        = 4,  /**< Correlation coefficient. */
    TAPI_CV_CCOEFF_NORMED = 5,  /**< Correlation coefficient (same family). */
} tapi_cv_match_method;

/** What tapi_cv_decode() looks for. The values are the wire values. */
typedef enum tapi_cv_decode_kind {
    TAPI_CV_QR      = 1,  /**< QR codes only. */
    TAPI_CV_BARCODE = 2,  /**< 1-D barcodes only. */
    TAPI_CV_ANY     = 3,  /**< Both. */
} tapi_cv_decode_kind;

/** A rectangle in an image. Pass @c NULL to a call to mean the whole. */
typedef struct tapi_cv_rect {
    int x;      /**< Left. */
    int y;      /**< Top. */
    int w;      /**< Width. */
    int h;      /**< Height. */
} tapi_cv_rect;

/**
 * An inclusive HSV colour range for tapi_cv_region(). OpenCV's hue is
 * @c 0..179; saturation and value are @c 0..255.
 */
typedef struct tapi_cv_hsv_range {
    int h_lo, s_lo, v_lo;   /**< Lower bound. */
    int h_hi, s_hi, v_hi;   /**< Upper bound. */
} tapi_cv_hsv_range;

/** Dimensions and encoding of an image. */
typedef struct tapi_cv_image_info {
    int width;          /**< Pixels across. */
    int height;         /**< Pixels down. */
    int channels;       /**< Colour channels (1 grey, 3 BGR, 4 BGRA). */
    int depth;          /**< Bits per channel, e.g. @c 8. */
    char *format;       /**< Container name from the magic, e.g. @c "PNG". */
} tapi_cv_image_info;

/** Options for tapi_cv_compare(). Zeroed means exact, no heatmap. */
typedef struct tapi_cv_compare_opts {
    /** Per-channel delta at or below which a pixel counts as unchanged. */
    int tolerance;
    /** Write a diff heatmap on the agent and return its path. */
    bool want_diff;
} tapi_cv_compare_opts;

/** The result of tapi_cv_compare(). */
typedef struct tapi_cv_diff {
    unsigned int diff_pixels;   /**< Pixels differing by over the tolerance. */
    unsigned int total_pixels;  /**< Pixels compared. */
    int max_delta;              /**< Largest per-channel delta, 0..255. */
    double mean_delta;          /**< Mean per-channel absolute delta. */
    double mse;                 /**< Mean squared error. */
    double psnr;                /**< Peak signal-to-noise ratio, dB (capped 100). */
    double ssim;                /**< Structural similarity, -1..1 (1 identical). */
    int bbox_x, bbox_y, bbox_w, bbox_h; /**< Changed region, or all -1. */
    char *diff_path;            /**< Heatmap on the agent, or @c NULL. */
} tapi_cv_diff;

/** Options for tapi_cv_match(). Zeroed means CCOEFF_NORMED, 1 match. */
typedef struct tapi_cv_match_opts {
    tapi_cv_match_method method;    /**< Method; default CCOEFF_NORMED. */
    double threshold;               /**< Min score to report, 0..1. */
    int max_matches;                /**< Cap on matches; default 1. */
} tapi_cv_match_opts;

/** One template match. */
typedef struct tapi_cv_match {
    double score;           /**< Normalised score, higher is better. */
    int x, y, w, h;         /**< Where it matched (top-left and size). */
} tapi_cv_match;

/** The result of tapi_cv_match(). */
typedef struct tapi_cv_matches {
    double best;            /**< Best score anywhere, even below threshold. */
    te_vec matches;         /**< Vector of #tapi_cv_match, best first. */
} tapi_cv_matches;

/** The result of tapi_cv_region(). */
typedef struct tapi_cv_region_stats {
    int mean_b, mean_g, mean_r;         /**< Mean colour, 0..255. */
    int min_v, max_v;                   /**< Grey min and max, 0..255. */
    double in_range_fraction;           /**< Share inside the HSV range, 0..1. */
    int bbox_x, bbox_y, bbox_w, bbox_h; /**< In-range region, or all -1. */
} tapi_cv_region_stats;

/** One OCR word. */
typedef struct tapi_cv_word {
    int conf;               /**< Confidence, 0..100. */
    int x, y, w, h;         /**< Bounding box. */
    char *text;             /**< The word. */
} tapi_cv_word;

/** The result of tapi_cv_ocr(). */
typedef struct tapi_cv_text {
    char *text;             /**< All recognised text, with line breaks. */
    int mean_conf;          /**< Mean per-word confidence, 0..100. */
    te_vec words;           /**< Vector of #tapi_cv_word. */
} tapi_cv_text;

/** One decoded symbol. */
typedef struct tapi_cv_code {
    char *type;             /**< @c "QR" or a barcode type, e.g. @c "EAN_13". */
    char *data;             /**< The payload. */
    int x, y, w, h;         /**< Bounding box. */
} tapi_cv_code;

/** The result of tapi_cv_decode(). */
typedef struct tapi_cv_codes {
    te_vec codes;           /**< Vector of #tapi_cv_code. */
} tapi_cv_codes;

/**
 * Is OpenCV usable on the agent?
 *
 * @param rpcs      RPC server on the agent.
 *
 * @return @c true when a vision call could be attempted.
 */
extern bool tapi_cv_available(rcf_rpc_server *rpcs);

/**
 * Does the agent have an optional backend?
 *
 * @param rpcs      RPC server on the agent.
 * @param feature   The backend (OCR, QR, barcode).
 *
 * @return @c true when the backend is compiled into the agent.
 */
extern bool tapi_cv_supports(rcf_rpc_server *rpcs, tapi_cv_feature feature);

/**
 * Read an image's dimensions and encoding.
 *
 * @param[in]  rpcs     RPC server on the agent.
 * @param[in]  path     Image file on the agent.
 * @param[out] info     Result; release with tapi_cv_image_info_free().
 *
 * @return Status code; @c TE_ESHCMD when the image cannot be read.
 */
extern te_errno tapi_cv_image_info(rcf_rpc_server *rpcs, const char *path,
                                   tapi_cv_image_info *info);

/**
 * Compare two equal-sized images pixel by pixel.
 *
 * @param[in]  rpcs     RPC server on the agent.
 * @param[in]  path_a   First image on the agent.
 * @param[in]  path_b   Second image on the agent.
 * @param[in]  opts     Options, or @c NULL for exact and no heatmap.
 * @param[out] diff     Result; release with tapi_cv_diff_free().
 *
 * @return Status code. @c TE_EFAIL when the two images differ in size.
 */
extern te_errno tapi_cv_compare(rcf_rpc_server *rpcs, const char *path_a,
                                const char *path_b,
                                const tapi_cv_compare_opts *opts,
                                tapi_cv_diff *diff);

/**
 * Are two images identical within a per-channel tolerance?
 *
 * A convenience over tapi_cv_compare(): @c true when no pixel differs
 * by more than @p tolerance.
 *
 * @param rpcs      RPC server on the agent.
 * @param path_a    First image.
 * @param path_b    Second image.
 * @param tolerance Per-channel delta allowed (0 for exact).
 *
 * @return @c true when the images match within the tolerance.
 */
extern bool tapi_cv_images_equal(rcf_rpc_server *rpcs, const char *path_a,
                                 const char *path_b, int tolerance);

/**
 * Find a template image inside a larger image.
 *
 * @param[in]  rpcs     RPC server on the agent.
 * @param[in]  image    The image to search on the agent.
 * @param[in]  templ    The template on the agent.
 * @param[in]  opts     Options, or @c NULL for CCOEFF_NORMED and one match.
 * @param[out] matches  Result; release with tapi_cv_matches_free().
 *
 * @return Status code. @c TE_EFAIL when the template is larger than the
 *         image.
 */
extern te_errno tapi_cv_match(rcf_rpc_server *rpcs, const char *image,
                              const char *templ,
                              const tapi_cv_match_opts *opts,
                              tapi_cv_matches *matches);

/**
 * Is a template present, and where is the best match?
 *
 * A convenience over tapi_cv_match(): @c true when the best match
 * reaches @p threshold.
 *
 * @param[in]  rpcs       RPC server on the agent.
 * @param[in]  image      The image to search.
 * @param[in]  templ      The template.
 * @param[in]  threshold  Min normalised score, 0..1.
 * @param[out] best       The best match, or @c NULL to ignore it.
 *
 * @return @c true when the template was found at or above @p threshold.
 */
extern bool tapi_cv_find(rcf_rpc_server *rpcs, const char *image,
                         const char *templ, double threshold,
                         tapi_cv_match *best);

/**
 * Statistics over a rectangle, and the share inside an HSV range.
 *
 * @param[in]  rpcs     RPC server on the agent.
 * @param[in]  image    The image on the agent.
 * @param[in]  rect     The rectangle, or @c NULL for the whole image.
 * @param[in]  color    An HSV range to measure the in-range share and
 *                      box of, or @c NULL to skip the colour test.
 * @param[out] stats    Result (no heap; no free needed).
 *
 * @return Status code.
 */
extern te_errno tapi_cv_region(rcf_rpc_server *rpcs, const char *image,
                               const tapi_cv_rect *rect,
                               const tapi_cv_hsv_range *color,
                               tapi_cv_region_stats *stats);

/**
 * Recognise text in an image.
 *
 * @param[in]  rpcs     RPC server on the agent.
 * @param[in]  image    The image on the agent.
 * @param[in]  rect     A rectangle to read, or @c NULL for the whole.
 * @param[in]  lang     tesseract language, or @c NULL for @c "eng".
 * @param[out] result   Result; release with tapi_cv_text_free().
 *
 * @return Status code; @c TE_EOPNOTSUPP when OCR is not on the agent.
 */
extern te_errno tapi_cv_ocr(rcf_rpc_server *rpcs, const char *image,
                            const tapi_cv_rect *rect, const char *lang,
                            tapi_cv_text *result);

/**
 * Recognise text and return it as one string.
 *
 * A convenience over tapi_cv_ocr().
 *
 * @param rpcs      RPC server on the agent.
 * @param image     The image.
 * @param rect      A rectangle, or @c NULL for the whole image.
 * @param lang      Language, or @c NULL for @c "eng".
 *
 * @return The text, heap-allocated (caller frees), or @c NULL on error
 *         or when OCR is not available.
 */
extern char *tapi_cv_ocr_text(rcf_rpc_server *rpcs, const char *image,
                              const tapi_cv_rect *rect, const char *lang);

/**
 * Decode the QR and/or barcode symbols in an image.
 *
 * @param[in]  rpcs     RPC server on the agent.
 * @param[in]  image    The image on the agent.
 * @param[in]  kind     What to look for (QR, barcode, or any).
 * @param[out] codes    Result; release with tapi_cv_codes_free().
 *
 * @return Status code; @c TE_EOPNOTSUPP when only barcodes were asked
 *         for and that backend is not on the agent.
 */
extern te_errno tapi_cv_decode(rcf_rpc_server *rpcs, const char *image,
                               tapi_cv_decode_kind kind, tapi_cv_codes *codes);

/** Write a diff into the log. */
extern void tapi_cv_diff_log(const tapi_cv_diff *diff);
/** Write matches into the log. */
extern void tapi_cv_matches_log(const tapi_cv_matches *matches);
/** Write region statistics into the log. */
extern void tapi_cv_region_log(const tapi_cv_region_stats *stats);
/** Write recognised text into the log. */
extern void tapi_cv_text_log(const tapi_cv_text *text);
/** Write decoded codes into the log. */
extern void tapi_cv_codes_log(const tapi_cv_codes *codes);

/** Release a #tapi_cv_image_info. */
extern void tapi_cv_image_info_free(tapi_cv_image_info *info);
/** Release a #tapi_cv_diff. */
extern void tapi_cv_diff_free(tapi_cv_diff *diff);
/** Release a #tapi_cv_matches. */
extern void tapi_cv_matches_free(tapi_cv_matches *matches);
/** Release a #tapi_cv_text. */
extern void tapi_cv_text_free(tapi_cv_text *text);
/** Release a #tapi_cv_codes. */
extern void tapi_cv_codes_free(tapi_cv_codes *codes);

/**
 * Spell out a match method.
 *
 * @param method    The method.
 *
 * @return A static string, never @c NULL.
 */
extern const char *tapi_cv_method2str(tapi_cv_match_method method);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TAPI_CV_H__ */

/**@} <!-- END tapi_cv --> */
