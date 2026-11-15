/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief Agent-side computer vision: the OpenCV backend
 *
 * Every vision operation, driven through OpenCV's C++ API (and, for
 * OCR, libtesseract) - the libraries are linked and called in this
 * process, nothing is spawned. The only C++ translation unit in ta_cv.
 *
 * It deliberately includes NO Test Environment headers: TE's C headers
 * are not C++-safe, so this unit speaks a plain-C seam - @c char**
 * out-parameters filled with @c strdup'd strings, @c int scalars by
 * pointer, and an @c int return (0 ok / 1 bad image / 2 engine failure
 * / 3 the feature is not compiled in) - and the C dispatcher in ta_cv.c
 * converts to/from the TE types.
 *
 * Which optional backends are compiled in is decided by the build
 * (see meson.build): @c TA_CV_HAVE_TESSERACT for OCR and
 * @c TA_CV_HAVE_BARCODE for 1-D barcodes. QR decoding is part of
 * OpenCV's objdetect module, which is always linked.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#ifdef TA_CV_HAVE_BARCODE
#include <opencv2/objdetect/barcode.hpp>
#endif

#ifdef TA_CV_HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

/* Return values of the raw seam, matching ta_cv.h's rc_from_raw(). */
#define CV_OK         0
#define CV_EBADIMG    1
#define CV_EFAIL      2
#define CV_EUNSUP     3

/* Feature bits, matching ta_cv.h. */
#define CV_FEAT_OCR     1
#define CV_FEAT_QR      2
#define CV_FEAT_BARCODE 4

/* Decode kinds, matching ta_cv.h. */
#define CV_DECODE_QR      1
#define CV_DECODE_BARCODE 2

/** strdup() a std::string into an out-param, when the out-param is set. */
static void
set_out(char **out, const std::string &s)
{
    if (out != NULL)
        *out = strdup(s.c_str());
}

/** A real value as a milli-unit integer (value * 1000, rounded). */
static int
milli(double v)
{
    return static_cast<int>(std::lround(v * 1000.0));
}

/** Store a scalar through a possibly-NULL out-pointer. */
static void
put(int *p, int v)
{
    if (p != NULL)
        *p = v;
}

/** Bits per channel of an OpenCV depth. */
static int
depth_bits(int cvdepth)
{
    switch (cvdepth)
    {
        case CV_8U: case CV_8S:   return 8;
        case CV_16U: case CV_16S: return 16;
        case CV_32S: case CV_32F: return 32;
        case CV_64F:              return 64;
        default:                  return 0;
    }
}

/** Name the image container from the file's first bytes. */
static std::string
sniff_format(const char *path)
{
    unsigned char b[12] = {0};
    std::ifstream f(path, std::ios::binary);

    if (!f)
        return "?";
    f.read(reinterpret_cast<char *>(b), sizeof(b));

    if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
        return "PNG";
    if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF)
        return "JPEG";
    if (b[0] == 'B' && b[1] == 'M')
        return "BMP";
    if (b[0] == 'G' && b[1] == 'I' && b[2] == 'F')
        return "GIF";
    if (b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F' &&
        b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P')
        return "WEBP";
    if ((b[0] == 'I' && b[1] == 'I' && b[2] == 0x2A) ||
        (b[0] == 'M' && b[1] == 'M' && b[3] == 0x2A))
        return "TIFF";
    return "?";
}

/* ---- available ---------------------------------------------------- */

/*
 * Can this OpenCV actually decode a QR code? The QRCodeDetector type is
 * always present (objdetect), but decoding needs OpenCV to have been
 * built with QUIRC; without it detectAndDecode() silently returns "". So
 * probe a real round-trip - encode a tiny code and decode it - and only
 * then report QR as available, the same honesty the OCR/barcode gates
 * give. The encoder is not always present either (older OpenCV), in
 * which case QR decoding is reported unavailable too.
 */
static bool
qr_decode_works()
{
    try
    {
        cv::Ptr<cv::QRCodeEncoder> enc = cv::QRCodeEncoder::create();
        cv::Mat code;

        enc->encode("tsf-cv", code);
        if (code.empty())
            return false;

        cv::Mat big;
        cv::copyMakeBorder(code, big, 4, 4, 4, 4, cv::BORDER_CONSTANT,
                           cv::Scalar(255));
        cv::resize(big, big, cv::Size(), 8, 8, cv::INTER_NEAREST);

        cv::QRCodeDetector det;
        return !det.detectAndDecode(big).empty();
    }
    catch (const std::exception &)
    {
        return false;
    }
}

