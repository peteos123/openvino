// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "openvino/pass/serialize.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>

#include "common_test_utils/file_utils.hpp"
#include "common_test_utils/graph_comparator.hpp"
#include "common_test_utils/test_common.hpp"
#include "openvino/core/graph_util.hpp"
#include "openvino/op/add.hpp"
#include "openvino/pass/serialize.hpp"
#include "openvino/runtime/core.hpp"
#include "openvino/runtime/tensor.hpp"
#include "openvino/util/file_util.hpp"
#include "read_ir.hpp"

namespace ov::test {

using op::v0::Parameter, op::v0::Constant, op::v1::Add;

class SerializePassTest : public testing::Test {
protected:
    std::filesystem::path m_out_xml_path;
    std::filesystem::path m_out_bin_path;
    std::shared_ptr<Model> m_model;

    void SetUp() override {
        const auto filePrefix = ov::test::utils::generateTestFilePrefix();
        m_out_xml_path = filePrefix + ".xml";
        m_out_bin_path = filePrefix + ".bin";
    }

    void TearDown() override {
        if (std::filesystem::exists(m_out_xml_path)) {
            std::filesystem::remove(m_out_xml_path);
        }

        if (std::filesystem::exists(m_out_bin_path)) {
            std::filesystem::remove(m_out_bin_path);
        }
    }

    static FunctionsComparator model_comparator() {
        return FunctionsComparator::with_default()
            .enable(FunctionsComparator::ATTRIBUTES)
            .enable(FunctionsComparator::CONST_VALUES);
    }
};

class SerializePassTestP : public SerializePassTest, public testing::WithParamInterface<element::Type> {};

INSTANTIATE_TEST_SUITE_P(numeric_types,
                         SerializePassTestP,
                         testing::Values(element::bf16,
                                         element::f16,
                                         element::f32,
                                         element::f64,
                                         element::i4,
                                         element::i8,
                                         element::i16,
                                         element::i32,
                                         element::i64,
                                         element::u1,
                                         element::u2,
                                         element::u4,
                                         element::u8,
                                         element::u16,
                                         element::u32,
                                         element::u64,
                                         element::nf4,
                                         element::f8e4m3,
                                         element::f8e5m2,
                                         element::f4e2m1,
                                         element::f8e8m0),
                         testing::PrintToStringParamName());

TEST_P(SerializePassTestP, serialize_simple_model_with_constant) {
    const auto& precision = GetParam();
    const auto p1 = std::make_shared<Parameter>(precision, PartialShape{5});
    const auto c1 = std::make_shared<Constant>(precision, Shape{5}, std::vector{1, 0, 1, 1, 1});
    const auto add = std::make_shared<Add>(p1, c1);
    m_model = std::make_shared<Model>(OutputVector{add}, ParameterVector{p1}, "simple_model");

    OV_ASSERT_NO_THROW(pass::Serialize(m_out_xml_path, m_out_bin_path).run_on_model(m_model));

    const auto serialized_model = test::readModel(m_out_xml_path.string(), m_out_bin_path.string());
    const auto& [is_valid, error_msg] = model_comparator().compare(serialized_model, m_model);
    EXPECT_TRUE(is_valid) << error_msg;
}
}  // namespace ov::test

using SerializationParams = std::tuple<std::string, std::string>;

class SerializationTest : public ov::test::TestsCommon, public testing::WithParamInterface<SerializationParams> {
public:
    std::string m_model_path;
    std::string m_binary_path;
    std::string m_out_xml_path;
    std::string m_out_bin_path;

    void CompareSerialized(std::function<void(const std::shared_ptr<ov::Model>&)> serializer) {
        auto expected = ov::test::readModel(m_model_path, m_binary_path);
        auto orig = expected->clone();
        serializer(expected);
        auto result = ov::test::readModel(m_out_xml_path, m_out_bin_path);
        const auto fc = FunctionsComparator::with_default()
                            .enable(FunctionsComparator::ATTRIBUTES)
                            .enable(FunctionsComparator::CONST_VALUES);
        const auto res = fc.compare(result, expected);
        const auto res2 = fc.compare(expected, orig);
        EXPECT_TRUE(res.valid) << res.message;
        EXPECT_TRUE(res2.valid) << res2.message;
    }

