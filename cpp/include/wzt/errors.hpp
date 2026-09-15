#pragma once

#include <stdexcept>
#include <string>

namespace wzt {

class StorageError : public std::runtime_error {
public:
    StorageError(std::string code, std::string message);

    [[nodiscard]] const std::string& code() const noexcept { return code_; }
    [[nodiscard]] const std::string& stage() const noexcept { return stage_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    std::string stage_{"STORAGE"};
    std::string code_;
    std::string message_;
};

}  // namespace wzt
