/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief RPC for computer vision
 *
 * The RPCs of rpcs_cv, a thin layer over ta_cv, which analyses an image
 * on the agent with OpenCV in the RPC server process through OpenCV's
 * own C++ library API. Add this file to the rpcxdr definitions of the
 * engine platform and of the agent platform:
 *
 *   TE_LIB_PARMS([rpcxdr], [<platform>], [],
 *                [--with-rpcdefs=tarpc_job.x.m4,../ta_cv/cv_rpc.x.m4])
 *
 * There is no state between calls: every call reads one image (two, for
 * a compare) and returns one verdict. XDR has no portable float, so each
 * real-valued metric travels as a scaled integer in milli-units
 * (value * 1000, named @c *_milli) and the engine side turns it back
 * into a double; a pixel count or a coordinate is a plain integer.
 * Results that are lists - template matches, OCR words, decoded codes -
 * come back as newline-separated records, one per line, the same shape
 * tsf-smt and tsf-upnp use, and the engine side parses them.
 */

/*
 * The feature bits carried in cv_available.out.features (OCR = 1, QR = 2,
 * barcode = 4) and the cv_decode kind (QR = 1, barcode = 2, any = 3) are
 * plain integers on the wire; their named forms live in ta_cv.h (agent)
 * and tapi_cv.h (engine), not here - XDR has no anonymous enum.
 *
 * cv_available(): is OpenCV compiled into this agent and able to run a
 * trivial operation? retval is 0 when it is. features is a bitmask of
 * the optional backends that are present (OCR, QR, barcode).
 */
struct tarpc_cv_available_in {
    struct tarpc_in_arg common;
};

struct tarpc_cv_available_out {
    struct tarpc_out_arg common;

    tarpc_int       retval;
    tarpc_uint      features;
    string          version<>;     /* OpenCV version, e.g. "4.6.0" */
};

/*
 * cv_image_info(): the dimensions and encoding of an image file.
 */
struct tarpc_cv_image_info_in {
    struct tarpc_in_arg common;

    string          path<>;
};

struct tarpc_cv_image_info_out {
    struct tarpc_out_arg common;

    tarpc_int       retval;
    tarpc_int       width;
    tarpc_int       height;
    tarpc_int       channels;
    tarpc_int       depth;          /* bits per channel, e.g. 8 */
    string          format<>;       /* container, from the magic, e.g. "PNG" */
};

/*
 * cv_compare(): a pixel difference of two images of the same size.
 *
 *   tolerance        per-channel delta at or below which a pixel is
 *                    "unchanged" (0 = exact)
 *   want_diff        write a diff heatmap to a temp file and return it
 *   diff_pixels      pixels that differ by more than tolerance
 *   total_pixels     pixels compared
 *   max_delta        largest per-channel absolute delta, 0..255
 *   mean_delta_milli mean per-channel absolute delta * 1000
 *   mse_milli        mean squared error * 1000
 *   psnr_milli       peak signal-to-noise ratio, dB * 1000 (capped)
 *   ssim_milli       structural similarity, -1000..1000
 *   bbox_*           bounding box of the changed region, or all -1
 *   diff_path        the heatmap file on the agent, when want_diff
 */
struct tarpc_cv_compare_in {
    struct tarpc_in_arg common;

    string          path_a<>;
    string          path_b<>;
    tarpc_int       tolerance;
    tarpc_bool      want_diff;
};

struct tarpc_cv_compare_out {
    struct tarpc_out_arg common;

    tarpc_int       retval;
    tarpc_uint      diff_pixels;
    tarpc_uint      total_pixels;
    tarpc_int       max_delta;
    tarpc_int       mean_delta_milli;
    tarpc_int       mse_milli;
    tarpc_int       psnr_milli;
    tarpc_int       ssim_milli;
    tarpc_int       bbox_x;
    tarpc_int       bbox_y;
    tarpc_int       bbox_w;
    tarpc_int       bbox_h;
    string          diff_path<>;
};