    void SetUp() override {
        m_model_path = ov::test::utils::getModelFromTestModelZoo(
            ov::util::path_join({SERIALIZED_ZOO, "ir/", std::get<0>(GetParam())}).string());
        if (!std::get<1>(GetParam()).empty()) {
            m_binary_path = ov::test::utils::getModelFromTestModelZoo(
                ov::util::path_join({SERIALIZED_ZOO, "ir/", std::get<1>(GetParam())}).string());
        }

        std::string filePrefix = ov::test::utils::generateTestFilePrefix();
        m_out_xml_path = filePrefix + ".xml";
        m_out_bin_path = filePrefix + ".bin";
    }

    void TearDown() override {
        std::remove(m_out_xml_path.c_str());
        std::remove(m_out_bin_path.c_str());
    }
};

TEST_P(SerializationTest, CompareFunctions) {
    CompareSerialized([this](const std::shared_ptr<ov::Model>& m) {
        ov::pass::Serialize(m_out_xml_path, m_out_bin_path).run_on_model(m);
    });
}

TEST_P(SerializationTest, SerializeHelper) {
    CompareSerialized([this](const std::shared_ptr<ov::Model>& m) {
        ov::serialize(m, m_out_xml_path, m_out_bin_path);
    });
}

TEST_P(SerializationTest, SaveModel) {
    CompareSerialized([this](const std::shared_ptr<ov::Model>& m) {
        ov::save_model(m, m_out_xml_path, false);
    });
}

TEST_P(SerializationTest, CompareFunctionsByPath) {
    const auto out_xml_path = std::filesystem::path(m_out_xml_path);
    const auto out_bin_path = std::filesystem::path(m_out_bin_path);
    CompareSerialized([&out_xml_path, &out_bin_path](const auto& m) {
        ov::pass::Serialize(out_xml_path, out_bin_path).run_on_model(m);
    });
}

TEST_P(SerializationTest, SaveModelByPath) {
    const auto out_xml_path = std::filesystem::path(m_out_xml_path);
    CompareSerialized([&out_xml_path](const auto& m) {
        ov::save_model(m, out_xml_path, false);
    });
}

INSTANTIATE_TEST_SUITE_P(
    IRSerialization,
    SerializationTest,
    testing::Values(std::make_tuple("add_abc.xml", "add_abc.bin"),
                    std::make_tuple("add_abc_f64.xml", ""),
                    std::make_tuple("add_abc_bin.xml", ""),
                    std::make_tuple("split_equal_parts_2d.xml", "split_equal_parts_2d.bin"),
                    std::make_tuple("addmul_abc.xml", "addmul_abc.bin"),
                    std::make_tuple("add_abc_initializers.xml", "add_abc_initializers.bin"),
                    std::make_tuple("add_abc_initializers.xml", "add_abc_initializers_f32_nan_const.bin"),
                    std::make_tuple("add_abc_initializers_nan_const.xml", "add_abc_initializers_nan_const.bin"),
                    std::make_tuple("add_abc_initializers_u1_const.xml", "add_abc_initializers_u1_const.bin"),
                    std::make_tuple("experimental_detectron_roi_feature_extractor.xml", ""),
                    std::make_tuple("experimental_detectron_roi_feature_extractor_opset6.xml", ""),
                    std::make_tuple("experimental_detectron_detection_output.xml", ""),
                    std::make_tuple("experimental_detectron_detection_output_opset6.xml", ""),
                    std::make_tuple("nms5.xml", "nms5.bin"),
                    std::make_tuple("shape_of.xml", ""),
                    std::make_tuple("dynamic_input_shape.xml", ""),
                    std::make_tuple("pad_with_shape_of.xml", ""),
                    std::make_tuple("conv_with_rt_info.xml", ""),
                    std::make_tuple("loop_2d_add.xml", "loop_2d_add.bin"),
                    std::make_tuple("nms5_dynamism.xml", "nms5_dynamism.bin"),
                    std::make_tuple("if_diff_case.xml", "if_diff_case.bin"),
                    std::make_tuple("if_body_without_parameters.xml", "if_body_without_parameters.bin"),
                    std::make_tuple("string_parameter.xml", "string_parameter.bin"),
                    std::make_tuple("const_string.xml", "const_string.bin")));

