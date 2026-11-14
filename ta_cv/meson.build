# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
#
# External TE library (see TE_EXT_REPO): agent-side computer vision over
# OpenCV's C++ API (opencv2/..., -lopencv_*), plus optional OCR over
# libtesseract. The library is linked and called in-process; nothing is
# spawned. The agent and its RPC server both link it.
#
# The vision work is one C++ translation unit (ta_cv_opencv.cpp), so the
# build of this library needs a C++ compiler; ta_cv.c is the C seam.

headers += files(
    'ta_cv.h',
)

sources += files(
    'ta_cv.c',
    'ta_cv_opencv.cpp',
)

te_libs += [
    'logger_core',
    'tools',
]

cc = meson.get_compiler('c')
add_languages('cpp', required: true)
cxx = meson.get_compiler('cpp')

# OpenCV ships opencv4.pc on most distributions; fall back to linking the
# modules we use directly when there is no pkg-config.
opencv_dep = dependency('opencv4', required: false)
if not opencv_dep.found()
    opencv_dep = dependency('opencv', required: false)
endif
if not opencv_dep.found()
    opencv_dep = declare_dependency(dependencies: [
        cxx.find_library('opencv_core'),
        cxx.find_library('opencv_imgcodecs'),
        cxx.find_library('opencv_imgproc'),
        cxx.find_library('opencv_objdetect'),
    ])
endif
deps += [ opencv_dep ]

cv_defs = []

# 1-D barcode decoding: OpenCV folds cv::barcode into the objdetect
# module from 4.8 (opencv2/objdetect/barcode.hpp); on older OpenCV it is
# in opencv_contrib. Compile it in only when the header is there, so a
# plain OpenCV still builds (cv_decode then serves QR alone).
if cxx.has_header('opencv2/objdetect/barcode.hpp', dependencies: opencv_dep)
    cv_defs += '-DTA_CV_HAVE_BARCODE'
endif

# OCR over libtesseract (+ leptonica). Optional: without it cv_ocr
# returns TE_EOPNOTSUPP and the suite's OCR test skips.
tess_dep = dependency('tesseract', required: false)
if not tess_dep.found()
    tess_dep = cxx.find_library('tesseract', has_headers: 'tesseract/baseapi.h',
                                required: false)
endif
if tess_dep.found()
    deps += [ tess_dep ]
    lept_dep = dependency('lept', required: false)
    if not lept_dep.found()
        lept_dep = cxx.find_library('lept', required: false)
    endif
    if lept_dep.found()
        deps += [ lept_dep ]
    endif
    cv_defs += '-DTA_CV_HAVE_TESSERACT'
endif

if cv_defs.length() > 0
    deps += declare_dependency(compile_args: cv_defs)
endif
