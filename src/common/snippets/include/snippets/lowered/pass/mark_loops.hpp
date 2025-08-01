// Copyright (C) 2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstddef>

#include "openvino/core/rtti.hpp"
#include "pass.hpp"
#include "snippets/lowered/linear_ir.hpp"

namespace ov::snippets::lowered::pass {

/**
 * @interface MarkLoops
 * @brief The pass marks expressions with Loop IDs.
 *        The pass iterates expression by expression till the following conditions:
 *          - the layouts and subtensors them are the same
 *          - the consumer of the expression is explicitly after this expression - the pass marks the branches
 * @ingroup snippets
 */
class MarkLoops : public RangedPass {
public:
    OPENVINO_RTTI("MarkLoops", "", RangedPass);
    explicit MarkLoops(size_t vector_size);
    bool run(LinearIR& linear_ir, lowered::LinearIR::constExprIt begin, lowered::LinearIR::constExprIt end) override;

private:
    size_t m_vector_size;
};

}  // namespace ov::snippets::lowered::pass
