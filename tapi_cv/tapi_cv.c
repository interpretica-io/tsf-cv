/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Computer vision from a test
 *
 * The engine-neutral layer over the cv_* RPCs: it fills the RPC's
 * inputs, asks the agent, turns the @c *_milli integers back into
 * doubles and parses the newline/tab result text into the public
 * result structures.
 */

#define TE_LGR_USER     "TAPI CV"

#include "te_config.h"

#include <stdlib.h>
#include <string.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "te_vector.h"
#include "logger_api.h"

#include "tapi_cv.h"
#include "tapi_cv_rpc.h"

/* See description in tapi_cv.h */
const char *
tapi_cv_method2str(tapi_cv_match_method method)
{
    switch (method)
    {
        case TAPI_CV_SQDIFF:        return "sqdiff";
        case TAPI_CV_SQDIFF_NORMED: return "sqdiff-normed";
        case TAPI_CV_CCORR:         return "ccorr";
        case TAPI_CV_CCORR_NORMED:  return "ccorr-normed";
        case TAPI_CV_CCOEFF:        return "ccoeff";
        case TAPI_CV_CCOEFF_NORMED: return "ccoeff-normed";
        default:                    return "unknown";
    }
}

/*
 * Split @p line (length @p len, no newline) on tabs into at most
 * @p max_fields pointers. The last field keeps the remainder of the
 * line, tabs and all, so a trailing text field may contain spaces (and,
 * being last, needs no splitting). Returns the number of fields found.
 * Each field is NUL-terminated in place in @p buf, a writable copy.
 */
static int
split_tabs(char *buf, char **fields, int max_fields)
{
    int n = 0;
    char *p = buf;

    while (n < max_fields)
    {
        fields[n++] = p;
        if (n == max_fields)
            break;
        p = strchr(p, '\t');
        if (p == NULL)
            break;
        *p++ = '\0';
    }
    return n;
}

/* Iterate the lines of @p text, handing each (as a writable copy) to cb. */
static void
foreach_line(const char *text, void (*cb)(char *line, void *ctx), void *ctx)
{
    const char *line = text;

    while (line != NULL && *line != '\0')
    {
        const char *nl = strchr(line, '\n');
        size_t len = nl != NULL ? (size_t)(nl - line) : strlen(line);

        if (len != 0)
        {
            char *copy = TE_STRNDUP(line, len);

            cb(copy, ctx);
            free(copy);
        }
        line = nl != NULL ? nl + 1 : NULL;
    }
}

/* ---- available / supports ----------------------------------------- */

/* See description in tapi_cv.h */
bool
tapi_cv_available(rcf_rpc_server *rpcs)
{
    return rpc_cv_available(rpcs, NULL, NULL) == 0;
}

/* See description in tapi_cv.h */
bool
tapi_cv_supports(rcf_rpc_server *rpcs, tapi_cv_feature feature)
{
    unsigned int features = 0;

    if (rpc_cv_available(rpcs, &features, NULL) != 0)
        return false;

    return (features & (unsigned int)feature) != 0;
}

/* ---- image_info --------------------------------------------------- */

/* See description in tapi_cv.h */
te_errno
tapi_cv_info(rcf_rpc_server *rpcs, const char *path,
                   tapi_cv_image_info *info)
{
    te_string format = TE_STRING_INIT;
    te_errno rc;

    memset(info, 0, sizeof(*info));
    rc = rpc_cv_image_info(rpcs, path, &info->width, &info->height,
                           &info->channels, &info->depth, &format);
    if (rc == 0 && format.len != 0)
        info->format = TE_STRDUP(te_string_value(&format));

    te_string_free(&format);
    return rc;
}

/* ---- compare ------------------------------------------------------ */

