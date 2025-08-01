// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "cut_specific_model.hpp"

#include "paddle_utils.hpp"

using namespace ov::frontend;

using PaddleCutTest = FrontEndCutModelTest;

static CutModelParam getTestData_2in_2out() {
    CutModelParam res;
    res.m_frontEndName = PADDLE_FE;
    res.m_modelsPath = std::string(TEST_PADDLE_MODELS_DIRNAME);
    res.m_modelName = "2in_2out/2in_2out" + std::string(TEST_PADDLE_MODEL_EXT);
    res.m_oldInputs = {"inputX1", "inputX2"};
    res.m_newInputs = {"add1.tmp_0"};
    if (std::string(TEST_GEN_TAG) == "ge3") {
        res.m_oldOutputs = {"relu3a.tmp_0/Result", "relu3b.tmp_0/Result"};
    } else if (std::string(TEST_GEN_TAG) == "ge2") {
        res.m_oldOutputs = {"save_infer_model/scale_0.tmp_0", "save_infer_model/scale_1.tmp_0"};
    } else {
        // Unexpected TEST_GEN_TAG value. This branch is reached if TEST_GEN_TAG is neither "ge2" nor "ge3".
        throw std::runtime_error("Unsupported TEST_PADDLE_VERSION: " + std::string(TEST_GEN_TAG));
    }
    res.m_newOutputs = {"add2.tmp_0"};
    res.m_tensorValueName = "conv2dX2.tmp_0";
    res.m_tensorValue = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    res.m_op_before_name = "conv2dX2.tmp_0";
    return res;
}

INSTANTIATE_TEST_SUITE_P(PaddleCutTest,
                         FrontEndCutModelTest,
                         ::testing::Values(getTestData_2in_2out()),
                         FrontEndCutModelTest::getTestCaseName);
