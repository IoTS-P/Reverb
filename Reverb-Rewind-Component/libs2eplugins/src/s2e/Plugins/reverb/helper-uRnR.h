#ifndef S2E_PLUGINS_HELPER_uRnR_H
#define S2E_PLUGINS_HELPER_uRnR_H

#include <klee/Expr.h>
#include <s2e/Utils.h>

#include "klee/util/ExprPPrinter.h"

// for klee:ref hash
#include <cstdint>
#include <functional>

namespace std {
template <>
struct hash<klee::ref<klee::Expr>> {
  size_t operator()(const klee::ref<klee::Expr>& e) const {
    // use pointer as hash value
    return hash<uintptr_t>()(reinterpret_cast<uintptr_t>(e.get()));
  }
};
}  // namespace std

// debug
#define debug_stream(state) \
  s2e()->getDebugStream() << "[State " << state->getID() << "] "

// only support for arm32 now
#define XLEN 32

// constant key in symb_sequence for special condition
#define CONST_KEY (UINT64_MAX - 1)
#define MEM_KEY (UINT64_MAX - 2)
#define LOOP_TIME 3
#define MMIO_BASE 0x40000000
#define MMIO_END 0x60000000
#define NVIC_ADDR 0xe0000000
#define NVIC_END 0xe000efff

///////////////////
// config

#define MAX_SUB_SEARCH_DEPTH 6
#define MAX_CONDITION_LEN 300

