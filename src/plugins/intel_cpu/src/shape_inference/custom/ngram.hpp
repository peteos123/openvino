// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cpu_memory.h"
#include "cpu_types.h"
#include "openvino/core/node.hpp"
#include "shape_inference/shape_inference_cpu.hpp"

#pragma once

namespace ov::intel_cpu::node {
using Result = IShapeInfer::Result;
class NgramShapeInfer : public ShapeInferEmptyPads {
public:
    explicit NgramShapeInfer(const size_t k) : m_k(k) {}
    Result infer(const std::vector<std::reference_wrapper<const VectorDims>>& input_shapes,
                 const std::unordered_map<size_t, MemoryPtr>& data_dependency) override;

    [[nodiscard]] port_mask_t get_port_mask() const override {
        return EMPTY_PORT_MASK;
    }

private:
    size_t m_k;
};

class NgramShapeInferFactory : public ShapeInferFactory {
public:
    explicit NgramShapeInferFactory(std::shared_ptr<ov::Node> op) : m_op(std::move(op)) {}
    [[nodiscard]] ShapeInferPtr makeShapeInfer() const override;

private:
    std::shared_ptr<ov::Node> m_op;
};
}  // namespace ov::intel_cpu::node
