// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "pyopenvino/core/offline_transformations.hpp"

#include <pybind11/stl.h>

#include <compress_quantize_weights.hpp>
#include <openvino/pass/make_stateful.hpp>
#include <openvino/pass/sdpa_to_paged_attention.hpp>
#include <openvino/pass/serialize.hpp>
#include <openvino/pass/stateful_to_stateless.hpp>
#include <pruning.hpp>
#include <transformations/common_optimizations/compress_float_constants.hpp>
#include <transformations/common_optimizations/fused_names_cleanup.hpp>
#include <transformations/common_optimizations/mark_precision_sensitive_shapeof_subgraphs.hpp>
#include <transformations/common_optimizations/moc_legacy_transformations.hpp>
#include <transformations/common_optimizations/moc_transformations.hpp>
#include <transformations/flush_fp32_subnormals_to_zero.hpp>
#include <transformations/op_conversions/convert_sequences_to_tensor_iterator.hpp>
#include <transformations/smart_reshape/smart_reshape.hpp>

#include "openvino/pass/low_latency.hpp"
#include "openvino/pass/manager.hpp"
#include "pyopenvino/utils/utils.hpp"

namespace py = pybind11;

void regmodule_offline_transformations(py::module m) {
    py::module m_offline_transformations =
        m.def_submodule("_offline_transformations", "Offline transformations module");
    m_offline_transformations.doc() =
        "openvino._offline_transformations is a private module contains different offline passes.";

    m_offline_transformations.def(
        "apply_moc_transformations",
        [](py::object& ie_api_model, bool cf, bool smart_reshape) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            if (smart_reshape)
                manager.register_pass<ov::pass::SmartReshape>();
            manager.register_pass<ov::pass::MOCTransformations>(cf);
            manager.register_pass<ov::pass::FlushFP32SubnormalsToZero>();
            manager.run_passes(model);
        },
        py::arg("model"),
        py::arg("cf"),
        py::arg("smart_reshape") = false);

    m_offline_transformations.def(
        "apply_moc_legacy_transformations",
        [](py::object& ie_api_model, const std::vector<std::string>& params_with_custom_types) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::MOCLegacyTransformations>(params_with_custom_types);
            manager.run_passes(model);
        },
        py::arg("model"),
        py::arg("params_with_custom_types"));

    m_offline_transformations.def(
        "apply_low_latency_transformation",
        [](py::object& ie_api_model, bool use_const_initializer = true) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::LowLatency2>(use_const_initializer);
            manager.run_passes(model);
        },
        py::arg("model"),
        py::arg("use_const_initializer") = true);

    m_offline_transformations.def(
        "apply_pruning_transformation",
        [](py::object& ie_api_model) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::Pruning>();
            manager.run_passes(model);
        },
        py::arg("model"));

    m_offline_transformations.def(
        "apply_make_stateful_transformation",
        [](py::object& ie_api_model, const std::map<std::string, std::string>& param_res_names) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::MakeStateful>(param_res_names);
            manager.run_passes(model);
        },
        py::arg("model"),
        py::arg("param_res_names"));

    m_offline_transformations.def(
        "apply_make_stateful_transformation",
        [](py::object& ie_api_model, const ov::pass::MakeStateful::ParamResPairs& pairs_to_replace) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::MakeStateful>(pairs_to_replace);
            manager.run_passes(model);
        },
        py::arg("model"),
        py::arg("pairs_to_replace"));

    m_offline_transformations.def(
        "compress_model_transformation",
        [](py::object& ie_api_model) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            bool postponed = false;
            return ov::pass::compress_model_to_f16(model, postponed);
        },
        py::arg("model"));

    m_offline_transformations.def(
        "compress_quantize_weights_transformation",
        [](py::object& ie_api_model) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::CompressQuantizeWeights>();
            manager.run_passes(model);
        },
        py::arg("model"));

    m_offline_transformations.def(
        "convert_sequence_to_tensor_iterator_transformation",
        [](py::object ie_api_model) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::ConvertSequenceToTensorIterator>();
            manager.run_passes(model);
        },
        py::arg("model"));

    m_offline_transformations.def(
        "apply_fused_names_cleanup",
        [](py::object ie_api_model) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::FusedNamesCleanup>();
            manager.run_passes(model);
        },
        py::arg("model"));

    m_offline_transformations.def(
        "paged_attention_transformation",
        [](py::object& ie_api_model,
           bool use_block_indices_inputs,
           bool use_score_outputs,
           bool allow_score_aggregation,
           bool allow_cache_rotation,
           bool allow_xattention) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::SDPAToPagedAttention>(use_block_indices_inputs,
                                                                  use_score_outputs,
                                                                  allow_score_aggregation,
                                                                  allow_cache_rotation,
                                                                  allow_xattention);
            manager.run_passes(model);
        },
        py::arg("model"),
        py::arg("use_block_indices_inputs") = false,
        py::arg("use_score_outputs") = false,
        py::arg("allow_score_aggregation") = false,
        py::arg("allow_cache_rotation") = false,
        py::arg("allow_xattention") = false);

    m_offline_transformations.def(
        "stateful_to_stateless_transformation",
        [](py::object& ie_api_model) {
            const auto model = Common::utils::convert_to_model(ie_api_model);
            ov::pass::Manager manager;
            manager.register_pass<ov::pass::StatefulToStateless>();
            manager.run_passes(model);
        },
        py::arg("model"));
}