/*
 * cv_match(): find a template image inside a larger image.
 *
 *   method        OpenCV match method (0=SQDIFF 1=SQDIFF_NORMED
 *                 2=CCORR 3=CCORR_NORMED 4=CCOEFF 5=CCOEFF_NORMED);
 *                 the score is always normalised 0..1, higher is better
 *   threshold_milli  score * 1000 at or above which a location counts
 *   max_matches   cap on the number of non-overlapping matches returned
 *   best_milli    the best score found anywhere * 1000
 *   matches       one "score_milli \t x \t y \t w \t h" line per match,
 *                 best first
 */
struct tarpc_cv_match_in {
    struct tarpc_in_arg common;

    string          image<>;
    string          templ<>;
    tarpc_int       method;
    tarpc_int       threshold_milli;
    tarpc_int       max_matches;
};

struct tarpc_cv_match_out {
    struct tarpc_out_arg common;

    tarpc_int       retval;
    tarpc_int       best_milli;
    string          matches<>;
};

/*
 * cv_region(): statistics over a rectangle, and the share of it that
 * falls inside an HSV colour range.
 *
 *   x,y,w,h       the rectangle; w or h <= 0 means the whole image
 *   use_color     test the HSV range below (else only mean/min/max)
 *   h_lo..v_hi    inclusive HSV range (H 0..179, S/V 0..255)
 *   mean_b/g/r    mean colour of the rectangle, 0..255
 *   min_v,max_v   min and max grey value in the rectangle, 0..255
 *   in_range_milli  fraction of the rectangle inside the range * 1000
 *   bbox_*        bounding box of the in-range pixels, or all -1
 */
struct tarpc_cv_region_in {
    struct tarpc_in_arg common;

    string          image<>;
    tarpc_int       x;
    tarpc_int       y;
    tarpc_int       w;
    tarpc_int       h;
    tarpc_bool      use_color;
    tarpc_int       h_lo;
    tarpc_int       s_lo;
    tarpc_int       v_lo;
    tarpc_int       h_hi;
    tarpc_int       s_hi;
    tarpc_int       v_hi;
};

struct tarpc_cv_region_out {
    struct tarpc_out_arg common;

    tarpc_int       retval;
    tarpc_int       mean_b;
    tarpc_int       mean_g;
    tarpc_int       mean_r;
    tarpc_int       min_v;
    tarpc_int       max_v;
    tarpc_int       in_range_milli;
    tarpc_int       bbox_x;
    tarpc_int       bbox_y;
    tarpc_int       bbox_w;
    tarpc_int       bbox_h;
};

/*
 * cv_ocr(): recognise text in an image (or a rectangle of it) with
 * libtesseract. retval is TE_EOPNOTSUPP when OCR is not compiled in.
 *
 *   x,y,w,h       rectangle; w or h <= 0 means the whole image
 *   lang          tesseract language, or "" for "eng"
 *   text          the recognised text, as one opaque string
 *   words         one "conf \t x \t y \t w \t h \t word" line per word
 *   mean_conf     mean per-word confidence, 0..100
 */
struct tarpc_cv_ocr_in {
    struct tarpc_in_arg common;

    string          image<>;
    tarpc_int       x;
    tarpc_int       y;
    tarpc_int       w;
    tarpc_int       h;
    string          lang<>;
};

struct tarpc_cv_ocr_out {
    struct tarpc_out_arg common;

    tarpc_int       retval;
    tarpc_int       mean_conf;
    string          text<>;
    string          words<>;
};

/*
 * cv_decode(): decode 2-D (QR) and/or 1-D (barcode) symbols.
 *
 *   kind          1 = QR only, 2 = barcode only, 3 = any
 *   codes         one "type \t x \t y \t w \t h \t payload" line per
 *                 symbol; payload is last and may contain spaces
 *   count         number of symbols decoded
 */
struct tarpc_cv_decode_in {
    struct tarpc_in_arg common;

    string          image<>;
    tarpc_int       kind;
};

struct tarpc_cv_decode_out {
    struct tarpc_out_arg common;

    tarpc_int       retval;
    tarpc_int       count;
    string          codes<>;
};

program cv
{
    version ver0
    {
        RPC_DEF(cv_available)
        RPC_DEF(cv_image_info)
        RPC_DEF(cv_compare)
        RPC_DEF(cv_match)
        RPC_DEF(cv_region)
        RPC_DEF(cv_ocr)
        RPC_DEF(cv_decode)
    } = 1;
} = 43;