#ifdef ENABLE_OV_ONNX_FRONTEND

INSTANTIATE_TEST_SUITE_P(ONNXSerialization,
                         SerializationTest,
                         testing::Values(std::make_tuple("add_abc.onnx", ""),
                                         std::make_tuple("split_equal_parts_2d.onnx", ""),
                                         std::make_tuple("addmul_abc.onnx", ""),
                                         std::make_tuple("add_abc_initializers.onnx", "")));

#endif

class MetaDataSerialize : public ov::test::TestsCommon {
public:
    std::string ir_with_meta = R"V0G0N(
<net name="Network" version="11">
    <layers>
        <layer name="in1" type="Parameter" id="0" version="opset1">
            <data element_type="f32" shape="1,3,22,22"/>
            <output>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>3</dim>
                    <dim>22</dim>
                    <dim>22</dim>
                </port>
            </output>
        </layer>
        <layer name="activation" id="1" type="ReLU" version="opset1">
            <input>
                <port id="1" precision="FP32">
                    <dim>1</dim>
                    <dim>3</dim>
                    <dim>22</dim>
                    <dim>22</dim>
                </port>
            </input>
            <output>
                <port id="2" precision="FP32">
                    <dim>1</dim>
                    <dim>3</dim>
                    <dim>22</dim>
                    <dim>22</dim>
                </port>
            </output>
        </layer>
        <layer name="output" type="Result" id="2" version="opset1">
            <input>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>3</dim>
                    <dim>22</dim>
                    <dim>22</dim>
                </port>
            </input>
        </layer>
    </layers>
    <edges>
        <edge from-layer="1" from-port="2" to-layer="2" to-port="0"/>
        <edge from-layer="0" from-port="0" to-layer="1" to-port="1"/>
    </edges>
    <meta_data>
        <MO_version value="TestVersion"/>
        <Runtime_version value="TestVersion"/>
        <cli_parameters>
            <input_shape value="[1, 3, 22, 22]"/>
            <transform value=""/>
            <use_new_frontend value="False"/>
        </cli_parameters>
    </meta_data>
    <framework_meta>
        <batch value="1"/>
        <chunk_size value="16"/>
    </framework_meta>
    <quantization_parameters>
        <config>{
        'compression': {
            'algorithms': [
                {
                    'name': 'DefaultQuantization',
                    'params': {
                        'num_samples_for_tuning': 2000,
                        'preset': 'performance',
                        'stat_subset_size': 300,
                        'use_layerwise_tuning': false
                    }
                }
            ],
            'dump_intermediate_model': true,
            'target_device': 'ANY'
        },
        'engine': {
            'models': [
                {
                    'name': 'bert-small-uncased-whole-word-masking-squad-0001',
                    'launchers': [
                        {
                            'framework': 'openvino',
                            'adapter': {
                                'type': 'bert_question_answering',
                                'start_token_logits_output': 'output_s',
                                'end_token_logits_output': 'output_e'
                            },
                            'inputs': [
                                {
                                    'name': 'input_ids',
                                    'type': 'INPUT',
                                    'value': 'input_ids'
                                },
                                {
                                    'name': 'attention_mask',
                                    'type': 'INPUT',
                                    'value': 'input_mask'
                                },
                                {
                                    'name': 'token_type_ids',
                                    'type': 'INPUT',
                                    'value': 'segment_ids'
                                }
                            ],
                            'device': 'cpu'
                        }
                    ],
                    'datasets': [
                        {
                            'name': 'squad_v1_1_msl384_mql64_ds128_lowercase',
                            'annotation_conversion': {
                                'converter': 'squad',
                                'testing_file': 'PATH',
                                'max_seq_length': 384,
                                'max_query_length': 64,
                                'doc_stride': 128,
                                'lower_case': true,
                                'vocab_file': 'PATH'
                            },
                            'reader': {
                                'type': 'annotation_features_extractor',
                                'features': [
                                    'input_ids',
                                    'input_mask',
                                    'segment_ids'
                                ]
                            },
                            'postprocessing': [
                                {
                                    'type': 'extract_answers_tokens',
                                    'max_answer': 30,
                                    'n_best_size': 20
                                }
                            ],
                            'metrics': [
                                {
                                    'name': 'F1',
                                    'type': 'f1',
                                    'reference': 0.9157
                                },
                                {
                                    'name': 'EM',
                                    'type': 'exact_match',
                                    'reference': 0.8504
                                }
                            ],
                            '_command_line_mapping': {
                                'testing_file': 'PATH',
                                'vocab_file': [
                                    'PATH'
                                ]
                            }
                        }
                    ]
                }
            ],
            'stat_requests_number': null,
            'eval_requests_number': null,
            'type': 'accuracy_checker'
        }
    }</config>
        <version value="invalid version"/>
        <cli_params value="{'quantize': None, 'preset': None, 'model': None, 'weights': None, 'name': None, 'engine': None, 'ac_config': None, 'max_drop': None, 'evaluate': False, 'output_dir': 'PATH', 'direct_dump': True, 'log_level': 'INFO', 'pbar': False, 'stream_output': False, 'keep_uncompressed_weights': False, 'data_source': None}"/>
    </quantization_parameters>
</net>
)V0G0N";

    std::string m_out_xml_path;
    std::string m_out_bin_path;

    void SetUp() override {
        std::string filePrefix = ov::test::utils::generateTestFilePrefix();
        m_out_xml_path = filePrefix + ".xml";
        m_out_bin_path = filePrefix + ".bin";
    }

    void check_meta_info(const std::shared_ptr<ov::Model>& model) {
        auto& rt_info = model->get_rt_info();
        const std::string pot_conf_ref =
            "{ 'compression': { 'algorithms': [ { 'name': 'DefaultQuantization', 'params': { 'num_samples_for_tuning': "
            "2000, 'preset': 'performance', 'stat_subset_size': 300, 'use_layerwise_tuning': false } } ], "
            "'dump_intermediate_model': true, 'target_device': 'ANY' }, 'engine': { 'models': [ { 'name': "
            "'bert-small-uncased-whole-word-masking-squad-0001', 'launchers': [ { 'framework': 'openvino', 'adapter': "
            "{ 'type': 'bert_question_answering', 'start_token_logits_output': 'output_s', 'end_token_logits_output': "
            "'output_e' }, 'inputs': [ { 'name': 'input_ids', 'type': 'INPUT', 'value': 'input_ids' }, { 'name': "
            "'attention_mask', 'type': 'INPUT', 'value': 'input_mask' }, { 'name': 'token_type_ids', 'type': 'INPUT', "
            "'value': 'segment_ids' } ], 'device': 'cpu' } ], 'datasets': [ { 'name': "
            "'squad_v1_1_msl384_mql64_ds128_lowercase', 'annotation_conversion': { 'converter': 'squad', "
            "'testing_file': 'PATH', 'max_seq_length': 384, 'max_query_length': 64, 'doc_stride': 128, 'lower_case': "
            "true, 'vocab_file': 'PATH' }, 'reader': { 'type': 'annotation_features_extractor', 'features': [ "
            "'input_ids', 'input_mask', 'segment_ids' ] }, 'postprocessing': [ { 'type': 'extract_answers_tokens', "
            "'max_answer': 30, 'n_best_size': 20 } ], 'metrics': [ { 'name': 'F1', 'type': 'f1', 'reference': 0.9157 "
            "}, { 'name': 'EM', 'type': 'exact_match', 'reference': 0.8504 } ], '_command_line_mapping': { "
            "'testing_file': 'PATH', 'vocab_file': [ 'PATH' ] } } ] } ], 'stat_requests_number': null, "
            "'eval_requests_number': null, 'type': 'accuracy_checker' } }";
        ASSERT_TRUE(!rt_info.empty());
        std::string version;
        EXPECT_NO_THROW(version = model->get_rt_info<std::string>("MO_version"));
        EXPECT_EQ(version, "TestVersion");

        EXPECT_NO_THROW(version = model->get_rt_info<std::string>("Runtime_version"));
        EXPECT_EQ(version, "TestVersion");

        std::string pot_config;
        EXPECT_NO_THROW(pot_config = model->get_rt_info<std::string>("optimization", "config"));
        EXPECT_EQ(pot_config, pot_conf_ref);

        ov::AnyMap cli_map;
        EXPECT_NO_THROW(cli_map = model->get_rt_info<ov::AnyMap>("conversion_parameters"));
        auto it = cli_map.find("input_shape");
        ASSERT_NE(it, cli_map.end());
        EXPECT_TRUE(it->second.is<std::string>());
        EXPECT_EQ(it->second.as<std::string>(), "[1, 3, 22, 22]");

        it = cli_map.find("transform");
        ASSERT_NE(it, cli_map.end());
        EXPECT_TRUE(it->second.is<std::string>());
        EXPECT_EQ(it->second.as<std::string>(), "");

        it = cli_map.find("use_new_frontend");
        ASSERT_NE(it, cli_map.end());
        EXPECT_TRUE(it->second.is<std::string>());
        EXPECT_EQ(it->second.as<std::string>(), "False");
    }

    void TearDown() override {
        std::remove(m_out_xml_path.c_str());
        std::remove(m_out_bin_path.c_str());
    }
};

