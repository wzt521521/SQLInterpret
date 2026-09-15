#pragma once

#include "minisql/common.h"

#include <string>

namespace minidbms {

[[noreturn]] inline void execution_error(const std::string& code, const std::string& message) {
    throw minisql::DBError(minisql::Stage::Execution, code, message);
}

}  // namespace minidbms