extern "C" int
ta_cv_available_raw(unsigned int *features, char **version)
{
    try
    {
        unsigned int feats = 0;

        if (qr_decode_works())
            feats |= CV_FEAT_QR;
#ifdef TA_CV_HAVE_TESSERACT
        feats |= CV_FEAT_OCR;
#endif
#ifdef TA_CV_HAVE_BARCODE
        feats |= CV_FEAT_BARCODE;
#endif
        if (features != NULL)
            *features = feats;
        set_out(version, CV_VERSION);

        /* A trivial op touches the core the way a real call does. */
        cv::Mat probe = cv::Mat::zeros(2, 2, CV_8UC3);
        (void)cv::sum(probe);

        return CV_OK;
    }
    catch (const std::exception &)
    {
        return CV_EFAIL;
    }
}

/* ---- image_info --------------------------------------------------- */

extern "C" int
ta_cv_image_info_raw(const char *path, int *width, int *height, int *channels,
                     int *depth, char **format)
{
    try
    {
        cv::Mat img = cv::imread(path != NULL ? path : "",
                                 cv::IMREAD_UNCHANGED);

        if (img.empty())
            return CV_EBADIMG;

        put(width, img.cols);
        put(height, img.rows);
        put(channels, img.channels());
        put(depth, depth_bits(img.depth()));
        set_out(format, sniff_format(path));

        return CV_OK;
    }
    catch (const std::exception &)
    {
        return CV_EFAIL;
    }
}

/* ---- compare ------------------------------------------------------ */