TEST_F(MetaDataSerialize, get_meta_serialized_without_init) {
    auto model = ov::test::readModel(ir_with_meta);

    {
        auto& rt_info = model->get_rt_info();
        ASSERT_FALSE(rt_info.empty());
    }

    // Serialize the model
    ov::serialize(model, m_out_xml_path, m_out_bin_path);

    auto s_model = ov::test::readModel(m_out_xml_path, m_out_bin_path);
    {
        auto& rt_info = s_model->get_rt_info();
        ASSERT_FALSE(rt_info.empty());
        check_meta_info(s_model);
    }
}

TEST_F(MetaDataSerialize, get_meta_serialized_with_init) {
    auto model = ov::test::readModel(ir_with_meta);

    {
        auto& rt_info = model->get_rt_info();
        ASSERT_FALSE(rt_info.empty());
        check_meta_info(model);
    }

    // Serialize the model
    ov::serialize(model, m_out_xml_path, m_out_bin_path);

    auto s_model = ov::test::readModel(m_out_xml_path, m_out_bin_path);
    {
        auto& rt_info = s_model->get_rt_info();
        ASSERT_FALSE(rt_info.empty());
        check_meta_info(s_model);
    }
}

TEST_F(MetaDataSerialize, get_meta_serialized_changed_meta) {
    auto model = ov::test::readModel(ir_with_meta);

    {
        auto& rt_info = model->get_rt_info();
        ASSERT_FALSE(rt_info.empty());
        check_meta_info(model);
        // Add new property to meta information
        model->set_rt_info("my_value", "meta_data", "my_property");
    }

    // Serialize the model
    ov::serialize(model, m_out_xml_path, m_out_bin_path);

    auto s_model = ov::test::readModel(m_out_xml_path, m_out_bin_path);
    {
        std::string prop;
        EXPECT_NO_THROW(prop = model->get_rt_info<std::string>("meta_data", "my_property"));
        EXPECT_EQ(prop, "my_value");

        auto& rt_info = s_model->get_rt_info();
        ASSERT_NE(rt_info.find("meta_data"), rt_info.end());
        check_meta_info(s_model);
    }
}

