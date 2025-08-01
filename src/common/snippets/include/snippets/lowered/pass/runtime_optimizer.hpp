// Copyright (C) 2024 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <type_traits>

#include "openvino/core/except.hpp"
#include "openvino/core/rtti.hpp"
#include "snippets/lowered/pass/pass.hpp"
#include "snippets/runtime_configurator.hpp"

namespace ov::snippets::lowered::pass {
/**
 * @class RuntimeOptimizer
 * @brief Base class for runtime optimizers that operate on LinearIR and RuntimeConfigurator during
 * RuntimeConfigurator::update stage.
 */
class RuntimeOptimizer : public ConstPass {
public:
    OPENVINO_RTTI("RuntimeOptimizer", "0", ConstPass)
    RuntimeOptimizer() = default;
    explicit RuntimeOptimizer(const RuntimeConfigurator* configurator) : m_configurator(configurator) {
        OPENVINO_ASSERT(configurator, "RuntimeConfigurator musn't be nullptr");
    }
    /**
     * @brief Defines if this pass is applicable. If it is not applicable, its registration in pass pipeline can be
     * skipped.
     */
    virtual bool applicable() const = 0;

    /**
     * @brief Creates an instance of the specified pass type and checks if it is applicable.
     * If the pass is applicable, it is registered in the provided pipeline.
     * @param pipeline The pipeline in which the pass should be registered.
     * @param args The arguments to be forwarded to the pass constructor.
     */
    template <typename OptimizerType,
              typename... Args,
              typename = std::enable_if_t<std::is_base_of_v<RuntimeOptimizer, OptimizerType>>>
    static void register_if_applicable(PassPipeline& pipeline, Args&&... args) {
        auto pass = std::make_shared<OptimizerType>(std::forward<Args>(args)...);
        if (pass->applicable()) {
            pipeline.register_pass(pass);
        }
    }

protected:
    const RuntimeConfigurator* m_configurator = nullptr;
};

}  // namespace ov::snippets::lowered::pass
