// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "priorbox_clustered.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <oneapi/dnnl/dnnl_common.hpp>
#include <string>
#include <vector>

#include "cpu_types.h"
#include "graph_context.h"
#include "memory_desc/cpu_memory_desc.h"
#include "node.h"
#include "onednn/iml_type_mapper.h"
#include "openvino/core/except.hpp"
#include "openvino/core/node.hpp"
#include "openvino/core/parallel.hpp"
#include "openvino/core/type.hpp"
#include "openvino/core/type/element_type.hpp"
#include "openvino/op/prior_box_clustered.hpp"
#include "shape_inference/custom/priorbox_clustered.hpp"
#include "utils/general_utils.h"

namespace ov::intel_cpu::node {
bool PriorBoxClustered::isSupportedOperation(const std::shared_ptr<const ov::Node>& op,
                                             std::string& errorMessage) noexcept {
    try {
        const auto priorBox = ov::as_type_ptr<const ov::op::v0::PriorBoxClustered>(op);
        if (!priorBox) {
            errorMessage = "Only opset1 PriorBoxClustered operation is supported";
            return false;
        }
    } catch (...) {
        return false;
    }
    return true;
}

PriorBoxClustered::PriorBoxClustered(const std::shared_ptr<ov::Node>& op, const GraphContext::CPtr& context)
    : Node(op, context, PriorBoxClusteredShapeInferFactory(op)) {
    std::string errorMessage;
    if (!isSupportedOperation(op, errorMessage)) {
        OPENVINO_THROW_NOT_IMPLEMENTED(errorMessage);
    }

    const auto priorBox = ov::as_type_ptr<const ov::op::v0::PriorBoxClustered>(op);
    const ov::op::v0::PriorBoxClustered::Attributes& attrs = priorBox->get_attrs();

    widths = attrs.widths;
    heights = attrs.heights;
    clip = attrs.clip;
    variances = attrs.variances;
    step = attrs.step;
    step_heights = attrs.step_heights;
    step_widths = attrs.step_widths;
    offset = attrs.offset;

    number_of_priors = widths.size();

    if (variances.empty()) {
        variances.push_back(0.1F);
    }
}

bool PriorBoxClustered::needShapeInfer() const {
    auto memory = getDstMemoryAtPort(0);
    if (memory->getShape().isDynamic()) {
        return true;
    }

    const auto& output_shape = memory->getShape().getStaticDims();
    const int* in_data = getSrcDataAtPortAs<int>(0);
    const int h = in_data[0];
    const int w = in_data[1];
    const auto output = static_cast<size_t>(4) * h * w * number_of_priors;

    return output_shape[1] != output;
}

bool PriorBoxClustered::needPrepareParams() const {
    return false;
}

void PriorBoxClustered::initSupportedPrimitiveDescriptors() {
    if (!supportedPrimitiveDescriptors.empty()) {
        return;
    }

    addSupportedPrimDesc({{LayoutType::ncsp, ov::element::i32}, {LayoutType::ncsp, ov::element::i32}},
                         {{LayoutType::ncsp, ov::element::f32}},
                         impl_desc_type::ref_any);
}

void PriorBoxClustered::createPrimitive() {
    if (inputShapesDefined()) {
        if (needPrepareParams()) {
            prepareParams();
        }
        updateLastInputDims();
    }
}

void PriorBoxClustered::execute([[maybe_unused]] const dnnl::stream& strm) {
    const int* in_data = getSrcDataAtPortAs<int>(0);
    const int layer_height = in_data[0];
    const int layer_width = in_data[1];

    const int* in_image = getSrcDataAtPortAs<int>(1);
    int img_height = in_image[0];
    int img_width = in_image[1];

    float step_w = step_widths == 0 ? step : step_widths;
    float step_h = step_heights == 0 ? step : step_heights;
    if (all_of(0, step_w, step_h)) {
        step_w = static_cast<float>(img_width) / static_cast<float>(layer_width);
        step_h = static_cast<float>(img_height) / static_cast<float>(layer_height);
    }

    auto* dst_data = getDstDataAtPortAs<float>(0);
    const auto& out_shape = getChildEdgeAt(0)->getMemory().getShape().getStaticDims();

    size_t var_size = variances.size();
    parallel_for2d(layer_height, layer_width, [&](int64_t h, int64_t w) {
        float center_x = (static_cast<float>(w) + offset) * step_w;
        float center_y = (static_cast<float>(h) + offset) * step_h;

        for (int s = 0; s < number_of_priors; ++s) {
            float box_width = widths[s];
            float box_height = heights[s];

            float xmin = (center_x - box_width / 2.0F) / static_cast<float>(img_width);
            float ymin = (center_y - box_height / 2.0F) / static_cast<float>(img_height);
            float xmax = (center_x + box_width / 2.0F) / static_cast<float>(img_width);
            float ymax = (center_y + box_height / 2.0F) / static_cast<float>(img_height);

            if (clip) {
                xmin = (std::min)((std::max)(xmin, 0.0F), 1.0F);
                ymin = (std::min)((std::max)(ymin, 0.0F), 1.0F);
                xmax = (std::min)((std::max)(xmax, 0.0F), 1.0F);
                ymax = (std::min)((std::max)(ymax, 0.0F), 1.0F);
            }

            const uint64_t idx = h * layer_width * number_of_priors * 4 + w * number_of_priors * 4 + s * 4;
            dst_data[idx + 0] = xmin;
            dst_data[idx + 1] = ymin;
            dst_data[idx + 2] = xmax;
            dst_data[idx + 3] = ymax;

            // At this point we have either:
            // 1. A single variance value (to be repeated 4 times for each prior)
            // 2. 4 variance values
            if (var_size == 1) {
                for (size_t j = 0; j < 4; j++) {
                    dst_data[idx + j + out_shape[1]] = variances[0];
                }
            } else {
                for (size_t j = 0; j < var_size; j++) {
                    dst_data[idx + j + out_shape[1]] = variances[j];
                }
            }
        }
    });
}

bool PriorBoxClustered::created() const {
    return getType() == Type::PriorBoxClustered;
}

}  // namespace ov::intel_cpu::node