namespace s2e {
namespace plugins {

using namespace klee;

typedef std::vector<klee::ref<klee::Expr>> ExprList;

typedef struct expr_pc_ {
  klee::ref<klee::Expr> expr;
  uint64_t mmio_addr;
  uint64_t start_addr;  // the start_addr in input;
} expr_pc;

typedef enum mmio_type_ {
  BITEXTRACT,
  CONSTANT,
  PASSTHROUGH,
  SET,
  UNMODELED,
} mmio_type;

typedef enum crash_reason_ {
  INPUT_DRAINED,
  ISR_DRAINED,
  INVALID_READ,
  INVALID_WRITE,
  INVALID_PC,
  SYM_PC,
  SYM_ADDR,
  DIV_BY_ZERO,
  SUB_UNDERFLOW,
} crash_t;

typedef enum AnalysisMethod {
  AM_FIRST,
  AM_SYMPC = AM_FIRST,  // Symbolic pc
  AM_SYMADDR,           // Over-limit Symbolic Address
  AM_INTEGER,           // Integer Over/Under-flow
  AM_BRANCH,            // Branch Flipping
  AM_NONE,              // error
  AM_LAST = AM_NONE,
} AnalysisMethod;

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

// the query type to ghidra
typedef enum ghidra_msg_ {
  CONSTANT_LOOP,
  FAKE_READ,
} ghidra_msg_t;

// the different part of symbolic name
enum SubSymbName {
  ADDR,   // the address where is read from
  PC,     // the pc where the read appear
  START,  // the start pointer in input vector
  SIZE    // the size of read
};

// different input : user or hardware
enum InMode { USER, HARDWARE };

// how user input in expr
enum ExprUser { ALL, PART, NO };

// ghidra respond type
enum GhidraResp { JUMP, NOT, MAYBE, CONST, FAKE };

enum InferType { READ, WRITE, EXIT };

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

// on piece of records read from yml
typedef struct record_ {
  uint8_t access_size;  // bytes
  uint64_t addr;
  uint64_t pc;

  union {
    uint64_t val;       // val for constant
    uint64_t init_val;  // val for passthrough
    uint64_t mask;      // mask for bitextract
  };

  std::vector<uint64_t> val_set;
  mmio_type type;

} record;

inline uint64_t Vec2Val(const std::vector<uint8_t> vec) {
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

template <typename T>
static bool getConcreteValue(S2EExecutionState* state, ref<Expr> expr,
                             T* value) {
  // auto size = sizeof(T);
  if (isa<ConstantExpr>(expr)) {
    ref<ConstantExpr> ce = dyn_cast<ConstantExpr>(expr);
    *value = ce->getZExtValue();
    return true;
  }
  // evaluate symobolic regs
  ref<ConstantExpr> ce;
  ce = dyn_cast<ConstantExpr>(state->concolics->evaluate(expr));
  *value = ce->getZExtValue();
  return false;
}

uint32_t ExprDepth(klee::ref<klee::Expr> expr);

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

// bool isReadExprAtOffset(ref<Expr> e, const ReadExpr* base, ref<Expr>
// offset)
// {
//   const ReadExpr* re = dyn_cast<ReadExpr>(e.get());

//   // right now, all Reads are byte reads but some
//   // transformations might change this
//   if (!re || (re->getWidth() != Expr::Int8)) return false;

//   // Check if the index follows the stride.
//   // FIXME: How aggressive should this be simplified. The
//   // canonicalizing builder is probably the right choice, but this
//   // is yet another area where we would really prefer it to be
//   // global or else use static methods.
//   return SubExpr::create(re->getIndex(), base->getIndex()) == offset;
// }
// /// hasOrderedReads: \arg e must be a ConcatExpr, \arg stride must
// /// be 1 or -1.
// ///
// /// If all children of this Concat are reads or concats of reads
// /// with consecutive offsets according to the given \arg stride, it
// /// returns the base ReadExpr according to \arg stride: first Read
// /// for 1 (MSB), last Read for -1 (LSB).  Otherwise, it returns
// /// null.
// const ReadExpr* hasOrderedReads(ref<Expr> e, int stride) {
//   assert(e->getKind() == Expr::Concat);
//   assert(stride == 1 || stride == -1);

//   const ReadExpr* base = dyn_cast<ReadExpr>(e->getKid(0));

//   // right now, all Reads are byte reads but some
//   // transformations might change this
//   if (!base || base->getWidth() != Expr::Int8) return NULL;

//   // Get stride expr in proper index width.
//   Expr::Width idxWidth = base->getIndex()->getWidth();
//   ref<Expr> strideExpr = ConstantExpr::alloc(stride, idxWidth);
//   ref<Expr> offset = ConstantExpr::create(0, idxWidth);

//   e = e->getKid(1);

//   // concat chains are unbalanced to the right
//   while (e->getKind() == Expr::Concat) {
//     offset = AddExpr::create(offset, strideExpr);
//     if (!isReadExprAtOffset(e->getKid(0), base, offset)) return NULL;
//     e = e->getKid(1);
//   }

//   offset = AddExpr::create(offset, strideExpr);
//   if (!isReadExprAtOffset(e, base, offset)) return NULL;

//   if (stride == -1)
//     return cast<ReadExpr>(e.get());
//   else
//     return base;
// }

// ref<Expr> getReads(ref<Expr> e, ref<ReadExpr>& read) {
//   // Invariant: \forall_{i \in stack} !i.isConstant() && i \in visited
//   std::vector<ref<Expr>> stack;
//   ExprHashSet visited;
//   std::set<const UpdateNode*> updates;
//   ExprList ret;

//   if (!isa<ConstantExpr>(e)) {
//     visited.insert(e);
//     stack.push_back(e);
//   }

//   while (!stack.empty()) {
//     ref<Expr> top = stack.back();
//     stack.pop_back();

//     if (ConcatExpr* ce = dyn_cast<ConcatExpr>(top)) {
//       const ReadExpr* re = hasOrderedReads(top, -1);
//       read = dyn_cast<ReadExpr>(top->getKid(0));
//       if (re != NULL) {
//         return top;
//       }
//     } else if (!isa<ConstantExpr>(top)) {
//       Expr* e = top.get();
//       for (unsigned i = 0; i < e->getNumKids(); i++) {
//         ref<Expr> k = e->getKid(i);
//         if (!isa<ConstantExpr>(k) && visited.insert(k).second)
//           stack.push_back(k);
//       }
//     }
//   }
//   return nullptr;
// }

}  // namespace plugins
}  // namespace s2e

#endif  // S2E_PLUGINS_uRnR_HELPER_H