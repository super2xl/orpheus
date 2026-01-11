#include "expression_evaluator.h"
#include <cctype>
#include <algorithm>
#include <sstream>

namespace orpheus::utils {

ExpressionEvaluator::ExpressionEvaluator(
    ModuleResolver module_resolver,
    MemoryReader memory_reader,
    RegisterResolver register_resolver
)
    : module_resolver_(std::move(module_resolver))
    , memory_reader_(std::move(memory_reader))
    , register_resolver_(std::move(register_resolver)) {
}

void ExpressionEvaluator::SetVariable(const std::string& name, uint64_t value) {
    variables_[name] = value;
}

void ExpressionEvaluator::ClearVariables() {
    variables_.clear();
}

std::vector<ExpressionEvaluator::Token> ExpressionEvaluator::Tokenize(const std::string& expr) {
    std::vector<Token> tokens;
    size_t i = 0;

    while (i < expr.size()) {
        // Skip whitespace
        if (std::isspace(expr[i])) {
            i++;
            continue;
        }

        // Single character tokens
        switch (expr[i]) {
            case '+': tokens.push_back({TokenType::Plus, "+"}); i++; continue;
            case '-': tokens.push_back({TokenType::Minus, "-"}); i++; continue;
            case '*': tokens.push_back({TokenType::Star, "*"}); i++; continue;
            case '/': tokens.push_back({TokenType::Slash, "/"}); i++; continue;
            case '(': tokens.push_back({TokenType::LParen, "("}); i++; continue;
            case ')': tokens.push_back({TokenType::RParen, ")"}); i++; continue;
            case '[': tokens.push_back({TokenType::LBracket, "["}); i++; continue;
            case ']': tokens.push_back({TokenType::RBracket, "]"}); i++; continue;
        }

        // Variable ($name)
        if (expr[i] == '$') {
            i++;
            size_t start = i;
            while (i < expr.size() && (std::isalnum(expr[i]) || expr[i] == '_')) {
                i++;
            }
            tokens.push_back({TokenType::Variable, expr.substr(start, i - start)});
            continue;
        }

        // Identifier (module name, register name) - MUST check before bare hex
        // because letters like 'c', 'a', 'b', 'd', 'e', 'f' are valid hex digits
        if (std::isalpha(expr[i]) || expr[i] == '_') {
            size_t start = i;
            while (i < expr.size() && (std::isalnum(expr[i]) || expr[i] == '_' || expr[i] == '.')) {
                i++;
            }
            tokens.push_back({TokenType::Identifier, expr.substr(start, i - start)});
            continue;
        }

        // Hex number (0x...)
        if (i + 1 < expr.size() && expr[i] == '0' && (expr[i+1] == 'x' || expr[i+1] == 'X')) {
            size_t start = i;
            i += 2;
            while (i < expr.size() && std::isxdigit(expr[i])) {
                i++;
            }
            std::string text = expr.substr(start, i - start);
            Token tok;
            tok.type = TokenType::Number;
            tok.text = text;
            tok.value = std::stoull(text, nullptr, 16);
            tokens.push_back(tok);
            continue;
        }

        // Bare number (must start with digit 0-9) - always treat as hex for reversing context
        if (std::isdigit(expr[i])) {
            size_t start = i;

            while (i < expr.size() && std::isxdigit(expr[i])) {
                i++;
            }

            std::string text = expr.substr(start, i - start);

            Token tok;
            tok.type = TokenType::Number;
            tok.text = text;
            // Always parse as hex - this is a reversing tool, addresses/offsets are hex
            tok.value = std::stoull(text, nullptr, 16);
            tokens.push_back(tok);
            continue;
        }

        // Unknown character - skip
        error_ = "Unexpected character: " + std::string(1, expr[i]);
        i++;
    }

    tokens.push_back({TokenType::End, ""});
    return tokens;
}

std::optional<uint64_t> ExpressionEvaluator::Evaluate(const std::string& expression) {
    error_.clear();
    current_ = 0;

    // Handle empty expression
    std::string trimmed = expression;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

    if (trimmed.empty()) {
        error_ = "Empty expression";
        return std::nullopt;
    }

    tokens_ = Tokenize(trimmed);
    if (!error_.empty()) {
        return std::nullopt;
    }

    auto result = ParseExpression();

    if (result && !IsAtEnd()) {
        error_ = "Unexpected token after expression: " + Peek().text;
        return std::nullopt;
    }

    return result;
}

std::optional<uint64_t> ExpressionEvaluator::ParseExpression() {
    // expression = term (('+' | '-') term)*
    auto left = ParseTerm();
    if (!left) return std::nullopt;

    while (Check(TokenType::Plus) || Check(TokenType::Minus)) {
        bool is_plus = Check(TokenType::Plus);
        Advance();

        auto right = ParseTerm();
        if (!right) return std::nullopt;

        if (is_plus) {
            *left += *right;
        } else {
            *left -= *right;
        }
    }

    return left;
}

std::optional<uint64_t> ExpressionEvaluator::ParseTerm() {
    // term = factor (('*' | '/') factor)*
    auto left = ParseFactor();
    if (!left) return std::nullopt;

    while (Check(TokenType::Star) || Check(TokenType::Slash)) {
        bool is_mul = Check(TokenType::Star);
        Advance();

        auto right = ParseFactor();
        if (!right) return std::nullopt;

        if (is_mul) {
            *left *= *right;
        } else {
            if (*right == 0) {
                error_ = "Division by zero";
                return std::nullopt;
            }
            *left /= *right;
        }
    }

    return left;
}

std::optional<uint64_t> ExpressionEvaluator::ParseFactor() {
    // factor = dereference | primary
    if (Check(TokenType::LBracket)) {
        return ParseDereference();
    }
    return ParsePrimary();
}

std::optional<uint64_t> ExpressionEvaluator::ParseDereference() {
    // dereference = '[' expression ']'
    if (!Match(TokenType::LBracket)) {
        error_ = "Expected '['";
        return std::nullopt;
    }

    auto addr = ParseExpression();
    if (!addr) return std::nullopt;

    if (!Match(TokenType::RBracket)) {
        error_ = "Expected ']'";
        return std::nullopt;
    }

    // Read memory at address
    if (!memory_reader_) {
        error_ = "Memory reader not available";
        return std::nullopt;
    }

    auto value = memory_reader_(*addr);
    if (!value) {
        std::stringstream ss;
        ss << "Failed to read memory at 0x" << std::hex << *addr;
        error_ = ss.str();
        return std::nullopt;
    }

    return value;
}

std::optional<uint64_t> ExpressionEvaluator::ParsePrimary() {
    // primary = number | identifier | variable | '(' expression ')'

    // Parenthesized expression
    if (Match(TokenType::LParen)) {
        auto result = ParseExpression();
        if (!result) return std::nullopt;

        if (!Match(TokenType::RParen)) {
            error_ = "Expected ')'";
            return std::nullopt;
        }
        return result;
    }

    // Number
    if (Check(TokenType::Number)) {
        Token tok = Advance();
        return tok.value;
    }

    // Variable
    if (Check(TokenType::Variable)) {
        Token tok = Advance();
        auto it = variables_.find(tok.text);
        if (it != variables_.end()) {
            return it->second;
        }
        error_ = "Unknown variable: $" + tok.text;
        return std::nullopt;
    }

    // Identifier (module name or register)
    if (Check(TokenType::Identifier)) {
        Token tok = Advance();
        std::string name = tok.text;

        // Convert to lowercase for case-insensitive matching
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Try register first (if resolver available)
        if (register_resolver_) {
            auto reg_val = register_resolver_(lower);
            if (reg_val) {
                return reg_val;
            }
        }

        // Try module resolver
        if (module_resolver_) {
            auto mod_base = module_resolver_(name);
            if (mod_base) {
                return mod_base;
            }
        }

        error_ = "Unknown identifier: " + name;
        return std::nullopt;
    }

    // Unary minus
    if (Match(TokenType::Minus)) {
        auto val = ParsePrimary();
        if (!val) return std::nullopt;
        return static_cast<uint64_t>(-static_cast<int64_t>(*val));
    }

    error_ = "Unexpected token: " + Peek().text;
    return std::nullopt;
}

bool ExpressionEvaluator::Match(TokenType type) {
    if (Check(type)) {
        Advance();
        return true;
    }
    return false;
}

bool ExpressionEvaluator::Check(TokenType type) const {
    if (IsAtEnd()) return type == TokenType::End;
    return Peek().type == type;
}

const ExpressionEvaluator::Token& ExpressionEvaluator::Peek() const {
    return tokens_[current_];
}

const ExpressionEvaluator::Token& ExpressionEvaluator::Advance() {
    if (!IsAtEnd()) current_++;
    return tokens_[current_ - 1];
}

bool ExpressionEvaluator::IsAtEnd() const {
    return current_ >= tokens_.size() || tokens_[current_].type == TokenType::End;
}

} // namespace orpheus::utils
