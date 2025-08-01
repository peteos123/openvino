// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "insert_convert_after_extension.hpp"

#include <memory>

#include "cpu_types.h"
#include "openvino/cc/pass/itt.hpp"
#include "openvino/core/node.hpp"
#include "openvino/core/node_output.hpp"
#include "openvino/core/type.hpp"
#include "openvino/core/type/element_type.hpp"
#include "openvino/op/convert.hpp"
#include "openvino/op/result.hpp"
#include "openvino/pass/matcher_pass.hpp"
#include "openvino/pass/pattern/matcher.hpp"
#include "openvino/pass/pattern/op/label.hpp"
#include "openvino/pass/pattern/op/pattern.hpp"
#include "utils/general_utils.h"

ov::pass::InsertConvertAfterExtension::InsertConvertAfterExtension(bool convert_output_precision) {
    MATCHER_SCOPE(InsertConvertAfterExtension);

    auto i64_extension = [](const ov::Output<ov::Node>& output) -> bool {
        auto node = output.get_node_shared_ptr();
        return ov::intel_cpu::TypeFromName(node->get_type_name()) == ov::intel_cpu::Type::Unknown &&
               ov::pass::pattern::type_matches_any({ov::element::i64, ov::element::u64})(output);
    };

    auto ref_m = ov::pass::pattern::any_input(i64_extension);

    ov::matcher_pass_callback callback = [=](ov::pass::pattern::Matcher& m) {
        const auto ref = m.get_match_root();

        for (auto& output : ref->outputs()) {
            if (ov::intel_cpu::any_of(output.get_element_type(), ov::element::i64, ov::element::u64)) {
                auto targetInputs = output.get_target_inputs();
                auto convert = std::make_shared<op::v0::Convert>(output, ov::element::i32);

                for (const auto& targetInput : targetInputs) {
                    // Keep the original output element type if required.
                    if (!convert_output_precision && is_type<op::v0::Result>(targetInput.get_node())) {
                        continue;
                    }
                    targetInput.replace_source_output(convert);
                }

                auto& convertTensor = convert->output(0).get_tensor();

                if (!output.get_names().empty()) {
                    convertTensor.set_names(output.get_names());
                }
            }
        }

        return true;
    };

    auto m = std::make_shared<ov::pass::pattern::Matcher>(ref_m, matcher_name);
    this->register_matcher(m, callback);
}