/* See description in tapi_cv.h */
te_errno
tapi_cv_compare(rcf_rpc_server *rpcs, const char *path_a, const char *path_b,
                const tapi_cv_compare_opts *opts, tapi_cv_diff *diff)
{
    te_string diff_path = TE_STRING_INIT;
    int mean_delta_milli = 0, mse_milli = 0, psnr_milli = 0, ssim_milli = 0;
    te_errno rc;

    memset(diff, 0, sizeof(*diff));
    diff->bbox_x = diff->bbox_y = diff->bbox_w = diff->bbox_h = -1;

    rc = rpc_cv_compare(rpcs, path_a, path_b,
                        opts != NULL ? opts->tolerance : 0,
                        opts != NULL && opts->want_diff,
                        &diff->diff_pixels, &diff->total_pixels,
                        &diff->max_delta, &mean_delta_milli, &mse_milli,
                        &psnr_milli, &ssim_milli, &diff->bbox_x, &diff->bbox_y,
                        &diff->bbox_w, &diff->bbox_h, &diff_path);
    if (rc == 0)
    {
        diff->mean_delta = mean_delta_milli / 1000.0;
        diff->mse = mse_milli / 1000.0;
        diff->psnr = psnr_milli / 1000.0;
        diff->ssim = ssim_milli / 1000.0;
        if (diff_path.len != 0)
            diff->diff_path = TE_STRDUP(te_string_value(&diff_path));
    }

    te_string_free(&diff_path);
    return rc;
}

/* See description in tapi_cv.h */
bool
tapi_cv_images_equal(rcf_rpc_server *rpcs, const char *path_a,
                     const char *path_b, int tolerance)
{
    tapi_cv_compare_opts opts = { .tolerance = tolerance };
    tapi_cv_diff diff;
    bool equal;

    if (tapi_cv_compare(rpcs, path_a, path_b, &opts, &diff) != 0)
        return false;

    equal = diff.diff_pixels == 0;
    tapi_cv_diff_free(&diff);
    return equal;
}

/* ---- match -------------------------------------------------------- */

static void
match_line(char *line, void *ctx)
{
    tapi_cv_matches *matches = ctx;
    char *f[5];

    if (split_tabs(line, f, 5) == 5)
    {
        tapi_cv_match m;

        m.score = strtol(f[0], NULL, 10) / 1000.0;
        m.x = (int)strtol(f[1], NULL, 10);
        m.y = (int)strtol(f[2], NULL, 10);
        m.w = (int)strtol(f[3], NULL, 10);
        m.h = (int)strtol(f[4], NULL, 10);
        TE_VEC_APPEND(&matches->matches, m);
    }
}

/* See description in tapi_cv.h */
te_errno
tapi_cv_match_all(rcf_rpc_server *rpcs, const char *image, const char *templ,
              const tapi_cv_match_opts *opts, tapi_cv_matches *matches)
{
    te_string text = TE_STRING_INIT;
    int best_milli = 0;
    int method = opts != NULL ? (int)opts->method : TAPI_CV_CCOEFF_NORMED;
    int threshold_milli = opts != NULL ? (int)(opts->threshold * 1000.0) : 0;
    int max_matches = opts != NULL && opts->max_matches > 0 ?
                      opts->max_matches : 1;
    te_errno rc;

    memset(matches, 0, sizeof(*matches));
    matches->matches = (te_vec)TE_VEC_INIT(tapi_cv_match);

    rc = rpc_cv_match(rpcs, image, templ, method, threshold_milli, max_matches,
                      &best_milli, &text);
    if (rc == 0)
    {
        matches->best = best_milli / 1000.0;
        foreach_line(te_string_value(&text), match_line, matches);
    }

    te_string_free(&text);
    return rc;
}

/* See description in tapi_cv.h */
bool
tapi_cv_find(rcf_rpc_server *rpcs, const char *image, const char *templ,
             double threshold, tapi_cv_match *best)
{
    tapi_cv_match_opts opts = { .threshold = threshold, .max_matches = 1 };
    tapi_cv_matches matches;
    bool found;

    if (tapi_cv_match_all(rpcs, image, templ, &opts, &matches) != 0)
        return false;

    found = te_vec_size(&matches.matches) > 0;
    if (found && best != NULL)
        *best = TE_VEC_GET(tapi_cv_match, &matches.matches, 0);

    tapi_cv_matches_free(&matches);
    return found;
}

/* ---- region ------------------------------------------------------- */

