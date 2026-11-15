# tsf-cv

Computer vision from a Test Agent, packaged as an external Test
Environment (TE) repository (consumed with the `TE_EXT_REPO` builder
directive). It analyses an image on the agent over OpenCV's own **C++
library API — no Python, nothing spawned** — and reads the result back
as structured data.

Three libraries:

- `ta_cv` — agent side. Image analysis over **OpenCV's C++ API**
  (`opencv2/...`, `-lopencv_*`) and, optionally, OCR over
  **libtesseract**: the libraries are linked into the agent and called
  in-process. All the vision work is one C++ unit (`ta_cv_opencv.cpp`,
  the only C++ in the repository) behind a plain-C seam; the C
  dispatcher (`ta_cv.c`) adapts it to TE's types. The agent and its RPC
  server both link it.
- `rpcs_cv` — the `cv_*` RPCs for the RPC server of the agent, thin
  wrappers over `ta_cv`. The analysis happens on the agent, in the RPC
  server process.
- `tapi_cv` — engine side. `tapi_cv.h` gives a test the high-level
  calls; `tapi_cv_rpc.h` is the one-per-RPC layer beneath it.

TE has nothing for looking at an image. `tsf-ai` can send one to an LLM
provider (vision-through-an-LLM: a key, the network, tokens spent);
`tsf-cv` is ordinary computer vision, run where the image already is.

## What it pairs with

The image is analysed on the agent, so `tsf-cv` is the assertion layer
over whatever puts an image there: a screenshot from `tsf-playwright`,
`tsf-android` or `tsf-apple`, a framebuffer dump from `tsf-emu`, a frame
grabbed from a device. Nothing of the image leaves the agent — there is
no service, no key, no network.

## Operations

| Call | What it answers |
|---|---|
| `tapi_cv_image_info` | Dimensions, channels, depth, container of an image |
| `tapi_cv_compare` | Pixel diff of two images: changed-pixel count and box, max/mean delta, MSE, PSNR, SSIM, and an optional heatmap — the visual-regression primitive |
| `tapi_cv_match` | Find a template (an icon, a button) inside an image: best score and the matching locations |
| `tapi_cv_region` | Mean colour of a rectangle and the share of it inside an HSV range — "is the indicator green?" |
| `tapi_cv_ocr` | Recognise text (whole image or a rectangle), with per-word boxes and confidence |
| `tapi_cv_decode` | Decode QR and 1-D barcode symbols, with payloads and boxes |

Convenience predicates sit over the rich calls: `tapi_cv_images_equal()`
(a diff within a tolerance), `tapi_cv_find()` (a template at or above a
threshold), `tapi_cv_ocr_text()` (the text alone).

```c
tapi_cv_matches found;

if (!tapi_cv_available(pco))
    TEST_SKIP("The agent has no OpenCV");

/* Is the OK button on the screenshot, and where? */
CHECK_RC(tapi_cv_match_all(pco, "/tmp/screen.png", "/tmp/ok_button.png",
                       NULL, &found));
if (found.best < 0.9)
    TEST_VERDICT("OK button not found (best score %.2f)", found.best);
tapi_cv_matches_log(&found);
tapi_cv_matches_free(&found);
```

## It is a linked library, not a program

`ta_cv` does not run a `convert` or a `tesseract` command and scrape its
printout. It links OpenCV (and libtesseract) and calls the API in the
agent's RPC server process: an image is decoded by OpenCV (`cv::imread`),
a template is located by `cv::matchTemplate`, a colour share by
`cv::inRange`, a QR by `cv::QRCodeDetector`, text by a
`tesseract::TessBaseAPI`. Results come back as the library's own numbers
and objects, never parsed from text.

XDR has no portable floating-point type, so every real-valued metric
(SSIM, PSNR, MSE, a colour share, a match score) travels over the RPC as
a scaled integer in milli-units and `tapi_cv` turns it back into a
double. Lists — template matches, OCR words, decoded codes — come back
as newline-separated records, the same shape `tsf-smt` and `tsf-upnp`
use, which the engine side parses.

## Optional backends

QR decoding is part of OpenCV's `objdetect` module and is always there.
Two backends are optional and compiled in only when the agent has them:

