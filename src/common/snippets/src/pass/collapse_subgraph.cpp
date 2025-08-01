// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "snippets/pass/collapse_subgraph.hpp"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <memory>
#include <ostream>
#include <set>
#include <vector>

#include "openvino/core/descriptor/tensor.hpp"
#include "openvino/core/node.hpp"
#include "openvino/core/node_input.hpp"
#include "openvino/core/node_output.hpp"
#include "openvino/core/shape.hpp"
#include "openvino/core/type.hpp"
#include "openvino/core/type/element_type.hpp"
#include "openvino/core/validation_util.hpp"
#include "openvino/op/abs.hpp"
#include "openvino/op/add.hpp"
#include "openvino/op/broadcast.hpp"
#include "openvino/op/ceiling.hpp"
#include "openvino/op/clamp.hpp"
#include "openvino/op/constant.hpp"
#include "openvino/op/convert.hpp"
#include "openvino/op/divide.hpp"
#include "openvino/op/elu.hpp"
#include "openvino/op/equal.hpp"
#include "openvino/op/erf.hpp"
#include "openvino/op/exp.hpp"
#include "openvino/op/floor.hpp"
#include "openvino/op/floor_mod.hpp"
#include "openvino/op/gelu.hpp"
#include "openvino/op/greater.hpp"
#include "openvino/op/greater_eq.hpp"
#include "openvino/op/hswish.hpp"
#include "openvino/op/less.hpp"
#include "openvino/op/less_eq.hpp"
#include "openvino/op/logical_and.hpp"
#include "openvino/op/logical_not.hpp"
#include "openvino/op/logical_or.hpp"
#include "openvino/op/logical_xor.hpp"
#include "openvino/op/loop.hpp"
#include "openvino/op/matmul.hpp"
#include "openvino/op/maximum.hpp"
#include "openvino/op/minimum.hpp"
#include "openvino/op/mish.hpp"
#include "openvino/op/mod.hpp"
#include "openvino/op/multiply.hpp"
#include "openvino/op/negative.hpp"
#include "openvino/op/not_equal.hpp"
#include "openvino/op/power.hpp"
#include "openvino/op/prelu.hpp"
#include "openvino/op/reduce_max.hpp"
#include "openvino/op/reduce_sum.hpp"
#include "openvino/op/relu.hpp"
#include "openvino/op/round.hpp"
#include "openvino/op/select.hpp"
#include "openvino/op/sigmoid.hpp"
#include "openvino/op/softmax.hpp"
#include "openvino/op/sqrt.hpp"
#include "openvino/op/squared_difference.hpp"
#include "openvino/op/subtract.hpp"
#include "openvino/op/swish.hpp"
#include "openvino/op/tanh.hpp"
#include "openvino/op/transpose.hpp"
#include "openvino/op/util/arithmetic_reductions_keep_dims.hpp"
#include "openvino/op/util/attr_types.hpp"
#include "openvino/op/xor.hpp"
#include "openvino/opsets/opset1.hpp"
#include "openvino/pass/matcher_pass.hpp"
#include "openvino/pass/pattern/matcher.hpp"
#include "openvino/pass/pattern/op/label.hpp"
#include "openvino/util/pp.hpp"
#include "snippets/itt.hpp"
#include "snippets/op/subgraph.hpp"
#include "snippets/pass/fq_decomposition.hpp"
#include "snippets/pass/fuse_transpose_brgemm.hpp"
#include "snippets/pass/tokenization.hpp"
#include "snippets/pass/transpose_decomposition.hpp"
#include "snippets/remarks.hpp"
#include "snippets/utils/tokenization_utils.hpp"
#include "snippets/utils/utils.hpp"

