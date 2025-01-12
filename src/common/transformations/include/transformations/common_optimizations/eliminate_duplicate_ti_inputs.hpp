// Copyright (C) 2018-2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <memory>
#include <openvino/pass/graph_rewrite.hpp>
#include <transformations_visibility.hpp>
#include <vector>

namespace ov {
namespace pass {

class TRANSFORMATIONS_API EliminateDuplicateTIInputs;

}  // namespace pass
}  // namespace ov

/*
 * @ingroup ie_transformation_common_api
 * @brief EliminateDuplicateTIInputs transformation
 * removes duplicated inputs of SubgraphOps.
 */

class ov::pass::EliminateDuplicateTIInputs : public ov::pass::MatcherPass {
public:
    OPENVINO_RTTI("EliminateDuplicateTIInputs", "0");
    EliminateDuplicateTIInputs();
};
