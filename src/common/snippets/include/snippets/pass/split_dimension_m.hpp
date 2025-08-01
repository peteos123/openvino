// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "openvino/core/node.hpp"
#include "openvino/core/rtti.hpp"
#include "openvino/core/shape.hpp"
#include "openvino/op/matmul.hpp"
#include "snippets/op/subgraph.hpp"
#include "snippets/pass/common_optimizations.hpp"
#include "snippets/shape_types.hpp"
#include "subgraph_pass.hpp"

namespace ov::snippets::pass {

/**
 * @interface SplitDimensionM
 * @brief Inserts Reshape nodes before inputs and after outputs of Subgraphs with MatMul inside
 *        to split dimension M for MatMuls. It allows to increase work amount for parallelism
 * @attention This pass works only for MHA with static shapes.
 * For dynamic shapes, parallel work amount is optimized in RuntimeConfigurator.
 * @todo Ticket 148805: Move static cases handling in RuntimeConfigurator as well.
 * @ingroup snippets
 */
class SplitDimensionM : public CommonOptimizations::SubgraphPass {
public:
    OPENVINO_RTTI("SplitDimensionM", "0");
    explicit SplitDimensionM(size_t concurrency) : m_concurrency(concurrency) {}

    bool run_on_subgraph(const std::shared_ptr<op::Subgraph>& subgraph) override;

    // Return True if the MatMul node is supported by this optimization
    static bool is_supported_matmul(const std::shared_ptr<const ov::Node>& node);
    // Returns True if parallelism work amount (concurrency) can be increased by this optimization
    static bool can_be_optimized(const std::shared_ptr<const ov::Node>& node, size_t concurrency);

    /**
     * @brief Tries to split M dimension in "shape" in accordance to optimal parallel work amount
     * @param shape Original shape
     * @param optimal_parallelism_work_amount Optimal work amount
     * @param batch_m_dim reference on batch's part of the split M
     * @param new_m_dim reference on new M dim after the split
     * @return true if split was successfull, otherwise false
     */
    static bool split(const ov::Shape& shape,
                      size_t optimal_parallelism_work_amount,
                      size_t& batch_m_dim,
                      size_t& new_m_dim);

    /**
     * @brief Splits m dimension in order
     * @param order Original order
     * @param m_index M dimension index
     * @return updated order with the split M dimension
     */
    static std::vector<size_t> get_updated_order(const std::vector<size_t>& order, size_t m_index);
    /**
     * @brief Reshapes m dimension in "shape": separates M in two parts: "batch_m_dim" and "new_m_dim"
     * @param shape Shape to split
     * @param m_index M dimension index
     * @param batch_m_dim batch's part of the split M
     * @param new_m_dim new M dim after the split
     * @return the updated shape
     */
    static ov::snippets::VectorDims reshape_m_dim(ov::snippets::VectorDims shape,
                                                  size_t m_index,
                                                  size_t batch_m_dim,
                                                  size_t new_m_dim);
    /**
     * @brief Unsqueezes m dimension in "shape" (inserts "1" before the dimension)
     * @param shape Shape to split
     * @param m_index M dimension index
     * @return the updated shape
     */
    static ov::snippets::VectorDims unsqueeze_m_dim(ov::snippets::VectorDims shape, size_t m_index);

private:
    static std::shared_ptr<ov::op::v0::MatMul> get_matmul(const std::shared_ptr<op::Subgraph>& subgraph);
    /**
     * @brief Contains splitM approaches allowing to get the batch ideally divisible by optimal_parallelism_work_amount
     */
    static std::pair<size_t, size_t> split_ideally(size_t batch_dim,
                                                   size_t m_dim,
                                                   size_t optimal_parallelism_work_amount);
    /**
     * @brief Splits m_dim to minimize kernel_m in order to reduce waiting time for idle threads at the last parallel
     * loop iteration.
     */
    static std::pair<size_t, size_t> split_minimize_kernel_wa(size_t batch_dim,
                                                              size_t m_dim,
                                                              size_t optimal_parallelism_work_amount);
    /**
     * @brief Splits m_dim to get the batch in (optimal_parallelism_work_amount, 2 * optimal_parallelism_work_amount)
     * interval
     */
    static std::pair<size_t, size_t> split_fallback_increase_parallel_wa(size_t batch_dim,
                                                                         size_t m_dim,
                                                                         size_t optimal_parallelism_work_amount);

    static void reshape_subgraph(const std::shared_ptr<op::Subgraph>& subgraph,
                                 const ov::Shape& shape,
                                 size_t batch_m_dim,
                                 size_t new_m_dim);

    static size_t get_dim_M(const ov::Shape& shape) {
        if (shape.size() < dim_M_index + 1) {
            return 1;
        }
        return *(shape.rbegin() + dim_M_index);
    }

    size_t m_concurrency;

    static const size_t min_kernel_m;
    static const size_t dim_M_index;
};
}  // namespace ov::snippets::pass
