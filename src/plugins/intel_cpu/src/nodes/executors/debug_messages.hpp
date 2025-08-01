// Copyright (C) 2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#define UNSUPPORTED_SPARSE_WEIGHTS           " sparse weights are not supported"
#define UNSUPPORTED_WEIGHTS_DECOMPRESSION    " weights decompression is not supported"
#define UNSUPPORTED_POST_OPS                 " post ops are not supported"
#define UNSUPPORTED_NUMBER_OF_POSTOPS        " the number of post ops is not supported"
#define UNSUPPORTED_TYPE_OF_POSTOPS          " the type of post ops is not supported"
#define UNSUPPORTED_SRC_PRECISIONS           " unsupported src precisions"
#define UNSUPPORTED_WEI_PRECISIONS           " unsupported wei precisions"
#define UNSUPPORTED_DST_PRECISIONS           " unsupported dst precisions"
#define UNSUPPORTED_ISA                      " unsupported isa"
#define UNSUPPORTED_SRC_RANK                 " unsupported src rank"
#define UNSUPPORTED_WEI_RANK                 " unsupported wei rank"
#define UNSUPPORTED_DST_RANK                 " unsupported dst rank"
#define UNSUPPORTED_DST_STRIDES              " unsupported dst strides"
#define UNSUPPORTED_PER_CHANNEL_QUANTIZATION " unsupported per-channel quantization"
#define UNSUPPORTED_BY_EXECUTOR              " unsupported by executor"
#define HEURISTICS_MISMATCH                  " heuristics mismatch"
#define MEMORY_FORMAT_MISMATCH               " memory format mismatch"

// @todo implement VERIFY_OR version to support multiple conditions and error messages
#define VERIFY(condition, ...)               \
    do {                                     \
        if (!static_cast<bool>(condition)) { \
            DEBUG_LOG(__VA_ARGS__);          \
            return false;                    \
        }                                    \
    } while (0)
