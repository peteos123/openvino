// -*- coding: utf-8 -*-
// Copyright (C) 2018-2025 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

const { addon: ov } = require("../..");
const assert = require("assert");
const { describe, it, before, beforeEach } = require("node:test");
const { testModels, isModelAvailable } = require("../utils.js");

describe("ov.Model tests", () => {
  const { testModelFP32, addModel, addModelWithVar } = testModels;
  let core = null;
  let model = null;

  before(async () => {
    await isModelAvailable(testModelFP32);
    core = new ov.Core();
  });

  beforeEach(() => {
    model = core.readModelSync(testModelFP32.xml);
  });

  describe("Model.isDynamic()", () => {
    it("should return a boolean value type", () => {
      const result = model.isDynamic();
      assert.strictEqual(typeof result, "boolean", "isDynamic() should return a boolean value");
    });

    it("should not accept any arguments", () => {
      assert.throws(
        () => {
          model.isDynamic("unexpected argument");
        },
        /^Error: isDynamic\(\) does not accept any arguments\.$/,
        "Expected isDynamic to throw an error when called with arguments",
      );
    });

    it("returns false for a static model", () => {
      const expectedStatus = false;
      assert.strictEqual(
        model.isDynamic(),
        expectedStatus,
        "Expected isDynamic to return false for a static model",
      );
    });
  });

  describe("Model.getFriendlyName()", () => {
    it("returns model unique name if no friendly name is set", () => {
      const expectedName = "test_model";
      assert.strictEqual(model.getFriendlyName(), expectedName);
    });
    it("throws an error when called with arguments", () => {
      assert.throws(
        () => model.getFriendlyName("unexpected argument"),
        /getFriendlyName\(\) does not take any arguments/,
      );
    });
  });
  describe("Model.setFriendlyName()", () => {
    it("throws an error when called without a string argument", () => {
      assert.throws(
        () => model.setFriendlyName(),
        /Expected a single string argument for the friendly name/,
      );
      assert.throws(
        () => model.setFriendlyName(123),
        /Expected a single string argument for the friendly name/,
      );
    });

    it("throws an error when called with multiple arguments", () => {
      assert.throws(
        () => model.setFriendlyName("Name1", "Name2"),
        /Expected a single string argument for the friendly name/,
      );
    });

    it("returns the set friendly name of the model", () => {
      const friendlyName = "MyFriendlyModel";
      model.setFriendlyName(friendlyName);
      assert.strictEqual(model.getFriendlyName(), friendlyName);
    });

    it("retains the last set friendly name when set multiple times", () => {
      model.setFriendlyName("InitialName");
      model.setFriendlyName("FinalName");
      assert.strictEqual(model.getFriendlyName(), "FinalName");
    });

    it("handles setting an empty string as a friendly name", () => {
      assert.doesNotThrow(() => model.setFriendlyName(""));
      assert.strictEqual(model.getFriendlyName(), model.getName());
    });
  });

  describe("Model.getOutputSize()", () => {
    it("getOutputSize() should return a number", () => {
      const result = model.getOutputSize();
      assert.strictEqual(typeof result, "number");
    });

    it("should not accept any arguments", () => {
      assert.throws(() => {
        model.getOutputSize("unexpected argument");
      }, /^Error: getOutputSize\(\) does not accept any arguments\.$/);
    });

    it("should return 1 for the default model", () => {
      assert.strictEqual(model.getOutputSize(), 1);
    });
  });

  describe("Model.getOutputElementType()", () => {
    it("should return a string", () => {
      const result = model.getOutputElementType(0);
      assert.strictEqual(typeof result, "string");
    });

    it("should accept a single integer argument", () => {
      assert.throws(
        () => {
          model.getOutputElementType();
        },
        /'getOutputElementType' method called with incorrect parameters/,
        "Should throw when called without arguments",
      );

      assert.throws(
        () => {
          model.getOutputElementType("unexpected argument");
        },
        /'getOutputElementType' method called with incorrect parameters/,
        "Should throw on non-number argument",
      );

      assert.throws(
        () => {
          model.getOutputElementType(0, 1);
        },
        /'getOutputElementType' method called with incorrect parameters/,
        "Should throw on multiple arguments",
      );

      assert.throws(
        () => {
          model.getOutputElementType(3.14);
        },
        /'getOutputElementType' method called with incorrect parameters/,
        "Should throw on non-integer number",
      );
    });

    it("should return a valid element type for the default model", () => {
      const elementType = model.getOutputElementType(0);
      assert.ok(
        typeof elementType === "string" && elementType.length > 0,
        `Expected a non-empty string, got ${elementType}`,
      );
    });

    it("should throw an error for out-of-range index", () => {
      const outputSize = model.getOutputSize();
      assert.throws(() => {
        model.getOutputElementType(outputSize);
      }, /^Error: /);
    });
  });

  describe("Model.clone()", () => {
    it("should return an object of type model", () => {
      const clonedModel = model.clone();
      assert.ok(clonedModel instanceof ov.Model);
    });

    it("should return a model that is a clone of the calling model", () => {
      const clonedModel = model.clone();
      assert.deepStrictEqual(clonedModel, model);
    });

    it("should not accept any arguments", () => {
      assert.throws(
        () => model.clone("Unexpected argument").then(),
        /'clone' method called with incorrect parameters./,
      );
    });
  });

  describe("Model.reshape()", () => {
    const pShape = "[?,?,1..3,224]";
    const pShape14 = new ov.PartialShape("1, 4");
    const varId = "ID1";

    it("test reshape with PartialShape obj", () => {
      const partialShape = new ov.PartialShape(pShape);
      const reshapedModel = model.reshape(partialShape);
      assert.ok(reshapedModel instanceof ov.Model);

      const newShape = reshapedModel.input().getPartialShape();
      assert.ok(newShape instanceof ov.PartialShape);
      assert.deepStrictEqual(newShape.toString(), pShape);
    });

    it("test reshape with PartialShape string", () => {
      const reshapedModel = model.reshape(pShape);
      assert.ok(reshapedModel instanceof ov.Model);
      const newShape = reshapedModel.input().getPartialShape();
      assert.deepStrictEqual(newShape.toString(), pShape);
    });

    it("should not accept empty arguments", () => {
      assert.throws(() => model.reshape(), /'reshape' method called with incorrect parameters./);
    });

    it("test reshape with ports", () => {
      const model = core.readModelSync(addModel.xml);
      const inputShapePairs = model.inputs.map((input) => [input, pShape14]);
      model.reshape(new Map(inputShapePairs));
      for (const modelInput of model.inputs) {
        assert.deepStrictEqual(modelInput.getPartialShape().toString(), pShape14.toString());
      }
    });

    it("test reshape with indexes", () => {
      const model = core.readModelSync(addModel.xml);
      model.reshape(
        new Map([
          [0, pShape14],
          [1, pShape14],
        ]),
      );
      for (const modelInput of model.inputs) {
        assert.deepStrictEqual(modelInput.getPartialShape().toString(), pShape14.toString());
      }
    });

    it("test reshape with names", () => {
      const model = core.readModelSync(addModel.xml);
      const shapesArr = model.inputs.map((input) => [input.anyName, pShape14]);
      model.reshape(new Map(shapesArr));
      for (const modelInput of model.inputs) {
        assert.deepStrictEqual(modelInput.getPartialShape().toString(), pShape14.toString());
      }
    });

    it("test reshape with ports and variable", () => {
      const model = core.readModelSync(addModelWithVar.xml);
      const newShape = new ov.PartialShape("46, 1");
      const shapesArr = model.inputs.map((input) => [input, newShape]);
      model.reshape(new Map(shapesArr), { [varId]: newShape });
      for (const modelInput of model.inputs) {
        assert.deepStrictEqual(modelInput.getPartialShape().toString(), newShape.toString());
      }
    });

    it("test reshape with indexes and variable", () => {
      const model = core.readModelSync(addModelWithVar.xml);
      const pShape14 = new ov.PartialShape("1, 4");
      model.reshape(
        new Map([
          [0, pShape14],
          [1, pShape14],
        ]),
        { [varId]: pShape14 },
      );
      for (const modelInput of model.inputs) {
        assert.deepStrictEqual(modelInput.getPartialShape().toString(), pShape14.toString());
      }
    });

    it("test reshape with names and variable", () => {
      const model = core.readModelSync(addModelWithVar.xml);
      const shapesArr = model.inputs.map((input) => [input.anyName, pShape14]);
      model.reshape(new Map(shapesArr), { [varId]: pShape14 });
      for (const modelInput of model.inputs) {
        assert.deepStrictEqual(modelInput.getPartialShape().toString(), pShape14.toString());
      }
    });
  });
});