TEST_F(MetaDataSerialize, set_complex_meta_information) {
    const auto check_rt_info = [](const std::shared_ptr<ov::Model>& model) {
        EXPECT_TRUE(model->has_rt_info("config", "type_of_model"));
        EXPECT_TRUE(model->has_rt_info("config", "converter_type"));
        EXPECT_TRUE(model->has_rt_info("config", "model_parameters", "threshold"));
        EXPECT_TRUE(model->has_rt_info("config", "model_parameters", "min"));
        EXPECT_TRUE(model->has_rt_info("config", "model_parameters", "max"));
        EXPECT_TRUE(model->has_rt_info("config", "model_parameters", "labels", "label_tree", "type"));
        EXPECT_TRUE(model->has_rt_info("config", "model_parameters", "labels", "label_tree", "directed"));
        EXPECT_TRUE(model->has_rt_info("config", "model_parameters", "labels", "label_tree", "nodes"));
        EXPECT_TRUE(model->has_rt_info("config", "model_parameters", "labels", "label_tree", "float_empty"));
        EXPECT_TRUE(model->has_rt_info("config", "model_parameters", "labels", "label_groups", "ids"));
        EXPECT_TRUE(model->has_rt_info("config", "model_parameters", "mean_values"));

        EXPECT_EQ("classification", model->get_rt_info<std::string>("config", "type_of_model"));
        EXPECT_EQ("classification", model->get_rt_info<std::string>("config", "converter_type"));
        EXPECT_GE(0.0001f, model->get_rt_info<float>("config", "model_parameters", "threshold") - 13.23f);
        EXPECT_GE(0.0001f, model->get_rt_info<float>("config", "model_parameters", "min") - (-3.245433f));
        EXPECT_GE(0.0001f, model->get_rt_info<float>("config", "model_parameters", "max") - 3.2342233f);
        EXPECT_EQ("tree",
                  model->get_rt_info<std::string>("config", "model_parameters", "labels", "label_tree", "type"));
        EXPECT_EQ(true, model->get_rt_info<bool>("config", "model_parameters", "labels", "label_tree", "directed"));
        EXPECT_EQ(std::vector<std::string>{},
                  model->get_rt_info<std::vector<std::string>>("config",
                                                               "model_parameters",
                                                               "labels",
                                                               "label_tree",
                                                               "nodes"));
        EXPECT_EQ(std::vector<float>{},
                  model->get_rt_info<std::vector<float>>("config",
                                                         "model_parameters",
                                                         "labels",
                                                         "label_tree",
                                                         "float_empty"));
        std::vector<std::string> str_vec{"sasd", "fdfdfsdf"};
        EXPECT_EQ(str_vec,
                  model->get_rt_info<std::vector<std::string>>("config",
                                                               "model_parameters",
                                                               "labels",
                                                               "label_groups",
                                                               "ids"));
        std::vector<float> fl_vec{22.3f, 33.11f, 44.f};
        EXPECT_EQ(fl_vec, model->get_rt_info<std::vector<float>>("config", "model_parameters", "mean_values"));
    };

    auto model = ov::test::readModel(ir_with_meta);

    {
        auto& rt_info = model->get_rt_info();
        ASSERT_FALSE(rt_info.empty());
        check_meta_info(model);
        // Fill meta data
        model->set_rt_info("classification", "config", "type_of_model");
        model->set_rt_info("classification", "config", "converter_type");
        model->set_rt_info(13.23f, "config", "model_parameters", "threshold");
        model->set_rt_info(-3.245433f, "config", "model_parameters", "min");
        model->set_rt_info(3.2342233f, "config", "model_parameters", "max");
        model->set_rt_info("tree", "config", "model_parameters", "labels", "label_tree", "type");
        model->set_rt_info(true, "config", "model_parameters", "labels", "label_tree", "directed");
        model->set_rt_info(std::vector<float>{}, "config", "model_parameters", "labels", "label_tree", "float_empty");
        model->set_rt_info(std::vector<std::string>{}, "config", "model_parameters", "labels", "label_tree", "nodes");
        model->set_rt_info(std::vector<std::string>{"sasd", "fdfdfsdf"},
                           "config",
                           "model_parameters",
                           "labels",
                           "label_groups",
                           "ids");
        model->set_rt_info(std::vector<float>{22.3f, 33.11f, 44.f}, "config", "model_parameters", "mean_values");

        check_rt_info(model);
    }

    // Serialize the model
    ov::serialize(model, m_out_xml_path, m_out_bin_path);

    auto s_model = ov::test::readModel(m_out_xml_path, m_out_bin_path);
    {
        check_meta_info(s_model);
        check_rt_info(s_model);
    }
}

