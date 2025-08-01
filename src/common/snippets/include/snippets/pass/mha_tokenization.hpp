// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "openvino/opsets/opset1.hpp"
#include "openvino/pass/matcher_pass.hpp"
#include "snippets/pass/tokenization.hpp"

namespace ov::snippets::pass {

/**
 * @interface TokenizeMHASnippets
 * @brief The pass tokenizes MHA-pattern into Subgraph
 *        Pattern:           Transpose1
 *                               |
 *             Transpose0 [Eltwise, Select]
 *                     \     /
 *                     MatMul0
 *                        |
 *           [Eltwise, Select, Reshape]
 *                        |
 *                     Softmax
 *                        |
 *            [Eltwise, Select, Reshape]  Transpose2
 *                               \      /
 *                                MatMul1
 *                                  |
 *                  [Eltwise, Select, Transpose3]
 *        Notes:
 *          - Transposes can be missed
 *          - Transpose0, Transpose2 and Transpose3 may have only [0,2,1,3] order
 *          - Transpose1 may have only [0,2,3,1] order
 *          - [...] means any count of different nodes from list. But:
 *              * Reshapes can be only explicitly around Softmax (Reshape -> Softmax -> Reshape)
 *              * After MatMul1 may be only Transpose3 or any count of Eltwise, Select ops.
 * @ingroup snippets
 */
class TokenizeMHASnippets : public ov::pass::MatcherPass {
public:
    OPENVINO_MATCHER_PASS_RTTI("snippets::pass::TokenizeMHASnippets");
    explicit TokenizeMHASnippets(const SnippetsTokenization::Config& config);

    static std::vector<int32_t> get_fusion_transpose_order(size_t rank);
    static std::vector<int32_t> get_decomposed_transpose_order(size_t rank);
    static bool is_matmul0_supported(const std::shared_ptr<ov::opset1::MatMul>& matmul);
};

}  // namespace ov::snippets::pass
