#include <gtest/gtest.h>
#include "utils/expression_evaluator.h"

using namespace orpheus::utils;

// Helper: create evaluator with no module/memory resolvers (pure arithmetic)
static ExpressionEvaluator MakeEval() {
    return ExpressionEvaluator(
        [](const std::string&) -> std::optional<uint64_t> { return std::nullopt; },
        [](uint64_t) -> std::optional<uint64_t> { return std::nullopt; },
        nullptr
    );
}

// Helper: create evaluator with a module resolver
static ExpressionEvaluator MakeEvalWithModules(
    std::map<std::string, uint64_t> modules
) {
    return ExpressionEvaluator(
        [modules](const std::string& name) -> std::optional<uint64_t> {
            auto it = modules.find(name);
            if (it != modules.end()) return it->second;
            return std::nullopt;
        },
        [](uint64_t) -> std::optional<uint64_t> { return std::nullopt; },
        nullptr
    );
}

TEST(ExpressionEvaluator, ParseHexAddress) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("0x1000");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x1000u);
}

TEST(ExpressionEvaluator, ParseHexWithPrefix0X) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("0X1000");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x1000u);
}

TEST(ExpressionEvaluator, ParseBareHexNumber) {
    auto eval = MakeEval();
    // Bare numbers starting with a digit are parsed as hex
    auto result = eval.Evaluate("1000");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x1000u);
}

TEST(ExpressionEvaluator, Addition) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("0x1000+0x100");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x1100u);
}

TEST(ExpressionEvaluator, Subtraction) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("0x2000-0x100");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x1F00u);
}

TEST(ExpressionEvaluator, Multiplication) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("0x10*0x10");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x100u);
}

TEST(ExpressionEvaluator, Division) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("0x100/0x10");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x10u);
}

TEST(ExpressionEvaluator, DivisionByZero) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("0x100/0x0");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(eval.GetError(), "Division by zero");
}

TEST(ExpressionEvaluator, Parentheses) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("(0x10+0x10)*0x2");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x40u);
}

TEST(ExpressionEvaluator, OperatorPrecedence) {
    auto eval = MakeEval();
    // 0x10 + 0x10 * 0x2 = 0x10 + 0x20 = 0x30
    auto result = eval.Evaluate("0x10+0x10*0x2");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x30u);
}

TEST(ExpressionEvaluator, EmptyExpression) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("");
    EXPECT_FALSE(result.has_value());
}

TEST(ExpressionEvaluator, WhitespaceOnly) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("   ");
    EXPECT_FALSE(result.has_value());
}

TEST(ExpressionEvaluator, Variables) {
    auto eval = MakeEval();
    eval.SetVariable("myvar", 0x5000);
    auto result = eval.Evaluate("$myvar+0x100");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x5100u);
}

TEST(ExpressionEvaluator, UnknownVariable) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("$nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST(ExpressionEvaluator, ClearVariables) {
    auto eval = MakeEval();
    eval.SetVariable("x", 42);
    eval.ClearVariables();
    auto result = eval.Evaluate("$x");
    EXPECT_FALSE(result.has_value());
}

TEST(ExpressionEvaluator, ModuleResolverWorks) {
    auto eval = MakeEvalWithModules({{"client.dll", 0x7FF600000000}});
    auto result = eval.Evaluate("client.dll+0x1000");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x7FF600001000u);
}

TEST(ExpressionEvaluator, UnknownModuleFails) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("unknown.dll");
    EXPECT_FALSE(result.has_value());
}

TEST(ExpressionEvaluator, DereferenceWithoutReaderFails) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("[0x1000]");
    EXPECT_FALSE(result.has_value());
}

TEST(ExpressionEvaluator, DereferenceWithReader) {
    ExpressionEvaluator eval(
        [](const std::string&) -> std::optional<uint64_t> { return std::nullopt; },
        [](uint64_t addr) -> std::optional<uint64_t> {
            if (addr == 0x1000) return 0xDEADBEEF;
            return std::nullopt;
        },
        nullptr
    );
    auto result = eval.Evaluate("[0x1000]");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0xDEADBEEFu);
}

TEST(ExpressionEvaluator, ComplexExpression) {
    auto eval = MakeEvalWithModules({{"base.dll", 0x10000}});
    eval.SetVariable("offset", 0x200);
    auto result = eval.Evaluate("base.dll+$offset+0x50");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x10250u);
}

TEST(ExpressionEvaluator, UnmatchedParen) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("(0x10+0x10");
    EXPECT_FALSE(result.has_value());
}

TEST(ExpressionEvaluator, LargeAddress) {
    auto eval = MakeEval();
    auto result = eval.Evaluate("0x7FFFFFFFFFFFFFFF");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0x7FFFFFFFFFFFFFFFu);
}