// After deprecating undefined type, test whether the serialization of replacing undefined type with dynamic type is
// equivalent.
class UndefinedTypeDynamicTypeSerializationTests : public ::testing::Test {
public:
    // Customize a model with a dynamic type
    std::string dynamic_type_ir = R"V0G0N(<?xml version="1.0"?>
<net name="custom_model" version="11">
    <layers>
        <layer id="0" name="Parameter_1" type="Parameter" version="opset1">
            <data shape="1,1,128" element_type="f32" />
            <output>
                <port id="0" precision="FP32" names="Parameter_1">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="1" name="Relu_2" type="ReLU" version="opset1">
            <input>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </input>
            <output>
                <port id="1" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="2" name="ReadValue_3" type="ReadValue" version="opset6">
            <data variable_id="my_var" variable_type="dynamic" variable_shape="..." />
            <input>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </input>
            <output>
                <port id="1" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="3" name="Assign_4" type="Assign" version="opset6">
            <data variable_id="my_var" />
            <input>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </input>
            <output>
                <port id="1" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="4" name="Squeeze_5" type="Squeeze" version="opset1">
            <input>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </input>
            <output>
                <port id="1" precision="FP32" names="Output_5">
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="5" name="Result_6" type="Result" version="opset1">
            <input>
                <port id="0" precision="FP32">
                    <dim>128</dim>
                </port>
            </input>
        </layer>
    </layers>
    <edges>
        <edge from-layer="0" from-port="0" to-layer="1" to-port="0" />
        <edge from-layer="1" from-port="1" to-layer="2" to-port="0" />
        <edge from-layer="2" from-port="1" to-layer="3" to-port="0" />
        <edge from-layer="3" from-port="1" to-layer="4" to-port="0" />
        <edge from-layer="4" from-port="1" to-layer="5" to-port="0" />
    </edges>
    <rt_info />
</net>
)V0G0N";

    // Customize a model with a undefined type
    std::string undefined_type_ir = R"V0G0N(<?xml version="1.0"?>