- **OCR** — `libtesseract` (plus `leptonica`) and a language data file.
  Without it `tapi_cv_ocr()` returns `TE_EOPNOTSUPP`.
- **1-D barcodes** — `cv::barcode`, folded into OpenCV's `objdetect`
  from 4.8 (and in `opencv_contrib` before that). Without it
  `tapi_cv_decode()` still serves QR.

`tapi_cv_supports()` reports what is present, so a test skips the part
the agent cannot do rather than failing it.

## Agent host requirements

- **OpenCV 4.x** with development headers and shared libraries — Debian:
  `apt install libopencv-dev` (gives `opencv4.pc`, or the
  `libopencv_core`/`imgcodecs`/`imgproc`/`objdetect` the meson finds
  directly). The API used here (`imread`, `matchTemplate`, `inRange`,
  `QRCodeDetector`, `applyColorMap`, `tempfile`) is long-standing 4.x.
- **A C++ compiler** for the agent platform: `ta_cv_opencv.cpp` is the
  one C++ unit.
- **Optional, for OCR:** `libtesseract-dev`, `libleptonica-dev` and a
  `tesseract-ocr` language pack (e.g. `tesseract-ocr-eng`).
- **Optional, for 1-D barcodes:** an OpenCV built with `cv::barcode`
  (4.8+, or `opencv_contrib`).

## Usage

Declare the repository in an external libraries catalog and pass it to
`dispatcher.sh --external=<catalog.yml>`:

```yaml
repositories:
  - name: tsf_cv
    url: https://github.com/interpretica-io/tsf-cv.git
    ref: <tag>
    libs:
      - ta_cv
      - rpcs_cv
      - tapi_cv
```

In `builder.conf`, bind `tapi_cv` to the engine, list `ta_cv` and
`rpcs_cv` among the RPC server's libraries, and add the RPC definitions
to both platforms:

```
TE_EXT_REPO_USE([tsf_cv], [ta_cv rpcs_cv], [tapi_cv])

# The RPCs, on the engine and on the agent platform (see cv_rpc.x.m4):
TE_LIB_PARMS([rpcxdr], [${TE_HOST}], [],
             [--with-rpcdefs=tarpc_job.x.m4,../ta_cv/cv_rpc.x.m4])
TE_LIB_PARMS([rpcxdr], [${TE_TA_TYPE}], [],
             [--with-rpcdefs=tarpc_job.x.m4,../ta_cv/cv_rpc.x.m4])

# ta_cv and rpcs_cv in the RPC server application:
TE_TA_APP([ta_rpcprovider], ..., [ta_cv rpcs_cv ...], ...)
```

Then add `tapi_cv` to `te_libs` in the suite's `meson.build`. Requires
TE with `TE_EXT_REPO` support and an agent with an **RPC server**
(`ta_rpcprovider`). The RPC program number is **43**; change it in
`cv_rpc.x.m4` if it ever collides.

## What was verified, and what was not

**The C/C++ library code was not compiled here.** This environment had
no TE toolchain and no OpenCV/tesseract development headers, so `ta_cv.c`,
`ta_cv_opencv.cpp`, `rpcs_cv.c`, `tapi_cv*.c` and the TE integration (the
three `meson.build`s, `cv_rpc.x.m4`, `TE_EXT_REPO` wiring) were written
to the `tsf-smt` template but **not built or run**. The companion suite
`tsf-cv-ts` drives every call against deterministic synthetic images
generated on the agent and is the first thing to build it; the first
real build should expect the ordinary first-build fixes, and in
particular an OpenCV version or module-name adjustment (`cv::barcode` and
the `detectAndDecodeMulti` signature are the likeliest).

## Scope

- **OpenCV must be on the agent.** `tsf-cv` does not install it;
  `tapi_cv_available()` skips cleanly when it is not linked in, and
  `tapi_cv_supports()` gates OCR and barcodes. (`tsf-package` can
  install a dependency as a test's setup.)
- **An image file is the unit, and it lives on the agent.** A call takes
  a path on the agent; a test that holds an image on the engine copies
  it over first (TE's file transfer). `tsf-cv` reads images, it does not
  grab them — that is what the screenshot/framebuffer modules are for.