/* See description in tapi_cv.h */
te_errno
tapi_cv_region(rcf_rpc_server *rpcs, const char *image,
               const tapi_cv_rect *rect, const tapi_cv_hsv_range *color,
               tapi_cv_region_stats *stats)
{
    int in_range_milli = 0;
    te_errno rc;

    memset(stats, 0, sizeof(*stats));
    stats->bbox_x = stats->bbox_y = stats->bbox_w = stats->bbox_h = -1;

    rc = rpc_cv_region(rpcs, image,
                       rect != NULL ? rect->x : 0,
                       rect != NULL ? rect->y : 0,
                       rect != NULL ? rect->w : 0,
                       rect != NULL ? rect->h : 0,
                       color != NULL,
                       color != NULL ? color->h_lo : 0,
                       color != NULL ? color->s_lo : 0,
                       color != NULL ? color->v_lo : 0,
                       color != NULL ? color->h_hi : 0,
                       color != NULL ? color->s_hi : 0,
                       color != NULL ? color->v_hi : 0,
                       &stats->mean_b, &stats->mean_g, &stats->mean_r,
                       &stats->min_v, &stats->max_v, &in_range_milli,
                       &stats->bbox_x, &stats->bbox_y, &stats->bbox_w,
                       &stats->bbox_h);
    if (rc == 0)
        stats->in_range_fraction = in_range_milli / 1000.0;

    return rc;
}

/* ---- ocr ---------------------------------------------------------- */

static void
word_line(char *line, void *ctx)
{
    tapi_cv_text *result = ctx;
    char *f[6];

    if (split_tabs(line, f, 6) == 6)
    {
        tapi_cv_word w;

        w.conf = (int)strtol(f[0], NULL, 10);
        w.x = (int)strtol(f[1], NULL, 10);
        w.y = (int)strtol(f[2], NULL, 10);
        w.w = (int)strtol(f[3], NULL, 10);
        w.h = (int)strtol(f[4], NULL, 10);
        w.text = TE_STRDUP(f[5]);
        TE_VEC_APPEND(&result->words, w);
    }
}

/* See description in tapi_cv.h */
te_errno
tapi_cv_ocr(rcf_rpc_server *rpcs, const char *image, const tapi_cv_rect *rect,
            const char *lang, tapi_cv_text *result)
{
    te_string text = TE_STRING_INIT;
    te_string words = TE_STRING_INIT;
    te_errno rc;

    memset(result, 0, sizeof(*result));
    result->words = (te_vec)TE_VEC_INIT(tapi_cv_word);

    rc = rpc_cv_ocr(rpcs, image,
                    rect != NULL ? rect->x : 0,
                    rect != NULL ? rect->y : 0,
                    rect != NULL ? rect->w : 0,
                    rect != NULL ? rect->h : 0,
                    lang, &result->mean_conf, &text, &words);
    if (rc == 0)
    {
        if (text.len != 0)
            result->text = TE_STRDUP(te_string_value(&text));
        foreach_line(te_string_value(&words), word_line, result);
    }

    te_string_free(&text);
    te_string_free(&words);
    return rc;
}

/* See description in tapi_cv.h */
char *
tapi_cv_ocr_text(rcf_rpc_server *rpcs, const char *image,
                 const tapi_cv_rect *rect, const char *lang)
{
    tapi_cv_text result;
    char *text = NULL;

    if (tapi_cv_ocr(rpcs, image, rect, lang, &result) != 0)
        return NULL;

    if (result.text != NULL)
        text = TE_STRDUP(result.text);
    tapi_cv_text_free(&result);
    return text;
}

/* ---- decode ------------------------------------------------------- */

static void
code_line(char *line, void *ctx)
{
    tapi_cv_codes *codes = ctx;
    char *f[6];

    if (split_tabs(line, f, 6) == 6)
    {
        tapi_cv_code c;

        c.type = TE_STRDUP(f[0]);
        c.x = (int)strtol(f[1], NULL, 10);
        c.y = (int)strtol(f[2], NULL, 10);
        c.w = (int)strtol(f[3], NULL, 10);
        c.h = (int)strtol(f[4], NULL, 10);
        c.data = TE_STRDUP(f[5]);
        TE_VEC_APPEND(&codes->codes, c);
    }
}

/* See description in tapi_cv.h */
te_errno
tapi_cv_decode(rcf_rpc_server *rpcs, const char *image,
               tapi_cv_decode_kind kind, tapi_cv_codes *codes)
{
    te_string text = TE_STRING_INIT;
    int count = 0;
    te_errno rc;

    memset(codes, 0, sizeof(*codes));
    codes->codes = (te_vec)TE_VEC_INIT(tapi_cv_code);

    rc = rpc_cv_decode(rpcs, image, (int)kind, &count, &text);
    if (rc == 0)
        foreach_line(te_string_value(&text), code_line, codes);

    te_string_free(&text);
    return rc;
}