<net name="custom_model" version="11">
    <layers>
        <layer id="0" name="Parameter_1" type="Parameter" version="opset1">
            <data shape="1,1,128" element_type="f32" />
            <output>
                <port id="0" precision="FP32" names="Parameter_1">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="1" name="Relu_2" type="ReLU" version="opset1">
            <input>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </input>
            <output>
                <port id="1" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="2" name="ReadValue_3" type="ReadValue" version="opset6">
            <data variable_id="my_var" variable_type="undefined" variable_shape="..." />
            <input>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </input>
            <output>
                <port id="1" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="3" name="Assign_4" type="Assign" version="opset6">
            <data variable_id="my_var" />
            <input>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </input>
            <output>
                <port id="1" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="4" name="Squeeze_5" type="Squeeze" version="opset1">
            <input>
                <port id="0" precision="FP32">
                    <dim>1</dim>
                    <dim>1</dim>
                    <dim>128</dim>
                </port>
            </input>
            <output>
                <port id="1" precision="FP32" names="Output_5">
                    <dim>128</dim>
                </port>
            </output>
        </layer>
        <layer id="5" name="Result_6" type="Result" version="opset1">
            <input>
                <port id="0" precision="FP32">
                    <dim>128</dim>
                </port>
            </input>
        </layer>
    </layers>
    <edges>
        <edge from-layer="0" from-port="0" to-layer="1" to-port="0" />
        <edge from-layer="1" from-port="1" to-layer="2" to-port="0" />
        <edge from-layer="2" from-port="1" to-layer="3" to-port="0" />
        <edge from-layer="3" from-port="1" to-layer="4" to-port="0" />
        <edge from-layer="4" from-port="1" to-layer="5" to-port="0" />
    </edges>
    <rt_info />