/** Structural similarity of two 8-bit grayscale images (Wang et al.). */
static double
ssim_gray(const cv::Mat &a8, const cv::Mat &b8)
{
    const double C1 = 6.5025;
    const double C2 = 58.5225;
    cv::Mat a, b, a2, b2, ab;

    a8.convertTo(a, CV_32F);
    b8.convertTo(b, CV_32F);
    a2 = a.mul(a);
    b2 = b.mul(b);
    ab = a.mul(b);

    cv::Mat mu_a, mu_b;
    cv::GaussianBlur(a, mu_a, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(b, mu_b, cv::Size(11, 11), 1.5);

    cv::Mat mu_a2 = mu_a.mul(mu_a);
    cv::Mat mu_b2 = mu_b.mul(mu_b);
    cv::Mat mu_ab = mu_a.mul(mu_b);

    cv::Mat s_a2, s_b2, s_ab;
    cv::GaussianBlur(a2, s_a2, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(b2, s_b2, cv::Size(11, 11), 1.5);
    cv::GaussianBlur(ab, s_ab, cv::Size(11, 11), 1.5);
    s_a2 -= mu_a2;
    s_b2 -= mu_b2;
    s_ab -= mu_ab;

    cv::Mat num = (2 * mu_ab + C1).mul(2 * s_ab + C2);
    cv::Mat den = (mu_a2 + mu_b2 + C1).mul(s_a2 + s_b2 + C2);
    cv::Mat ssim_map;
    cv::divide(num, den, ssim_map);

    return cv::mean(ssim_map)[0];
}

extern "C" int
ta_cv_compare_raw(const char *path_a, const char *path_b, int tolerance,
                  int want_diff, unsigned int *diff_pixels,
                  unsigned int *total_pixels, int *max_delta,
                  int *mean_delta_milli, int *mse_milli, int *psnr_milli,
                  int *ssim_milli, int *bbox_x, int *bbox_y, int *bbox_w,
                  int *bbox_h, char **diff_path)
{
    try
    {
        cv::Mat a = cv::imread(path_a != NULL ? path_a : "", cv::IMREAD_COLOR);
        cv::Mat b = cv::imread(path_b != NULL ? path_b : "", cv::IMREAD_COLOR);

        if (a.empty() || b.empty())
            return CV_EBADIMG;
        if (a.size() != b.size())
            return CV_EFAIL; /* caller compares equal-sized images */

        cv::Mat diff;
        cv::absdiff(a, b, diff);

        double maxd = 0.0;
        cv::minMaxLoc(diff.reshape(1), NULL, &maxd);
        put(max_delta, static_cast<int>(maxd));

        cv::Scalar md = cv::mean(diff);
        double mean_delta = (md[0] + md[1] + md[2]) / 3.0;
        put(mean_delta_milli, milli(mean_delta));

        /* A pixel is "changed" when any channel exceeds the tolerance. */
        std::vector<cv::Mat> ch;
        cv::split(diff, ch);
        cv::Mat m0 = ch[0] > tolerance;
        cv::Mat m1 = ch[1] > tolerance;
        cv::Mat m2 = ch[2] > tolerance;
        cv::Mat over = m0 | m1 | m2;

        int changed = cv::countNonZero(over);
        if (diff_pixels != NULL)
            *diff_pixels = static_cast<unsigned int>(changed);
        if (total_pixels != NULL)
            *total_pixels = static_cast<unsigned int>(a.rows * a.cols);

        if (changed > 0)
        {
            cv::Rect bb = cv::boundingRect(over);
            put(bbox_x, bb.x);
            put(bbox_y, bb.y);
            put(bbox_w, bb.width);
            put(bbox_h, bb.height);
        }
        else
        {
            put(bbox_x, -1);
            put(bbox_y, -1);
            put(bbox_w, -1);
            put(bbox_h, -1);
        }

        /* MSE across all channels, then PSNR; SSIM on grayscale. */
        cv::Mat d2;
        diff.convertTo(d2, CV_32F);
        d2 = d2.mul(d2);
        cv::Scalar mse_s = cv::mean(d2); /* per-channel mean of squares */
        double mse = (mse_s[0] + mse_s[1] + mse_s[2]) / 3.0;
        put(mse_milli, milli(mse));
        double psnr = mse <= 1e-10 ? 100.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
        if (psnr > 100.0)
            psnr = 100.0;
        put(psnr_milli, milli(psnr));

        cv::Mat ga, gb;
        cv::cvtColor(a, ga, cv::COLOR_BGR2GRAY);
        cv::cvtColor(b, gb, cv::COLOR_BGR2GRAY);
        put(ssim_milli, milli(ssim_gray(ga, gb)));

        if (want_diff)
        {
            cv::Mat heat, gd;
            cv::cvtColor(diff, gd, cv::COLOR_BGR2GRAY);
            cv::applyColorMap(gd, heat, cv::COLORMAP_JET);
            std::string p = cv::tempfile("png");
            if (cv::imwrite(p, heat))
                set_out(diff_path, p);
        }

        return CV_OK;
    }
    catch (const std::exception &)
    {
        return CV_EFAIL;
    }
}

/* ---- match -------------------------------------------------------- */

extern "C" int
ta_cv_match_raw(const char *image, const char *templ, int method,
                int threshold_milli, int max_matches, int *best_milli,
                char **matches)
{
    try
    {
        cv::Mat img = cv::imread(image != NULL ? image : "", cv::IMREAD_COLOR);
        cv::Mat tpl = cv::imread(templ != NULL ? templ : "", cv::IMREAD_COLOR);

        if (img.empty() || tpl.empty())
            return CV_EBADIMG;
        if (tpl.rows > img.rows || tpl.cols > img.cols)
            return CV_EFAIL; /* template larger than the image */

        /*
         * The method selects the family; the score is always the
         * normalised variant, mapped so higher is a better match and
         * the range is comparable across methods.
         */
        int fam = method / 2;
        int tm = fam == 0 ? cv::TM_SQDIFF_NORMED :
                 fam == 1 ? cv::TM_CCORR_NORMED : cv::TM_CCOEFF_NORMED;

        cv::Mat res;
        cv::matchTemplate(img, tpl, res, tm);

        cv::Mat score = fam == 0 ? (1.0 - res) : res;

        double best = 0.0;
        cv::minMaxLoc(score, NULL, &best);
        put(best_milli, milli(best));

        double threshold = threshold_milli / 1000.0;
        int cap = max_matches > 0 ? max_matches : 1;
        std::string out;

        for (int i = 0; i < cap; i++)
        {
            double peak = 0.0;
            cv::Point loc;
            cv::minMaxLoc(score, NULL, &peak, NULL, &loc);
            if (peak < threshold)
                break;

            char line[128];
            std::snprintf(line, sizeof(line), "%d\t%d\t%d\t%d\t%d\n",
                          milli(peak), loc.x, loc.y, tpl.cols, tpl.rows);
            out += line;

            /* Suppress a template-sized window so the next peak is a
             * different location, not the same blob again. */
            int x0 = std::max(0, loc.x - tpl.cols / 2);
            int y0 = std::max(0, loc.y - tpl.rows / 2);
            int x1 = std::min(score.cols, loc.x + tpl.cols / 2 + 1);
            int y1 = std::min(score.rows, loc.y + tpl.rows / 2 + 1);
            score(cv::Rect(x0, y0, x1 - x0, y1 - y0)).setTo(-1.0e9);
        }

        set_out(matches, out);
        return CV_OK;
    }
    catch (const std::exception &)
    {
        return CV_EFAIL;
    }
}

/* ---- region ------------------------------------------------------- */

extern "C" int
ta_cv_region_raw(const char *image, int x, int y, int w, int h, int use_color,
                 int h_lo, int s_lo, int v_lo, int h_hi, int s_hi, int v_hi,
                 int *mean_b, int *mean_g, int *mean_r, int *min_v, int *max_v,
                 int *in_range_milli, int *bbox_x, int *bbox_y, int *bbox_w,
                 int *bbox_h)
{
    try
    {
        cv::Mat img = cv::imread(image != NULL ? image : "", cv::IMREAD_COLOR);

        if (img.empty())
            return CV_EBADIMG;

        /* A non-positive width or height means the whole image. */
        if (w <= 0 || h <= 0)
        {
            x = 0;
            y = 0;
            w = img.cols;
            h = img.rows;
        }
        cv::Rect roi(x, y, w, h);
        roi &= cv::Rect(0, 0, img.cols, img.rows);
        if (roi.width <= 0 || roi.height <= 0)
            return CV_EFAIL; /* rectangle outside the image */

        cv::Mat sub = img(roi);

        cv::Scalar m = cv::mean(sub);
        put(mean_b, static_cast<int>(m[0]));
        put(mean_g, static_cast<int>(m[1]));
        put(mean_r, static_cast<int>(m[2]));

        cv::Mat gray;
        cv::cvtColor(sub, gray, cv::COLOR_BGR2GRAY);
        double mn = 0.0, mx = 0.0;
        cv::minMaxLoc(gray, &mn, &mx);
        put(min_v, static_cast<int>(mn));
        put(max_v, static_cast<int>(mx));

        if (use_color)
        {
            cv::Mat hsv, mask;
            cv::cvtColor(sub, hsv, cv::COLOR_BGR2HSV);
            cv::inRange(hsv, cv::Scalar(h_lo, s_lo, v_lo),
                        cv::Scalar(h_hi, s_hi, v_hi), mask);

            int in = cv::countNonZero(mask);
            double frac = static_cast<double>(in) / (roi.width * roi.height);
            put(in_range_milli, milli(frac));

            if (in > 0)
            {
                cv::Rect bb = cv::boundingRect(mask);
                put(bbox_x, bb.x + roi.x);
                put(bbox_y, bb.y + roi.y);
                put(bbox_w, bb.width);
                put(bbox_h, bb.height);
            }
            else
            {
                put(bbox_x, -1);
                put(bbox_y, -1);
                put(bbox_w, -1);
                put(bbox_h, -1);
            }
        }
        else
        {
            put(in_range_milli, 0);
            put(bbox_x, -1);
            put(bbox_y, -1);
            put(bbox_w, -1);
            put(bbox_h, -1);
        }

        return CV_OK;
    }
    catch (const std::exception &)
    {
        return CV_EFAIL;
    }
}

/* ---- ocr ---------------------------------------------------------- */

extern "C" int
ta_cv_ocr_raw(const char *image, int x, int y, int w, int h, const char *lang,
              int *mean_conf, char **text, char **words)
{
#ifndef TA_CV_HAVE_TESSERACT
    (void)image; (void)x; (void)y; (void)w; (void)h; (void)lang;
    (void)mean_conf; (void)text; (void)words;
    return CV_EUNSUP;
#else
    try
    {
        cv::Mat img = cv::imread(image != NULL ? image : "", cv::IMREAD_COLOR);

        if (img.empty())
            return CV_EBADIMG;

        if (w > 0 && h > 0)
        {
            cv::Rect roi(x, y, w, h);
            roi &= cv::Rect(0, 0, img.cols, img.rows);
            if (roi.width <= 0 || roi.height <= 0)
                return CV_EFAIL;
            img = img(roi).clone();
        }

        tesseract::TessBaseAPI api;
        const char *l = (lang != NULL && lang[0] != '\0') ? lang : "eng";
        if (api.Init(NULL, l) != 0)
            return CV_EUNSUP; /* no language data on the agent */

        api.SetImage(img.data, img.cols, img.rows, img.channels(),
                     static_cast<int>(img.step));
        api.Recognize(NULL);

        char *utf8 = api.GetUTF8Text();
        set_out(text, utf8 != NULL ? utf8 : "");
        delete[] utf8;

        std::string wlines;
        tesseract::ResultIterator *it = api.GetIterator();
        if (it != NULL)
        {
            const tesseract::PageIteratorLevel lvl = tesseract::RIL_WORD;
            do
            {
                char *word = it->GetUTF8Text(lvl);
                if (word == NULL || word[0] == '\0')
                {
                    delete[] word;
                    continue;
                }
                int left = 0, top = 0, right = 0, bottom = 0;
                it->BoundingBox(lvl, &left, &top, &right, &bottom);
                int conf = static_cast<int>(it->Confidence(lvl));

                char head[96];
                std::snprintf(head, sizeof(head), "%d\t%d\t%d\t%d\t%d\t",
                              conf, left, top, right - left, bottom - top);
                wlines += head;
                wlines += word;
                wlines += "\n";
                delete[] word;
            } while (it->Next(lvl));
            delete it;
        }
        set_out(words, wlines);

        put(mean_conf, api.MeanTextConf());
        api.End();

        return CV_OK;
    }
    catch (const std::exception &)
    {
        return CV_EFAIL;
    }
#endif
}

/* ---- decode ------------------------------------------------------- */

/** Append one "type \t x \t y \t w \t h \t payload" record. */
static void
append_code(std::string &out, const std::string &type, const cv::Rect &bb,
            const std::string &payload)
{
    char head[128];
    std::snprintf(head, sizeof(head), "%s\t%d\t%d\t%d\t%d\t",
                  type.c_str(), bb.x, bb.y, bb.width, bb.height);
    out += head;
    out += payload;
    out += "\n";
}

extern "C" int
ta_cv_decode_raw(const char *image, int kind, int *count, char **codes)
{
    try
    {
        cv::Mat img = cv::imread(image != NULL ? image : "", cv::IMREAD_COLOR);

        if (img.empty())
            return CV_EBADIMG;

        bool want_qr = (kind & CV_DECODE_QR) != 0;
        bool want_bar = (kind & CV_DECODE_BARCODE) != 0;

#ifndef TA_CV_HAVE_BARCODE
        if (want_bar && !want_qr)
            return CV_EUNSUP; /* only barcodes asked for, none compiled in */
        want_bar = false;
#endif

        std::string out;
        int n = 0;

        if (want_qr)
        {
            cv::QRCodeDetector qr;

            /*
             * The single-code path is more reliable than the multi one
             * on several OpenCV builds, so try it first; fall back to the
             * multi path (several symbols in one image) when it finds
             * nothing.
             */
            std::vector<cv::Point2f> one_pts;
            std::string one = qr.detectAndDecode(img, one_pts);
            if (!one.empty())
            {
                cv::Rect bb = one_pts.size() >= 4 ? cv::boundingRect(one_pts)
                                                  : cv::Rect(-1, -1, -1, -1);
                append_code(out, "QR", bb, one);
                n++;
            }
            else
            {
                std::vector<std::string> texts;
                std::vector<cv::Point2f> pts;

                if (qr.detectAndDecodeMulti(img, texts, pts))
                {
                    for (size_t i = 0; i < texts.size(); i++)
                    {
                        if (texts[i].empty())
                            continue;
                        std::vector<cv::Point2f> quad(pts.begin() + i * 4,
                                                      pts.begin() + i * 4 + 4);
                        cv::Rect bb = cv::boundingRect(quad);
                        append_code(out, "QR", bb, texts[i]);
                        n++;
                    }
                }
            }
        }

#ifdef TA_CV_HAVE_BARCODE
        if (want_bar)
        {
            cv::barcode::BarcodeDetector bar;
            std::vector<std::string> texts;
            std::vector<std::string> types;
            std::vector<cv::Point2f> pts;

            if (bar.detectAndDecodeWithType(img, texts, types, pts))
            {
                for (size_t i = 0; i < texts.size(); i++)
                {
                    if (texts[i].empty())
                        continue;
                    std::vector<cv::Point2f> quad(pts.begin() + i * 4,
                                                  pts.begin() + i * 4 + 4);
                    cv::Rect bb = cv::boundingRect(quad);
                    append_code(out, types[i].empty() ? "BARCODE" : types[i],
                                bb, texts[i]);
                    n++;
                }
            }
        }
#endif

        put(count, n);
        set_out(codes, out);
        return CV_OK;
    }
    catch (const std::exception &)
    {
        return CV_EFAIL;
    }
}
