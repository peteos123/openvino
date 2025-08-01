// Copyright (C) 2024 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstddef>
#include <map>
#include <memory>

#include "openvino/core/model.hpp"
#include "openvino/pass/pass.hpp"

namespace ov::snippets::pass {

/**
 * @interface AnalyzeBroadcastableInputs
 * @brief Analyzes body parameters which affects inputs of broadcastable operations (If needed, `Broadcast` op should be
 * inserted there). Also the pass initializes special map `BroadcastableInputsMap` Notes:
 *          - Must be called after Canonicalization pass
 *          - Doesn't support `layouts` in PortDescriptors
 * @ingroup snippets
 */
class AnalyzeBroadcastableInputs : public ov::pass::ModelPass {
public:
    OPENVINO_MODEL_PASS_RTTI("snippets::pass::AnalyzeBroadcastableInputs");
    // [Index of Parameter -> Index of broadcastable dimension from end]
    using BroadcastableInputsMap = std::map<size_t, size_t>;
    explicit AnalyzeBroadcastableInputs(BroadcastableInputsMap& map);

    bool run_on_model(const std::shared_ptr<ov::Model>& body) override;

private:
    BroadcastableInputsMap& m_broadcastable_inputs;
};

}  // namespace ov::snippets::pass