namespace ov::snippets::pass {

namespace {
auto is_supported_op(const std::shared_ptr<const Node>& n) -> bool {
    OV_ITT_SCOPED_TASK(ov::pass::itt::domains::SnippetsTransform, "Snippets::is_supported_op")
    auto is_supported_matmul = [](const std::shared_ptr<const Node>& n) -> bool {
        const auto& matmul = ov::as_type_ptr<const opset1::MatMul>(n);
        const auto& out_rank = n->get_output_partial_shape(0).rank();
        if (!matmul || out_rank.is_dynamic() || out_rank.get_length() != 4) {
            return false;
        }
        const auto intype_0 = matmul->get_input_element_type(0);
        const auto intype_1 = matmul->get_input_element_type(1);
        const bool is_f32 = utils::all_of(element::f32, intype_0, intype_1);
        const bool is_int8 = utils::any_of(intype_0, element::i8, element::u8) && (intype_1 == element::i8);
        const bool is_bf16 = utils::all_of(element::bf16, intype_0, intype_1);
        return is_f32 || is_bf16 || is_int8;
    };
    auto is_supported_transpose = [](const std::shared_ptr<const Node>& n) -> bool {
        const auto& transpose = as_type_ptr<const opset1::Transpose>(n);
        if (transpose) {
            const auto parent = transpose->get_input_node_shared_ptr(0);
            const auto child = transpose->get_output_target_inputs(0).begin()->get_node()->shared_from_this();
            auto is_brgemm_case = ov::is_type_any_of<opset1::MatMul, opset1::MatMul>(child);
            auto decomposition_case = true;
            // Check for Transpose parent is MatMul inside Subgraph
            if (const auto subgraph = ov::as_type_ptr<const op::Subgraph>(parent)) {
                if (GetSnippetsSubgraphType(subgraph) != SnippetsSubgraphType::Completed) {
                    // Transpose decomposition is supported only for Transpose nodes right after Subgraph's parameters
                    decomposition_case = false;
                    const auto body = subgraph->body_ptr();
                    const auto subgraph_output =
                        body->get_results()[transpose->input_value(0).get_index()]->get_input_node_shared_ptr(0);
                    is_brgemm_case = is_brgemm_case || ov::is_type<opset1::MatMul>(subgraph_output);
                }
            }

            const auto& order = as_type_ptr<const opset1::Constant>(n->get_input_node_shared_ptr(1));
            if (order) {
                const auto order_value = order->cast_vector<int>();
                return (decomposition_case && TransposeDecomposition::is_supported_transpose_order(order_value)) ||
                       (is_brgemm_case && FuseTransposeBrgemm::is_supported_transpose_order(order_value));
            }
        }
        return false;
    };

    auto is_supported_fq_op = [](const std::shared_ptr<const Node>& n) -> bool {
        return CommonFakeQuantizeDecomposition::is_supported_fq(ov::as_type_ptr<const opset1::FakeQuantize>(n));
    };

    auto is_supported_ternary_eltwise_op = [](const std::shared_ptr<const Node>& n) -> bool {
        return ov::is_type<ov::op::v1::Select>(n);
    };

    auto is_supported_binary_eltwise_op = [](const std::shared_ptr<const Node>& n) -> bool {
        return ov::is_type_any_of<ov::op::v1::Add,
                                  ov::op::v1::Divide,
                                  ov::op::v1::Equal,
                                  ov::op::v1::FloorMod,
                                  ov::op::v1::Greater,
                                  ov::op::v1::GreaterEqual,
                                  ov::op::v1::Less,
                                  ov::op::v1::LessEqual,
                                  ov::op::v1::LogicalAnd,
                                  ov::op::v1::LogicalOr,
                                  ov::op::v1::LogicalXor,
                                  ov::op::v1::Maximum,
                                  ov::op::v1::Minimum,
                                  ov::op::v1::Mod,
                                  ov::op::v1::Multiply,
                                  ov::op::v1::NotEqual,
                                  ov::op::v0::PRelu,
                                  ov::op::v1::Power,
                                  ov::op::v0::SquaredDifference,
                                  ov::op::v1::Subtract,
                                  ov::op::v0::Xor,
                                  ov::op::v0::Convert>(n);
    };

    auto is_supported_unary_eltwise_op = [](const std::shared_ptr<const Node>& n) -> bool {
        return ov::is_type_any_of<ov::op::v0::Abs,
                                  ov::op::v0::Clamp,
                                  ov::op::v0::Floor,
                                  ov::op::v0::Ceiling,
                                  ov::op::v0::Elu,
                                  ov::op::v0::Erf,
                                  ov::op::v0::Exp,
                                  ov::op::v1::LogicalNot,
                                  ov::op::v4::Mish,
                                  ov::op::v0::Negative,
                                  ov::op::v0::Relu,
                                  ov::op::v5::Round,
                                  ov::op::v0::Sigmoid,
                                  ov::op::v0::Sqrt,
                                  ov::op::v0::Tanh,
                                  ov::op::v0::Gelu,
                                  ov::op::v7::Gelu,
                                  ov::op::v4::Swish,
                                  ov::op::v4::HSwish>(n);
    };

    auto is_supported_softmax = [](const std::shared_ptr<const Node>& n) -> bool {
        if (n->get_input_size() != 1 || n->get_input_partial_shape(0).rank().is_dynamic()) {
            return false;
        }
        int64_t axis = -1;
        const auto rank = n->get_input_partial_shape(0).rank();
        if (const auto softmax_v8 = ov::as_type_ptr<const ov::op::v8::Softmax>(n)) {
            if (rank.is_static()) {
                axis = ov::util::try_normalize_axis(softmax_v8->get_axis(), rank, *n);
            }
        } else if (const auto softmax_v1 = ov::as_type_ptr<const ov::op::v1::Softmax>(n)) {
            axis = softmax_v1->get_axis();
        } else {
            return false;
        }
        return axis >= 0 && axis == (rank.get_length() - 1);
    };

    auto is_supported_broadcast_op = [](const std::shared_ptr<const Node>& n) -> bool {
        // Broadcast is supported only for MHA tokenization where there are needed and special checks
        if (auto broadcast_v1 = ov::as_type_ptr<const ov::op::v1::Broadcast>(n)) {
            return broadcast_v1->get_broadcast_spec().m_type == ov::op::AutoBroadcastType::NUMPY;
        }
        if (auto broadcast_v3 = ov::as_type_ptr<const ov::op::v3::Broadcast>(n)) {
            return broadcast_v3->get_broadcast_spec().m_type == ov::op::BroadcastType::NUMPY;
        }
        return false;
    };

    auto is_supported_reduce_op = [](const std::shared_ptr<const Node>& n) -> bool {
        if (ov::is_type_any_of<const ov::op::v1::ReduceMax, const ov::op::v1::ReduceSum>(n)) {
            const auto& reduce_base = ov::as_type_ptr<const ov::op::util::ArithmeticReductionKeepDims>(n);
            const auto& axis_constant = ov::as_type_ptr<const ov::op::v0::Constant>(n->get_input_node_shared_ptr(1));
            const auto rank = n->get_input_partial_shape(0).rank();
            if (rank.is_dynamic() || !reduce_base->get_keep_dims() || !axis_constant ||
                shape_size(axis_constant->get_shape()) != 1) {
                return false;
            }

            const auto axis_value = axis_constant->cast_vector<int32_t>(1)[0];
            const auto normalized_axis = util::normalize(axis_value, rank.get_length());
            // Note: Reduction only over the last dimension is currently supported
            return normalized_axis == rank.get_length() - 1;
        }
        return false;
    };

    return is_supported_fq_op(n) || is_supported_unary_eltwise_op(n) || is_supported_binary_eltwise_op(n) ||
           is_supported_ternary_eltwise_op(n) || is_supported_transpose(n) || is_supported_softmax(n) ||
           is_supported_matmul(n) || is_supported_broadcast_op(n) || is_supported_reduce_op(n);
}

auto has_supported_in_out(const std::shared_ptr<const Node>& n) -> bool {
    auto supported = [](descriptor::Tensor& t) -> bool {
        // TODO [122585] Need to add dynamic rank support
        return t.get_partial_shape().rank().is_static();
    };
    const auto& inputs = n->inputs();
    const auto& outputs = n->outputs();
    // todo: Is this check necessary? Remove if not
    for (const auto& out : outputs) {
        for (const auto& in_out : out.get_target_inputs()) {
            if (ov::is_type<ov::op::v5::Loop>(in_out.get_node()->shared_from_this())) {
                return false;
            }
        }
    }
    return std::all_of(inputs.begin(),
                       inputs.end(),
                       [&](const Input<const Node>& in) {
                           return supported(in.get_tensor());
                       }) &&
           std::all_of(outputs.begin(), outputs.end(), [&](const Output<const Node>& out) {
               return supported(out.get_tensor());
           });
}
}  // namespace

const std::set<ov::element::Type>& ov::snippets::pass::TokenizeSnippets::get_supported_element_types() {
    static const std::set<ov::element::Type> supported_element_types = {ov::element::f32,
                                                                        ov::element::bf16,
                                                                        ov::element::f16,
                                                                        ov::element::i8,
                                                                        ov::element::u8};
    return supported_element_types;
}

bool TokenizeSnippets::AppropriateForSubgraph(const std::shared_ptr<const Node>& node) {
    return is_supported_op(node) && has_supported_in_out(node) && node->get_control_dependencies().empty() &&
           snippets::op::Subgraph::check_broadcast(node);
}

TokenizeSnippets::TokenizeSnippets(const SnippetsTokenization::Config& config) {
    MATCHER_SCOPE(TokenizeSnippets);

    auto label = ov::pass::pattern::any_input([](const ov::Output<ov::Node>& out) {
        const auto n = out.get_node_shared_ptr();
        // todo: MatMul and Transpose ops are always skipped by the SnippetsMarkSkipped pass.
        //  This is a temporary solution. Either modify SnippetsMarkSkipped
        //  or align this with the custom MHA tokenization pass.
        return (GetSnippetsNodeType(n) != SnippetsNodeType::SkippedByPlugin ||
                ov::is_type_any_of<ov::op::v0::MatMul, ov::op::v1::Transpose>(n)) &&
               AppropriateForSubgraph(n);
    });
    ov::graph_rewrite_callback callback = [OV_CAPTURE_CPY_AND_THIS](ov::pass::pattern::Matcher& m) -> bool {
        OV_ITT_SCOPED_TASK(ov::pass::itt::domains::SnippetsTransform, "Snippets::CreateSubgraph_callback")
        auto node = m.get_match_root();
        if (transformation_callback(node)) {
            return false;
        }
        remark(1) << "Match root: " << node->get_friendly_name() << " " << node << '\n';
        return ov::snippets::utils::tokenize_node(node, config);
    };
    auto matcher = std::make_shared<ov::pass::pattern::Matcher>(label, matcher_name);
    register_matcher(matcher, callback);
}
}  // namespace ov::snippets::pass
