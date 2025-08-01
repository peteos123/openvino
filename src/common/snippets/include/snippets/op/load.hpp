// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "openvino/core/attribute_visitor.hpp"
#include "openvino/core/node.hpp"
#include "openvino/core/node_output.hpp"
#include "openvino/core/node_vector.hpp"
#include "openvino/op/op.hpp"
#include "snippets/op/memory_access.hpp"
#include "snippets/shape_inference/shape_inference.hpp"
#include "snippets/shape_types.hpp"

namespace ov::snippets::op {

/**
 * @interface Load
 * @brief Generated during Lowering stage (convert_to_snippets_dialect) where explicit instructions should be emitted
 * for data loading where number of elements to load is determined by "count" (Default value is "1" - to load one
 * element) and memory offset for loading is determined by "offset" (Default value is "0" - to load starting from the
 * first element)
 * @ingroup snippets
 */
class Load : public modifier::MemoryAccess, public ov::op::Op {
public:
    OPENVINO_OP("Load", "SnippetsOpset");

    explicit Load(const Output<Node>& x, size_t count = 1LU, size_t offset = 0LU);
    Load() = default;

    size_t get_offset() const {
        return get_input_offset(0);
    }
    size_t get_count() const {
        return get_input_count(0);
    }

    void set_offset(size_t offset) {
        set_input_offset(offset, 0);
    }
    void set_count(size_t count) {
        set_input_count(count, 0);
    }

    void validate_and_infer_types() override;
    std::shared_ptr<Node> clone_with_new_inputs(const OutputVector& new_args) const override;
    bool visit_attributes(AttributeVisitor& visitor) override;

protected:
    void validate_memory_access_params() const;
};

/**
 * @interface LoadReorder
 * @brief It's just Load operation (and it's mapped on LoadEmitter during code generation) that allows to tweak
 *        shape propagation. We need it to keep correct shape propagation  when Transpose is decomposed to
 *        Load and Store. This is a temporary solution until tokenization of Reshape operation is supported.
 * @ingroup snippets
 */
class LoadReorder : public Load {
public:
    OPENVINO_OP("LoadReorder", "SnippetsOpset", Load);
    explicit LoadReorder(const Output<Node>& x,
                         size_t count = 1LU,
                         size_t offset = 0LU,
                         std::vector<size_t> order = {});
    LoadReorder() = default;

    void set_offset(size_t offset) {
        set_output_offset(offset, 0);
    }
    void set_count(size_t count) {
        set_output_count(count, 0);
    }

    bool visit_attributes(AttributeVisitor& visitor) override;
    std::shared_ptr<Node> clone_with_new_inputs(const OutputVector& new_args) const override;
    void validate_and_infer_types() override;

    class ShapeInfer : public IShapeInferSnippets {
        std::vector<size_t> m_order;

    public:
        explicit ShapeInfer(const std::shared_ptr<ov::Node>& n);
        Result infer(const std::vector<VectorDimsRef>& input_shapes) override;
    };

protected:
    std::vector<size_t> m_order;
};
}  // namespace ov::snippets::op
