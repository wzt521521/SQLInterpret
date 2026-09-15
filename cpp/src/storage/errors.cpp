#include "wzt/errors.hpp"

#include <utility>

namespace wzt {

StorageError::StorageError(std::string code, std::string message)
    : std::runtime_error("STORAGE:" + code + ": " + message),
      code_(std::move(code)),
      message_(std::move(message)) {}

}  // namespace wzt
