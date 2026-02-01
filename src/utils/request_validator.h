#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace orpheus {
namespace utils {

/**
 * RequestValidator - Common validation patterns for MCP handlers
 *
 * Builder pattern for validating JSON request parameters.
 * Short-circuits on first error for clean error messages.
 *
 * Usage:
 *   RequestValidator validator(req);
 *   if (!validator.RequirePid().RequireAddress().RequireDMA(dma).IsValid()) {
 *       return CreateErrorResponse(validator.GetError());
 *   }
 *   uint32_t pid = validator.GetPid();
 *   uint64_t address = validator.GetAddress();
 */
class RequestValidator {
public:
    explicit RequestValidator(const nlohmann::json& req) : req_(req) {}

    // Parameter requirements - chainable
    RequestValidator& RequirePid() {
        if (!error_.empty()) return *this;
        pid_ = req_.value("pid", 0u);
        if (pid_ == 0) {
            error_ = "Missing required parameter: pid";
        }
        return *this;
    }

    RequestValidator& RequireAddress(const std::string& param_name = "address") {
        if (!error_.empty()) return *this;
        if (req_.contains(param_name)) {
            auto& val = req_[param_name];
            if (val.is_string()) {
                address_ = ParseAddress(val.get<std::string>());
            } else if (val.is_number()) {
                address_ = val.get<uint64_t>();
            }
        }
        if (address_ == 0) {
            error_ = "Missing required parameter: " + param_name;
        }
        return *this;
    }

    RequestValidator& RequireSize(const std::string& param_name = "size",
                                   uint32_t max_size = 16 * 1024 * 1024) {
        if (!error_.empty()) return *this;
        size_ = req_.value(param_name, 0u);
        if (size_ == 0) {
            error_ = "Missing required parameter: " + param_name;
        } else if (size_ > max_size) {
            error_ = param_name + " too large: maximum is " + std::to_string(max_size) + " bytes";
        }
        return *this;
    }

    // Validate address is not NULL
    RequestValidator& RequireNonNullAddress() {
        if (!error_.empty()) return *this;
        if (address_ == 0) {
            error_ = "Invalid address: NULL pointer (0x0)";
        }
        return *this;
    }

    // Validate address looks like a valid usermode pointer
    RequestValidator& RequireUsermodeAddress() {
        if (!error_.empty()) return *this;
        if (address_ == 0) {
            error_ = "Invalid address: NULL pointer (0x0)";
        } else if (address_ < 0x10000) {
            // Very low addresses are typically reserved
            error_ = "Invalid address: value too low (likely invalid)";
        }
        return *this;
    }

    RequestValidator& RequireString(const std::string& param_name, std::string& out) {
        if (!error_.empty()) return *this;
        if (!req_.contains(param_name) || !req_[param_name].is_string() ||
            req_[param_name].get<std::string>().empty()) {
            error_ = "Missing required parameter: " + param_name;
        } else {
            out = req_[param_name].get<std::string>();
        }
        return *this;
    }

    RequestValidator& RequireU32(const std::string& param_name, uint32_t& out) {
        if (!error_.empty()) return *this;
        if (!req_.contains(param_name)) {
            error_ = "Missing required parameter: " + param_name;
        } else {
            out = req_[param_name].get<uint32_t>();
        }
        return *this;
    }

    // DMA validation - takes pointer to check
    template<typename DMA>
    RequestValidator& RequireDMA(DMA* dma) {
        if (!error_.empty()) return *this;
        if (!dma || !dma->IsConnected()) {
            error_ = "DMA not connected";
        }
        return *this;
    }

    // Custom validation with lambda
    RequestValidator& Require(bool condition, const std::string& error_msg) {
        if (!error_.empty()) return *this;
        if (!condition) {
            error_ = error_msg;
        }
        return *this;
    }

    // Check result
    bool IsValid() const { return error_.empty(); }
    const std::string& GetError() const { return error_; }

    // Accessors for validated values
    uint32_t GetPid() const { return pid_; }
    uint64_t GetAddress() const { return address_; }
    uint32_t GetSize() const { return size_; }

    // Optional parameter accessors with defaults
    template<typename T>
    T Get(const std::string& param, T default_val) const {
        return req_.value(param, default_val);
    }

    bool Has(const std::string& param) const {
        return req_.contains(param);
    }

private:
    static uint64_t ParseAddress(const std::string& str) {
        if (str.empty()) return 0;
        try {
            size_t pos = 0;
            if (str.substr(0, 2) == "0x" || str.substr(0, 2) == "0X") {
                return std::stoull(str, &pos, 16);
            }
            return std::stoull(str, &pos, 0);
        } catch (...) {
            return 0;
        }
    }

    const nlohmann::json& req_;
    std::string error_;
    uint32_t pid_ = 0;
    uint64_t address_ = 0;
    uint32_t size_ = 0;
};

} // namespace utils
} // namespace orpheus
