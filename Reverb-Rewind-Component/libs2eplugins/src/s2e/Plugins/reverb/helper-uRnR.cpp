#include "helper-uRnR.h"

namespace s2e {
namespace plugins {

using namespace klee;

inline llvm::raw_ostream& operator<<(llvm::raw_ostream& out,
                                     const crash_t type) {
  switch (type) {
    case INPUT_DRAINED:
      out << "input drained";
      break;
    case ISR_DRAINED:
      out << "isr drained";
      break;
    case INVALID_READ:
      out << "invalid read";
      break;
    case INVALID_WRITE:
      out << "invalid write";
      break;
    case INVALID_PC:
      out << "invalid pc";
      break;
    case SYM_PC:
      out << "symbolic pc";
      break;
    case SYM_ADDR:
      out << "symbolic address";
      break;
    default:
      out << "error";
  }
  return out;
}

inline llvm::raw_ostream& operator<<(llvm::raw_ostream& os,
                                     const AnalysisMethod method) {
  switch (method) {
    case AM_SYMPC:
      os << "Symbolic PC";
      break;
    case AM_SYMADDR:
      os << "Over-limit Symbolic Address";
      break;
    case AM_INTEGER:
      os << "Integer Over/Under-flow";
      break;
    case AM_BRANCH:
      os << "Branch Flipping";
      break;
    case AM_NONE:
      os << "Error";
      break;
    default:
      os << "Invalid Analysis Method";
      break;
  }
  return os;
}

inline llvm::raw_ostream& operator<<(llvm::raw_ostream& out,
                                     const InferType type) {
  switch (type) {
    case READ:
      out << "READ";
      break;
    case WRITE:
      out << "WRITE";
      break;
    default:
      out << "error";
  }
  return out;
}

inline llvm::raw_ostream& operator<<(llvm::raw_ostream& out,
                                     const mmio_type type) {
  switch (type) {
    case BITEXTRACT:
      out << "BITEXRACT";
      break;
    case CONSTANT:
      out << "CONSTANT";
      break;
    case PASSTHROUGH:
      out << "PASSTHROUGH";
      break;
    case SET:
      out << "SET";
      break;
    case UNMODELED:
      out << "UNMODELED";
      break;
    default:
      out << "error";
  }
  return out;
}


uint64_t Vec2Val(const std::vector<uint8_t> vec) {
  union {
    uint64_t value;
    uint8_t array[8];
  };
  value = 0;
  for (unsigned i = 0; i < vec.size(); i++) array[i] = vec[i];
  return value;
}

inline llvm::raw_ostream& operator<<(llvm::raw_ostream& os,
                                     const std::vector<uint8_t> vec) {
  os << hexval(Vec2Val(vec));
  return os;
}

inline std::ostream& operator<<(std::ostream& os,
                                const std::vector<uint8_t> vec) {
  os << hexval(Vec2Val(vec));
  return os;
}

inline uint32_t ExprLength(klee::ref<klee::Expr> expr) {
  uint32_t sum_ = 0;
  ExprList stack;
  stack.push_back(expr);
  while (!stack.empty()) {
    ref<Expr> e = stack.back();
    stack.pop_back();
    sum_ += e->getNumKids();
    for (unsigned i = 0; i < e->getNumKids(); i++) {
      ref<Expr> k = e->getKid(i);
      stack.push_back(k);
    }
  }
  return sum_;
}


}  // namespace plugins
}  // namespace s2e