/* ---- log ---------------------------------------------------------- */

/* See description in tapi_cv.h */
void
tapi_cv_diff_log(const tapi_cv_diff *diff)
{
    RING("CV diff: %u/%u px changed, max %d, mean %.2f, MSE %.2f, "
         "PSNR %.2f dB, SSIM %.4f%s%s",
         diff->diff_pixels, diff->total_pixels, diff->max_delta,
         diff->mean_delta, diff->mse, diff->psnr, diff->ssim,
         diff->diff_path != NULL ? ", heatmap " : "",
         diff->diff_path != NULL ? diff->diff_path : "");
}

/* See description in tapi_cv.h */
void
tapi_cv_matches_log(const tapi_cv_matches *matches)
{
    te_string s = TE_STRING_INIT;
    const tapi_cv_match *m;

    te_string_append(&s, "CV match: best %.4f, %zu at or over threshold",
                     matches->best, te_vec_size(&matches->matches));
    TE_VEC_FOREACH((te_vec *)&matches->matches, m)
        te_string_append(&s, "\n  %.4f at (%d,%d) %dx%d", m->score, m->x, m->y,
                         m->w, m->h);

    RING("%s", te_string_value(&s));
    te_string_free(&s);
}

/* See description in tapi_cv.h */
void
tapi_cv_region_log(const tapi_cv_region_stats *stats)
{
    RING("CV region: mean BGR (%d,%d,%d), grey %d..%d, in-range %.1f%%, "
         "box (%d,%d) %dx%d",
         stats->mean_b, stats->mean_g, stats->mean_r, stats->min_v,
         stats->max_v, stats->in_range_fraction * 100.0, stats->bbox_x,
         stats->bbox_y, stats->bbox_w, stats->bbox_h);
}

/* See description in tapi_cv.h */
void
tapi_cv_text_log(const tapi_cv_text *text)
{
    const tapi_cv_word *w;
    te_string s = TE_STRING_INIT;

    te_string_append(&s, "CV OCR (mean conf %d): \"%s\"", text->mean_conf,
                     text->text != NULL ? text->text : "");
    TE_VEC_FOREACH((te_vec *)&text->words, w)
        te_string_append(&s, "\n  [%d] \"%s\" at (%d,%d) %dx%d", w->conf,
                         w->text, w->x, w->y, w->w, w->h);

    RING("%s", te_string_value(&s));
    te_string_free(&s);
}

/* See description in tapi_cv.h */
void
tapi_cv_codes_log(const tapi_cv_codes *codes)
{
    const tapi_cv_code *c;
    te_string s = TE_STRING_INIT;

    te_string_append(&s, "CV decode: %zu symbol(s)",
                     te_vec_size(&codes->codes));
    TE_VEC_FOREACH((te_vec *)&codes->codes, c)
        te_string_append(&s, "\n  %s \"%s\" at (%d,%d) %dx%d", c->type,
                         c->data, c->x, c->y, c->w, c->h);

    RING("%s", te_string_value(&s));
    te_string_free(&s);
}

/* ---- free --------------------------------------------------------- */

/* See description in tapi_cv.h */
void
tapi_cv_image_info_free(tapi_cv_image_info *info)
{
    free(info->format);
    memset(info, 0, sizeof(*info));
}

/* See description in tapi_cv.h */
void
tapi_cv_diff_free(tapi_cv_diff *diff)
{
    free(diff->diff_path);
    memset(diff, 0, sizeof(*diff));
}

/* See description in tapi_cv.h */
void
tapi_cv_matches_free(tapi_cv_matches *matches)
{
    te_vec_free(&matches->matches);
    memset(matches, 0, sizeof(*matches));
}

/* See description in tapi_cv.h */
void
tapi_cv_text_free(tapi_cv_text *text)
{
    tapi_cv_word *w;

    TE_VEC_FOREACH(&text->words, w)
        free(w->text);
    te_vec_free(&text->words);
    free(text->text);
    memset(text, 0, sizeof(*text));
}

/* See description in tapi_cv.h */
void
tapi_cv_codes_free(tapi_cv_codes *codes)
{
    tapi_cv_code *c;

    TE_VEC_FOREACH(&codes->codes, c)
    {
        free(c->type);
        free(c->data);
    }
    te_vec_free(&codes->codes);
    memset(codes, 0, sizeof(*codes));
}
