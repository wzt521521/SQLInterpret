#pragma once
#include "minisql/plan.h"

namespace minisql {

// The real database supplies these interfaces. The compiler never owns pages.
class Executor {
 public:
  virtual ~Executor() = default;
  virtual ExecutionResult execute(const PlanPtr& plan) = 0;
};

struct Record {
  std::uint64_t page_id{0};
  std::uint32_t slot_id{0};
  Row values;
};

enum class ReadStatus { RowAvailable, End };
class RowIterator {
 public:
  virtual ~RowIterator() = default;
  virtual void init() = 0;
  virtual ReadStatus read(Record& record) = 0;
  // Execution failures throw DBError; no ambiguous end/error boolean.
};

// Shared typed expression contract for optimizer and the external executor.
// Reads only the supplied row; integer arithmetic is checked for overflow.
Value evaluate_expression(const ExprPtr& expression, const Row& row);

}  // namespace minisql