</net>
)V0G0N";

    std::filesystem::path m_dynamic_type_out_xml_path;
    std::filesystem::path m_dynamic_type_out_bin_path;
    std::filesystem::path m_undefined_type_out_xml_path;
    std::filesystem::path m_undefined_type_out_bin_path;

    void SetUp() override {
        std::string filePrefix = ov::test::utils::generateTestFilePrefix();
        m_undefined_type_out_xml_path = filePrefix + "_undefined.xml";
        m_undefined_type_out_bin_path = filePrefix + "_undefined.bin";
        m_dynamic_type_out_xml_path = filePrefix + "_dynamic.xml";
        m_dynamic_type_out_bin_path = filePrefix + "_dynamic.bin";
    }

    void TearDown() override {
        if (std::filesystem::exists(m_undefined_type_out_xml_path)) {
            std::filesystem::remove(m_undefined_type_out_xml_path);
        }

        if (std::filesystem::exists(m_undefined_type_out_bin_path)) {
            std::filesystem::remove(m_undefined_type_out_bin_path);
        }

        if (std::filesystem::exists(m_dynamic_type_out_xml_path)) {
            std::filesystem::remove(m_dynamic_type_out_xml_path);
        }

        if (std::filesystem::exists(m_dynamic_type_out_bin_path)) {
            std::filesystem::remove(m_dynamic_type_out_bin_path);
        }
    }

    bool files_equal(const std::filesystem::path& file_path1, const std::filesystem::path& file_path2) {
        std::ifstream xml_dynamic(file_path1.string(), std::ios::binary);
        std::ifstream xml_undefined(file_path2.string(), std::ios::binary);

        if (!xml_dynamic.good() || !xml_undefined.good()) {
            return false;
        }

        return std::equal(std::istreambuf_iterator<char>(xml_dynamic.rdbuf()),
                          std::istreambuf_iterator<char>(),
                          std::istreambuf_iterator<char>(xml_undefined.rdbuf()));
    }
};

// check stringstream serialize
TEST_F(UndefinedTypeDynamicTypeSerializationTests, compare_dynamic_type_undefined_type_serialization_stringstream) {
    std::stringstream dynamicXmlStream, undefinedXmlStream, dynamicBinStream, undefinedBinStream;

    // Test whether the serialization results of the two models are the same
    auto dynamicTypeModel = ov::test::readModel(dynamic_type_ir);
    auto undefinedTypeModel = ov::test::readModel(undefined_type_ir);

    // compile the serialized models with stringstream type
    ov::pass::Serialize(dynamicXmlStream, dynamicBinStream).run_on_model(dynamicTypeModel);
    ov::pass::Serialize(undefinedXmlStream, undefinedBinStream).run_on_model(undefinedTypeModel);

    ASSERT_EQ(dynamicXmlStream.str(), undefinedXmlStream.str())
        << "Serialized XML streams are different: dynamic type vs undefined type";
}

// check xml serialize
TEST_F(UndefinedTypeDynamicTypeSerializationTests, compare_dynamic_type_undefined_type_serialization_file) {
    auto dynamicTypeModel = ov::test::readModel(dynamic_type_ir);
    auto undefinedTypeModel = ov::test::readModel(undefined_type_ir);

    ov::pass::Serialize(m_dynamic_type_out_xml_path, m_dynamic_type_out_bin_path).run_on_model(dynamicTypeModel);
    ov::pass::Serialize(m_undefined_type_out_xml_path, m_undefined_type_out_bin_path).run_on_model(undefinedTypeModel);

    ASSERT_TRUE(files_equal(m_dynamic_type_out_xml_path, m_undefined_type_out_xml_path))
        << "Serialized XML files are different: dynamic type vs undefined type";
}
