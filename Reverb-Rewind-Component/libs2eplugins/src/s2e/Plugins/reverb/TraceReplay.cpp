///
/// Copyright (C) 2010-2015, Dependable Systems Laboratory, EPFL
///
/// Permission is hereby granted, free of charge, to any person obtaining a copy
/// of this software and associated documentation files (the "Software"), to
/// deal in the Software without restriction, including without limitation the
/// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
/// sell copies of the Software, and to permit persons to whom the Software is
/// furnished to do so, subject to the following conditions:
///
/// The above copyright notice and this permission notice shall be included in
/// all copies or substantial portions of the Software.
///
/// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
/// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
/// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
/// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
/// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
/// IN THE SOFTWARE.
///

#include "TraceReplay.h"

#include <klee/util/ExprTemplates.h>
#include <klee/util/ExprUtil.h>
#include <s2e/ConfigFile.h>
#include <s2e/S2E.h>
#include <s2e/SymbolicHardwareHook.h>
#include <s2e/Utils.h>
#include <s2e/cpu.h>

#include <filesystem>

#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprSMTLIBPrinter.h"
using namespace klee;

namespace s2e {
namespace plugins {

namespace {
class TraceReplayState : public PluginState {
 private:
  // current packet in two-dimensions array
  uint64_t packet_ptr = 0;
  // input_pointer for input in bytes;
  uint64_t in_ptr = 0;
  // point of isr_input in bytes
  uint64_t isr_ptr = 0;
  // previous fork pc
  uint64_t prev_fork_pc = 0;
  // previous fork times
  uint64_t prev_fork_time = 0;
  // previout state
  std::vector<S2EExecutionState*> prev_state;
  // user_input times
  uint32_t user_in_num = false;

  // interrupt conditions
  std::vector<ref<Expr>> conditions;

  // input symbolics
  std::vector<ref<Expr>> inputs;

  std::vector<ref<Expr>> constraints;

  // inputs
  std::vector<ref<Expr>> hardware_inputs;
  std::vector<uint32_t> isr_inputs;

  /////////////////////
  // Stuck ISR trigger

  // the basic blocks been executed
  std::deque<uint64_t> executed_blocks;

  // how many blocks new accessed
  uint64_t same_block_times = 0;

 public:
  /////////////////////
  // constant loop
  int out_pc = 0;
  ref<Expr> current_cond;
  ref<Expr> other_cond;
  S2EExecutionState* other_state;

  /////////////////////
  // Analysis

  // condition when this state first been forked
  ref<Expr> fork_cond;

  // pc when this state first been forked
  uint64_t fork_pc;

  /////////////////////
  // Functions

  virtual TraceReplayState* clone() const {
    TraceReplayState* new_state_ = new TraceReplayState(*this);
    new_state_->inputs.clear();
    for (auto p : this->inputs) new_state_->inputs.push_back(p);
    return new_state_;
  }

  TraceReplayState() {
    in_ptr = 0;
    isr_ptr = 0;
    prev_fork_pc = 0;
    prev_fork_time = 0;
    out_pc = 0;
    packet_ptr = 0;
  }

  static PluginState* factory(Plugin* p, S2EExecutionState* s) {
    return new TraceReplayState();
  }

  virtual ~TraceReplayState() {}

  inline uint64_t input_inc() { return in_ptr++; }

  inline void clr_in_ptr() { in_ptr = 0; }

  inline uint64_t get_in_ptr() { return in_ptr; }

  inline uint64_t packet_inc() { return packet_ptr++; }

  inline void clr_packet_ptr() { packet_ptr = 0; }

  inline uint64_t get_packet_ptr() { return packet_ptr; }

  inline uint64_t isr_inc() { return isr_ptr++; }

  inline void isr_dec() { isr_ptr--; }

  inline uint64_t get_isr_ptr() { return isr_ptr; }

  inline void clr_isr_ptr() { isr_ptr = 0; }

  // set previous fork pc
  inline void set_fork(uint64_t pc) { prev_fork_pc = pc; }

  // get previous fork pc
  inline uint64_t get_fork() { return prev_fork_pc; }

  inline void inc_fork() { prev_fork_time++; }

  inline uint64_t get_fork_time() { return prev_fork_time; }

  // kill current fork status
  inline void clear_fork() {
    prev_fork_time = 0;
    prev_state.clear();
  }

  inline void add_prev_state(S2EExecutionState* state) {
    prev_state.push_back(state);
  }

  inline std::vector<S2EExecutionState*> get_prev_states() {
    return prev_state;
  }

  inline void user_input() { user_in_num++; }

  inline uint32_t get_user_in() { return user_in_num; }

  inline void insert_cond(ref<Expr> cond) { conditions.push_back(cond); }

  inline void insert_input(ref<Expr> symb) { inputs.push_back(symb); }

  inline ExprList get_inputs() { return inputs; }

  inline std::vector<ref<Expr>> get_conds() { return conditions; }

  void AddConstraint(ref<Expr> constraint) {
    constraints.push_back(constraint);
  }

  void clearConstraints() { constraints.clear(); }

  std::vector<ref<Expr>> getConstraints() { return constraints; }

  // find executed block
  inline bool find_block(uint64_t pc) {
    for (auto i : executed_blocks) {
      if (i == pc) {
        return true;
      }
    }
    return false;
  }

  inline void insert_block(uint64_t pc) {
    executed_blocks.push_back(pc);
    // 8 is for loop length
    if (executed_blocks.size() > 8) {
      executed_blocks.pop_front();
    }
  }

  inline bool block_stuck() { return same_block_times == 1000; }

  inline void clear_block() { same_block_times = 0; }

  inline void inc_block() { same_block_times++; }

  inline void push_hardware(ref<Expr> in) { hardware_inputs.push_back(in); }

  inline void reset_hardware() { hardware_inputs.clear(); }

  inline std::vector<ref<Expr>> get_hardware() { return hardware_inputs; }

  inline std::vector<uint32_t> get_isr() { return isr_inputs; }

  inline void push_isr(uint32_t isr) { isr_inputs.push_back(isr); }

  inline void reset_isr() { isr_inputs.clear(); }

  // inline void update_last_hardware(ref<Expr> in) {
  //   hardware_inputs.pop_back();
  //   hardware_inputs.push_back(in);
  // }
};
}  // namespace

extern "C" {
static bool symbhw_is_care(struct MemoryDesc* mr, uint64_t physaddr,
                           uint64_t size, void* opaque);
}

static ref<Expr> symbhw_symbread(struct MemoryDesc* mr, uint64_t physaddress,
                                 const ref<Expr>& value,
                                 SymbolicHardwareAccessType type, void* opaque);

static void symbhw_symbwrite(struct MemoryDesc* mr, uint64_t physaddress,
                             const ref<Expr>& value,
                             SymbolicHardwareAccessType type, void* opaque);

S2E_DEFINE_PLUGIN(TraceReplay, "TraceReplay S2E plugin", "TraceReplay");

void TraceReplay::initialize() {
  /////////////////////////
  // Configs

  // global config
  analysis = g_s2e->getConfig()->getBool("analysis");
  // exit when analysis
  if (analysis) {
    return;
  }

  bool ok = false;
  snapshot_mode = true;
  std::string file_dir =
      std::filesystem::path(s2e()->getConfigFilePath()).parent_path().string();
  ConfigFile* cfg = s2e()->getConfig();

  // file which contains the input
  std::string symbol_file;
  std::string snapshot_file;
  std::vector<uint64_t> from_pc;
  std::vector<uint64_t> to_pc;
  std::vector<uint64_t> skip_read_pc;
  std::vector<uint64_t> skip_read_addr;
  bool debug;

  // switch
  config_init(debug, debug, Bool)
      config_init(user_only_mode, user_only_mode, Bool)
          config_init(fidelity, fidelity, Bool)
              config_init(random, random, Bool)
                  config_init(packet_mode, packet_mode, Bool)
      // snapshot
      config_init(snapshot_file, snapshotfile, String)
          config_init(symbol_file, symbfile, String)
      // input
      config_init(input_file, inputfile, String)
      // jump
      config_init(from_pc, jump, IntegerList)
          config_init(to_pc, to, IntegerList)
      // exit
      config_init(exit_pcs, exit_pcs, IntegerList)
      // skip
      config_init(skip_points, skip, IntegerList)
      // skip read
      config_init(skip_read_pc, skip_read_pc, IntegerList)
          config_init(skip_read_addr, skip_read_addr, IntegerList)
      // idle point
      config_init(idle, idle, Int)
      // user input
      config_init(user_addrs, user_addrs, IntegerList)
      // ghidra port
      config_init(ghidra_port, ghidra_port, Int)
      // snapshot ram
      config_init(snapshot_rams, ram, IntegerList)
      // hook reads
      config_init(hook_read, hook_read, IntegerList)
      // infer mode start pc
      config_init(infer_start, infer_start, Int)
      // DMA register
      config_init(dma_reg, dma_reg, Int) config_init(dma_len, dma_len, Int)
      // debug is using to switch other options
      if (debug) {
    trace_block = true;
  }

  // random select the symbolic state
  // comment it to get same result every time
  // if (random) {
  //   srand(time(NULL));
  // }

  bool global_analysis = g_s2e->getConfig()->getBool("analysis");

  getWarningsStream() << "global analysis " << global_analysis << "\n";

  // firmware ram configuration
  int ram_num = g_s2e->getConfig()->getListSize("mem.ram");
  if (ram_num != 1) {
    getWarningsStream() << "multip ram " << ram_num << "\n";
    return;
  }
  for (int i = 0; i < ram_num; ++i) {
    std::stringstream ssram;
    ssram << "mem.ram"
          << "[" << (i + 1) << "]";
    ram_start = cfg->getInt(ssram.str() + "[1]", 0, &ok);
    if (!ok) {
      getWarningsStream() << "Could not parse " << ssram.str() + "baseaddr"
                          << "\n";
      exit(-1);
    }
    ram_end = cfg->getInt(ssram.str() + "[2]", 0, &ok) + ram_start;
    if (!ok) {
      getWarningsStream() << "Could not parse " << ssram.str() + "size" << "\n";
      exit(-1);
    }
    getWarningsStream() << "valid ram " << i + 1
                        << " baseaddr:" << hexval(ram_start)
                        << " size:" << hexval(ram_end - ram_start) << "\n";
  }

  // rom configuration
  int rom_num = g_s2e->getConfig()->getListSize("mem.rom");
  if (rom_num != 1) {
    getWarningsStream() << "multi rom " << rom_num << "\n";
    return;
  }
  for (int i = 0; i < rom_num; ++i) {
    std::stringstream ssram;
    ssram << "mem.rom"
          << "[" << (i + 1) << "]";
    rom_start = cfg->getInt(ssram.str() + "[1]", 0, &ok);
    if (!ok) {
      getWarningsStream() << "Could not parse rom " << ssram.str() + "baseaddr"
                          << "\n";
      exit(-1);
    }
    rom_end = cfg->getInt(ssram.str() + "[2]", 0, &ok) + rom_start;
    if (!ok) {
      getWarningsStream() << "Could not parse rom " << ssram.str() + "size"
                          << "\n";
      exit(-1);
    }
    getWarningsStream() << "valid rom " << i + 1
                        << " baseaddr:" << hexval(rom_start)
                        << " size:" << hexval(rom_end - rom_start) << "\n";
  }

  // if ram_start is 0x0, for example nrf52840,
  // make the null_region strict to interrupt vector table
  if (ram_start < null_region) {
    null_region = 0x40;
  }

  // sort for binary search
  std::sort(skip_points.begin(), skip_points.end());
  std::sort(user_addrs.begin(), user_addrs.end());
  std::sort(hook_read.begin(), hook_read.end());
  std::sort(exit_pcs.begin(), exit_pcs.end());

  // check and insert force jump pc
  if (from_pc.size() != to_pc.size()) {
    getWarningsStream() << " force jump size does not match\n";
    return;
  }

  for (size_t i = 0; i < from_pc.size(); i++) {
    forcejump_map.insert(std::make_pair(from_pc[i], to_pc[i]));
  }

  // check and insert the skipped read
  if (skip_read_pc.size() != skip_read_addr.size()) {
    getWarningsStream() << " skip read size does not match\n";
    return;
  }

  for (size_t i = 0; i < skip_read_pc.size(); i++) {
    skip_read.insert(skip_read_pc[i] << XLEN | skip_read_addr[i]);
  }

  // load from file
  if (packet_mode) {
    load_packets(file_dir + "/" + input_file, packets);
  } else {
    load_input(file_dir + "/" + input_file, input);
  }
  // load snapshot
  load_input(file_dir + "/" + snapshot_file, snapshot);
  //  load_model_from_yml(model_file);
  load_symbols(file_dir + "/" + symbol_file);

  // set loop time to input's size
  loop_time = input.size();

  // memory hook
  g_symbolicMemoryHook = SymbolicMemoryHook(symbhw_is_care, symbhw_symbread,
                                            symbhw_symbwrite, this);

  /////////////////////////
  // Connections

  // when instructions is executed
  // s2e()->getCorePlugin()->onInstructionExecuted.connect(sigc::mem_fun(*this,
  // &TraceReplay::onInstExecuted));

  // when pc or ld/str address is symbolic
  s2e()->getCorePlugin()->onSymbolicAddress.connect(
      sigc::mem_fun(*this, &TraceReplay::onSymbolicAddress));

  // tanslate instructions hook
  s2e()->getCorePlugin()->onTranslateInstructionStart.connect(
      sigc::mem_fun(*this, &TraceReplay::onTranslateInstruction));

  // register access hook
  // s2e()->getCorePlugin()->onTranslateRegisterAccessEnd.connect(sigc::mem_fun(*this,
  // &TraceReplay::onReg));

  // block log for debug
  s2e()->getCorePlugin()->onTranslateBlockStart.connect(
      sigc::mem_fun(*this, &TraceReplay::onBlock));

  // block end for taret
  s2e()->getCorePlugin()->onTranslateBlockEnd.connect(
      sigc::mem_fun(*this, &TraceReplay::onBlockEnd));

#if defined(TARGET_I386) || defined(TARGET_X86_64)
  // Used to delete the error in editor
#elif defined(TARGET_ARM)
  // kill unused edge
  s2e()->getCorePlugin()->onStateForkDecide.connect(
      sigc::mem_fun(*this, &TraceReplay::onStateForkDecide));
#else
#error Unsupported target architecture
#endif

  // kill when state fork
  s2e()->getCorePlugin()->onStateFork.connect(
      sigc::mem_fun(*this, &TraceReplay::onStateFork));

  // hook symbolic address in memory access
  s2e()->getCorePlugin()->onBeforeSymbolicDataMemoryAccess.connect(
      sigc::mem_fun(*this, &TraceReplay::onSymbAddrMemAccess));

  s2e()->getCorePlugin()->onInvalidPCAccess.connect(
      sigc::mem_fun(*this, &TraceReplay::onInvalidPCAccess));

  s2e()->getCorePlugin()->onException.connect(
      sigc::mem_fun(*this, &TraceReplay::onException));

  s2e()->getCorePlugin()->onExceptionExit.connect(
      sigc::mem_fun(*this, &TraceReplay::onExceptionExit));

  /////////////////////////
  // Inner State Initialize

  // analysis report init
  report << "uARCUS Analysis Report\n";

  /// SOCKET
  // create the socket
  client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (client_socket == -1) {
    std::cerr << "Failed to create socket\n";
    exit(1);
  }
  // set the ip and port
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(ghidra_port);
  if (inet_pton(AF_INET, "127.0.0.1", &(server_addr.sin_addr)) <= 0) {
    std::cerr << "Invalide address or address not supported\n";
    exit(1);
  }
  // connect
  if (connect(client_socket, (struct sockaddr*)&server_addr,
              sizeof(server_addr)) < 0) {
    std::cerr << "Connection failed\n";
    exit(1);
  } else {
    s2e()->getDebugStream() << "ghidra server connected\n";
  }

  /////////////////////////
  // Others

  /// time start
  time_start = std::chrono::high_resolution_clock::now();

  /// searcher
  s2e()->getExecutor()->setSearcher(this);
}

inline bool TraceReplay::is_inputs_end(S2EExecutionState* state) {
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  if (packet_mode) {
    return plgState->get_packet_ptr() >= packets.size();
  } else {
    return plgState->get_in_ptr() >= input.size();
  }
}

inline bool TraceReplay::is_packet_end(S2EExecutionState* state) {
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  if (packet_mode) {
    return plgState->get_in_ptr() >= packets[plgState->get_packet_ptr()].size();
  } else {
    return is_inputs_end(state);
  }
}

inline uint64_t TraceReplay::getReadKey(S2EExecutionState* state,
                                     const ref<ReadExpr> read) {
  auto seq = hardware_symb_seq.find(read);
  if (seq != hardware_symb_seq.end())
    return seq->second[1] << XLEN | seq->second[2];
  getWarningsStream(state) << "Unrecognized hardware symbols: " << read << "\n";
  return 0;
}

// Determines the extent of user input present in the given expression.
ExprUser TraceReplay::CheckExprUser(S2EExecutionState* state, ref<Expr> expr) {
  // get the ReadExpr from condition;
  bool all_user_in = false;
  bool has_user_in = false;
  std::vector<ref<ReadExpr>> reads;
  findReads(expr, false, reads);
  for (auto read : reads) {
    InMode mode = InMode::HARDWARE;
    getSeq(read, &mode);
    if (mode == InMode::USER) {
      all_user_in = true;
      has_user_in = true;
    } else {
      all_user_in = false;
    }
  }
  if (all_user_in) {
    return ExprUser::ALL;
  } else if (has_user_in) {
    return ExprUser::PART;
  } else {
    return ExprUser::NO;
  }
}

inline ref<Expr> TraceReplay::CreateConcatEqRead(S2EExecutionState* state,
                                              uint64_t key, ref<Expr> read) {
  ref<Expr> ret;
  auto it = concat_reads.find(key);
  if (it == concat_reads.end()) {
    std::stringstream ss;
    ss << std::hex << key;
    ref<Expr> cread_ = state->createSymbolicValue(ss.str());
    concat_reads.insert({key, cread_});
    return EqExpr::create(cread_, read);
  } else {
    return EqExpr::create(it->second, read);
  }
}

inline ref<Expr> TraceReplay::CreateEqRead(S2EExecutionState* state, uint64_t key,
                                        ref<Expr> read) {
  uint64_t idx = getReadIndex(read);
  ref<Expr> uni_read;
  ref<Expr> ret;
  auto it = unified_reads.find(key);
  if (it == unified_reads.end()) {
    std::stringstream ss;
    ss << std::hex << key;
    std::vector<ref<Expr>> unified_reads_ =
        state->createSymbolicArray(ss.str());
    uni_read = unified_reads_[idx];
    unified_reads.insert({key, unified_reads_});
  } else {
    uni_read = it->second[idx];
  }
  // add not equal
  auto seq = hardware_symb_seq.find(read);
  if (!seq->second[3]) {
    ret = EqExpr::create(uni_read, read);
    seq->second[3] = 1;
  }
  return ret;
}

// test
bool isReadExprAtOffset(ref<Expr> e, const ReadExpr* base, ref<Expr> offset) {
  const ReadExpr* re = dyn_cast<ReadExpr>(e.get());

  // right now, all Reads are byte reads but some
  // transformations might change this
  if (!re || (re->getWidth() != Expr::Int8)) return false;

  // Check if the index follows the stride.
  // FIXME: How aggressive should this be simplified. The
  // canonicalizing builder is probably the right choice, but this
  // is yet another area where we would really prefer it to be
  // global or else use static methods.
  return SubExpr::create(re->getIndex(), base->getIndex()) == offset;
}
/// hasOrderedReads: \arg e must be a ConcatExpr, \arg stride must
/// be 1 or -1.
///
/// If all children of this Concat are reads or concats of reads
/// with consecutive offsets according to the given \arg stride, it
/// returns the base ReadExpr according to \arg stride: first Read
/// for 1 (MSB), last Read for -1 (LSB).  Otherwise, it returns
/// null.
const ReadExpr* hasOrderedReads(ref<Expr> e, int stride) {
  assert(e->getKind() == Expr::Concat);
  assert(stride == 1 || stride == -1);

  const ReadExpr* base = dyn_cast<ReadExpr>(e->getKid(0));

  // right now, all Reads are byte reads but some
  // transformations might change this
  if (!base || base->getWidth() != Expr::Int8) return NULL;

  // Get stride expr in proper index width.
  Expr::Width idxWidth = base->getIndex()->getWidth();
  ref<Expr> strideExpr = ConstantExpr::alloc(stride, idxWidth);
  ref<Expr> offset = ConstantExpr::create(0, idxWidth);

  e = e->getKid(1);

  // concat chains are unbalanced to the right
  while (e->getKind() == Expr::Concat) {
    offset = AddExpr::create(offset, strideExpr);
    if (!isReadExprAtOffset(e->getKid(0), base, offset)) return NULL;
    e = e->getKid(1);
  }

  offset = AddExpr::create(offset, strideExpr);
  if (!isReadExprAtOffset(e, base, offset)) return NULL;

  if (stride == -1)
    return cast<ReadExpr>(e.get());
  else
    return base;
}

ref<Expr> getReads(ref<Expr> e, ref<ReadExpr>& read) {
  // Invariant: \forall_{i \in stack} !i.isConstant() && i \in visited
  std::vector<ref<Expr>> stack;
  ExprHashSet visited;
  std::set<const UpdateNode*> updates;
  ExprList ret;

  if (!isa<ConstantExpr>(e)) {
    visited.insert(e);
    stack.push_back(e);
  }

  while (!stack.empty()) {
    ref<Expr> top = stack.back();
    stack.pop_back();

    if (ConcatExpr* ce = dyn_cast<ConcatExpr>(top)) {
      const ReadExpr* re = hasOrderedReads(top, -1);
      read = dyn_cast<ReadExpr>(top->getKid(0));
      if (re != NULL) {
        return top;
      }
    } else if (!isa<ConstantExpr>(top)) {
      Expr* e = top.get();
      for (unsigned i = 0; i < e->getNumKids(); i++) {
        ref<Expr> k = e->getKid(i);
        if (!isa<ConstantExpr>(k) && visited.insert(k).second)
          stack.push_back(k);
      }
    }
  }
  return nullptr;
}
// test end

inline void TraceReplay::AddConstraint(S2EExecutionState* state,
                                    ref<Expr> constraint, InferType type) {
  auto pconstraints = &infer_constraints;
  auto presults = &infer_results;
  auto pstr = "read";
  switch (type) {
    case InferType::READ: {
      pconstraints = &infer_constraints;
      presults = &infer_results;
      pstr = "read";
      break;
    }
    case InferType::WRITE: {
      pconstraints = &infer_write_constraints;
      presults = &infer_write_results;
      pstr = "write";
      break;
    }
    case InferType::EXIT: {
      pconstraints = &infer_exit_constraints;
      presults = &infer_exit_results;
      pstr = "exit";
      break;
    }
  }

  if (added_constraints.find(constraint) != added_constraints.end()) return;
  uint64_t pc = state->regs()->getPc();
  debug_stream(state) << "AddConstraint " << pstr << " pc:" << hexval(pc) << " "
                      << constraint << "\n";
  // concat solve
  ref<ReadExpr> read_;
  ref<Expr> reads_ = getReads(constraint, read_);
  if (!reads_.isNull()) {
    debug_stream(state) << "Get concat read: " << reads_ << "\n";
    uint64_t key = getReadKey(state, read_);
    auto it = pconstraints->find(key);
    if (it != pconstraints->end()) {
      ref<Expr> eq = CreateConcatEqRead(state, key, reads_);
      it->second.push_back(eq);
      it->second.push_back(constraint);
    } else {
      ExprList constraints = {constraint};
      ref<Expr> eq = CreateConcatEqRead(state, key, reads_);
      constraints.push_back(eq);
      pconstraints->insert({key, constraints});
    }
    ExprList cs_ = pconstraints->find(key)->second;
    if (!EvaluateConcatRead(state, cs_, reads_, key)) {
      // TODO: cannot solve, restore or no result
      if (type == InferType::WRITE) {
        getWarningsStream(state) << "not update write when conflict\n";
        return;
      }
      uint64_t res_ = SolveConstraints(state, cs_, reads_);
      added_constraints.insert(constraint);
      debug_stream(state) << "Solve result: " << hexval(res_) << "\n";
      auto it = presults->find(key);
      if (it == presults->end()) {
        presults->insert({key, res_});
      } else {
        it->second = res_;
      }
      getWarningsStream(state)
          << "Update result pc:" << hexval(key >> 32)
          << " addr:" << hexval(key << 32 >> 32) << " val:" << hexval(res_)
          << " to " << type << "\n";
    }
    return;
  }
  // split read in bytes may have bug when solve.
  std::vector<ref<ReadExpr>> reads;
  findReads(constraint, false, reads);
  uint64_t key = 0;
  for (auto read : reads) {
    key = getReadKey(state, read);
    auto it = pconstraints->find(key);
    if (it != pconstraints->end()) {
      ref<Expr> eq = CreateEqRead(state, key, read);
      if (!eq.isNull()) it->second.push_back(eq);
      it->second.push_back(constraint);
    } else {
      std::vector<ref<Expr>> constraints = {constraint};
      ref<Expr> eq = CreateEqRead(state, key, read);
      if (!eq.isNull()) constraints.push_back(eq);
      pconstraints->insert({key, constraints});
    }
  }
  added_constraints.insert(constraint);
  updateInferResult(state, reads, type);
}  // namespace plugins

// different from AddConstraint: constant loop no cross states, just add once is
// ok
inline uint64_t TraceReplay::AddConstConstraint(S2EExecutionState* state,
                                             uint64_t pc,
                                             ref<Expr> constraint) {
  debug_stream(state) << "AddConstConstraint: " << constraint
                      << " pc:" << hexval(pc) << "\n";
  ref<ReadExpr> read_;
  ref<Expr> reads = getReads(constraint, read_);
  if (reads.isNull()) {
    getWarningsStream(state) << "Cannot get concat read at pc:" << hexval(pc)
                             << " from " << constraint << "\n";
    return 0xffffffff;
  } else {
    uint64_t key = getReadKey(state, read_);
    auto it = infer_results.find(key);
    if (it != infer_results.end()) {  //  already solved
      return it->second;
    }
    debug_stream(state) << "Get concat read: " << reads << "\n";
    ExprList cs_;
    cs_.push_back(constraint);
    uint64_t res_ = SolveConstraints(state, cs_, reads);
    debug_stream(state) << "Solve constant loop result: " << hexval(res_)
                        << "\n";
    infer_results.insert({key, res_});
    getWarningsStream(state)
        << "Store constant loop result pc:" << hexval(key >> 32)
        << " addr:" << hexval(key << 32 >> 32) << " val:" << hexval(res_)
        << "\n";
    return res_;
  }
}

inline bool TraceReplay::EvaluateConcatRead(S2EExecutionState* state,
                                         std::vector<ref<Expr>> constraints,
                                         ref<Expr> read, uint64_t key) {
  debug_stream(state) << "Evalute ConcatRead for " << read << "\n";
  auto it = infer_results.find(key);
  if (it == infer_results.end()) {
    return false;
  }
  uint32_t val = it->second;
  ref<Expr> eq_ = EqExpr::create(ConstantExpr::create(val, 32), read);
  Validity result;
  ConstraintManager eval_manager(constraints);
  Query query(eval_manager, eq_);
  state->solver()->evaluate(query, result);
  if (result == Validity::False) {
    getInfoStream(state) << "evaluate false on key:" << key << " read:" << read
                         << "\n";
    return false;
  }
  return true;
}

inline bool TraceReplay::EvaluateConstraint(S2EExecutionState* state,
                                         std::vector<ref<Expr>> constraints,
                                         ref<Expr> expr, uint64_t key,
                                         size_t index) {
  debug_stream(state) << "Evalute Constraints for " << expr << "\n";
  uint64_t shift = index * 8;
  auto it = infer_results.find(key);
  if (it == infer_results.end()) {
    return false;
  }
  uint8_t val = (it->second >> shift) & 0xff;
  ref<Expr> eq_ = EqExpr::create(ConstantExpr::create(val, 8), expr);
  Validity result;
  ConstraintManager eval_manager(constraints);
  Query query(eval_manager, eq_);
  state->solver()->evaluate(query, result);
  if (result == Validity::False) {
    getInfoStream(state) << "evaluate false on key:" << key
                         << " index:" << index << " expr:" << expr << "\n";
    return false;
  }
  return true;
}

inline uint64_t TraceReplay::SolveConstraints(S2EExecutionState* state,
                                           std::vector<ref<Expr>> constraints,
                                           ref<Expr> expr) {
  // Constraints debug output
  std::stringstream ss;
  ss << "Expr: " << expr << "\n";
  ss << "Constraints: \n";
  for (size_t i = 0; i < constraints.size(); i++) {
    ss << "  " << constraints[i] << "\n";
  }
  debug_stream(state) << ss.str();
  // end
  ref<ConstantExpr> result;
  ConstraintManager constraint_manager(constraints);
  Query query(constraint_manager, expr);
  uint64_t res;
  state->solver()->getValue(query, result);
  res = result->getZExtValue();
  debug_stream(state) << "Solve Expr: " << expr << " Value: " << hexval(res)
                      << "\n";
  return res;
}

void TraceReplay::updateInferResult(S2EExecutionState* state,
                                 std::vector<ref<ReadExpr>> reads,
                                 InferType type) {
  std::set<uint64_t> keys;
  for (auto read : reads) keys.insert(getReadKey(state, read));
  for (auto key : keys) {
    auto uni_reads_pair = unified_reads.find(key);
    auto infer_constraints_pair = infer_constraints.find(key);
    if (uni_reads_pair == unified_reads.end()) {
      getWarningsStream(state)
          << "Not find unified read of " << hexval(key) << "\n";
      continue;
    }
    if (infer_constraints_pair == infer_constraints.end()) {
      getWarningsStream(state)
          << "Not find infer constraint of " << hexval(key) << "\n";
      continue;
    }
    ExprList constraints = infer_constraints_pair->second;
    // solve for every byte of unified read;
    for (size_t i = 0; i < uni_reads_pair->second.size(); i++) {
      ref<Expr> expr = uni_reads_pair->second[i];
      /* TODO: fix the edge case, for example uint32_t a != 0, KLEE will seem it
      as (a[0],a[1],a[2],a[3]) != 0. When using evaluate, every single byte
      imagine that the other can be not zero, so the result fails; fix here */
      if (!EvaluateConstraint(state, constraints, expr, key, i)) {
        uint64_t result = SolveConstraints(state, constraints, expr);
        StoreInferResult(state, result, expr, key, i, type);
      } else {
        StoreInferResult(state, 1, expr, key, i, type);
      }
    }

    getInfoStream(state) << "update infer result for pc: " << hexval(key >> 32)
                         << " addr:" << hexval(key & UINT32_MAX) << "\n";
  }
}

inline void TraceReplay::StoreInferResult(S2EExecutionState* state,
                                       uint64_t result, ref<Expr> read,
                                       uint64_t key, size_t index,
                                       InferType type) {
  if (type != InferType::READ) {
    getInfoStream(state) << "not store for " << type << " read " << read
                         << " result " << hexval(result) << "\n";
    return;
  }
  // Store results
  uint64_t shift = index * 8;
  auto it = infer_results.find(key);
  if (it != infer_results.end()) {
    it->second &= ~(0xff << shift);
    it->second |= (result << shift);
  } else {
    infer_results.insert({key, result << shift});
  }
  getWarningsStream(state) << "Set to pc:" << hexval(key >> 32)
                           << " addr:" << hexval(key << 32 >> 32)
                           << " idx:" << index << " val:" << result << "\n";
}

inline uint64_t TraceReplay::getReadIndex(ref<Expr> read) {
  if (ReadExpr* read_expr = dyn_cast<ReadExpr>(read)) {
    if (ConstantExpr* idx = dyn_cast<ConstantExpr>(read_expr->getIndex())) {
      return idx->getZExtValue();
    } else {
      getWarningsStream(g_s2e_state) << "Unrecognized read: " << read << "\n";
      return UINT64_MAX;
    }
  } else {
    getWarningsStream(g_s2e_state) << "Not a read expr: " << read << "\n";
    return UINT64_MAX;
  }
}

void TraceReplay::create_seq(klee::ref<Expr> expr, std::vector<uint64_t>& seqs,
                          InMode mode) {
  std::map<klee::ref<Expr>, std::vector<uint64_t>>* seq_map;

  if (mode == InMode::USER) {
    seq_map = &user_symb_seq;
  } else {
    seq_map = &hardware_symb_seq;
  }

  uint32_t bytes = expr->getWidth() / 8;

  if (bytes == 1 && isa<ReadExpr>(expr)) {
    seq_map->insert({expr, seqs});
  } else {
    klee::ref<Expr> expr_t = expr;

    for (size_t i = 0; i < bytes - 1; i++) {
      ref<Expr> k = expr_t->getKid(0);
      if (ReadExpr* re = dyn_cast<ReadExpr>(k)) {
        seqs[0] = getReadIndex(dyn_cast<ReadExpr>(k));
        // add the flag of whether it is equal with a unified ReadExpr
        seqs.push_back(0);
        seq_map->insert({k, seqs});
        expr_t = expr_t->getKid(1);
      } else {
        getWarningsStream() << "not a read expr: " << k << "\n";
      }
    }

    if (ReadExpr* re = dyn_cast<ReadExpr>(expr_t)) {
      seqs[0] = getReadIndex(dyn_cast<ReadExpr>(expr_t));
      seq_map->insert({expr_t, seqs});
    } else {
      getWarningsStream() << "not a read expr: " << expr_t << "\n";
    }
  }
}

GhidraResp TraceReplay::send_ghidra(S2EExecutionState* state, uint64_t pc,
                                 ghidra_msg_t type) {
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  if (type == CONSTANT_LOOP) {
    // search cache
    auto const_resp = const_loop_cache.find(pc);
    if (const_resp != const_loop_cache.end()) {
      uint64_t out_pc_ = const_resp->second;
      if (out_pc_ == 0) {
        return GhidraResp::NOT;
      } else {
        plgState->out_pc = out_pc_;
        return GhidraResp::CONST;
      }
    }
    std::string message = hexval(pc, 8).str() + "\n";

    if (send(client_socket, message.c_str(), message.size(), 0) < 0) {
      std::cerr << "Failed to send message\n";
      return GhidraResp::NOT;
    }

    char buffer[20] = {0};

    if (recv(client_socket, buffer, sizeof(buffer), 0) < 0) {
      std::cerr << "Failed to receive message\n";
      return GhidraResp::NOT;
    }

    if (buffer[0] == 'C') {  // Constant loop
      sscanf(buffer + 5, "%x", &plgState->out_pc);
      debug_stream(state) << "out pc:" << hexval(plgState->out_pc) << "\n";
      const_loop_cache.insert(std::make_pair(pc, plgState->out_pc));
      return GhidraResp::CONST;
    } else if (buffer[0] == 'J') {  // Jump
      return GhidraResp::JUMP;
    } else if (buffer[0] == 'M') {  // Maybe
      return GhidraResp::MAYBE;
    } else if (buffer[0] == 'N') {  // NOT
      const_loop_cache.insert(std::make_pair(pc, 0));
      return GhidraResp::NOT;
    } else {
      s2e()->getWarningsStream()
          << "not supported respond from ghidra server" << buffer << "\n";
      return GhidraResp::NOT;
    }
  } else if (type == FAKE_READ) {
    auto read_state = fake_read_cache.find(pc);
    if (read_state != fake_read_cache.end()) {
      return read_state->second;
    }
    std::string message = std::string("4") + hexval(pc, 8).str() + "\n";
    if (send(client_socket, message.c_str(), message.size(), 0) < 0) {
      std::cerr << "Failed to send message\n";
      return GhidraResp::NOT;
    }

    char buffer[20] = {0};

    if (recv(client_socket, buffer, sizeof(buffer), 0) < 0) {
      std::cerr << "Failed to receive message\n";
      return GhidraResp::NOT;
    }

    if (buffer[0] == 'F') {  // Constant loop
      fake_read_cache.insert(std::make_pair(pc, GhidraResp::FAKE));
      return GhidraResp::FAKE;
    } else if (buffer[0] == 'N') {  // NOT
      fake_read_cache.insert(std::make_pair(pc, GhidraResp::NOT));
      return GhidraResp::NOT;
    } else {
      s2e()->getWarningsStream()
          << "not supported respond from ghidra server" << buffer << "\n";
      return GhidraResp::NOT;
    }
  }
  return GhidraResp::NOT;
}

void TraceReplay::send_callind(uint64_t pc, uint64_t target) {
  debug_stream(g_s2e_state) << "Not Used\n";
  return;

  debug_stream(g_s2e_state) << "Send indirect call pc:" << hexval(pc)
                            << " target:" << hexval(target) << "\n";
  std::string message =
      std::string("1") + hexval(pc, 8).str() + hexval(target, 8).str() + "\n";
  if (send(client_socket, message.c_str(), message.size(), 0) < 0) {
    std::cerr << "Failed to send message\n";
  }
}

bool TraceReplay::send_user(S2EExecutionState* state, uint64_t pc) {
  debug_stream(g_s2e_state) << "Send User input pc:" << hexval(pc) << "\n";
  std::string message = std::string("1") + hexval(pc, 8).str() + "\n";
  if (send(client_socket, message.c_str(), message.size(), 0) < 0) {
    std::cerr << "Failed to send message\n";
  }
  if (send_ghidra(state, pc) == GhidraResp::CONST) {
    return true;
  }
  return false;
}

inline std::map<klee::ref<Expr>, std::vector<uint64_t>>::iterator
TraceReplay::getSeq(const klee::ref<Expr> expr, InMode* mode) {
  auto user_it = user_symb_seq.find(expr);
  auto hardware_it = hardware_symb_seq.find(expr);
  if (user_it != user_symb_seq.end()) {
    *mode = InMode::USER;
    return user_it;
  } else if (hardware_it != hardware_symb_seq.end()) {
    *mode = InMode::HARDWARE;
    return hardware_it;
  } else {
    getWarningsStream(g_s2e_state)
        << " not find in sequence map: " << expr << "\n";
    return hardware_symb_seq.end();
  }
}

inline std::string TraceReplay::getReadName(const ref<Expr>& expr) const {
  if (ReadExpr* re = dyn_cast<ReadExpr>(expr)) {
    return re->getUpdates()->getRoot()->getName();
  } else {
    std::string new_str = " ";
    getWarningsStream() << "not a read expr: " << expr << "\n";
    return new_str;
  }
}

void TraceReplay::findReadNames(const ref<Expr>& expr,
                             std::set<std::string>& read_names) {
  std::vector<ref<ReadExpr>> reads;
  findReads(expr, false, reads);
  for (auto i : reads) read_names.insert(getReadName(i));
}

void TraceReplay::onConcreteDataMemoryAccess(S2EExecutionState* state,
                                          uint64_t address, uint64_t value,
                                          uint8_t size, unsigned flags) {
  debug_stream(state) << " pc:" << hexval(state->regs()->getPc())
                      << " address:" << hexval(address)
                      << " val:" << hexval(value) << " size:" << hexval(size)
                      << " type:" << ((flags & 0x2) ? "write" : "read") << "\n";
  return;
}

void TraceReplay::onTranslateInstruction(ExecutionSignal* signal,
                                      S2EExecutionState* state,
                                      TranslationBlock* tb, uint64_t pc) {
  // fidelity test
  if (fidelity)
    signal->connect(sigc::mem_fun(*this, &TraceReplay::onInstExecution));
  // test end
  if (learn_mode && interrupt_end == pc) {
    signal->connect(sigc::mem_fun(*this, &TraceReplay::onIRQEndExec));
  }
  // skip
  if (std::binary_search(skip_points.begin(), skip_points.end(), pc)) {
    signal->connect(sigc::mem_fun(*this, &TraceReplay::onSkipExecution));
  } else if (pc == idle) {
    signal->connect(sigc::mem_fun(*this, &TraceReplay::onIsrExecution));
  } else {
    auto jump = forcejump_map.find(pc);
    if (jump != forcejump_map.end()) {
      // comment because of some fault in optimization
      // to_pc = jump->second;
      signal->connect(sigc::mem_fun(*this, &TraceReplay::onJumpExecution));
    }
  }
}

void TraceReplay::onInvalidPCAccess(S2EExecutionState* state, uint64_t pc) {
  std::string reason = "access invalid pc " + hexval(pc).str();
  crash_handler(state, pc, INVALID_PC, reason);
}

void TraceReplay::switch_mode(S2EExecutionState* state) {
  std::string message = std::string("2") + "\n";
  if (learn_mode) {
    getWarningsStream(state) << "LEARN mode finish \n";
    working_list.clear();
    // store remain constant constraints
    // AddConstConstraint(state, 0, nullptr);
    for (auto pair_ : infer_constraints) {
      uint64_t key = pair_.first;
      auto uni_reads_pair = unified_reads.find(key);
      if (uni_reads_pair == unified_reads.end()) {
        getWarningsStream(state)
            << "Not find unified read of " << hexval(key) << "\n";
        continue;
      }
      // solve for every byte of unified read;
      for (size_t i = 0; i < uni_reads_pair->second.size(); i++) {
        ref<Expr> expr = uni_reads_pair->second[i];
        if (!EvaluateConstraint(state, pair_.second, expr, key, i)) {
          uint64_t result = SolveConstraints(state, pair_.second, expr);
          StoreInferResult(state, result, expr, key, i, InferType::READ);
        }
      }
    }
    // TODO: add equal expr of every ReadExpr which have same pc address but
    // from different statess
    infer_constraints.clear();
    // solve end
    debug_stream(state) << "switch to infer mode \n";
    learn_mode = false;
    interrupt_end = 0x0;

    // clear all the callind
    for (auto callind_ : callind) {
      send_callind(callind_.first, callind_.second);
    }
    callind.clear();
    // end
    ResumeFromSnapshot(state, state->regs()->getPc());
  } else {
    message = std::string("3") + "\n";
    debug_stream(state) << "switch to learn mode \n";
    learn_mode = true;
  }

  if (send(client_socket, message.c_str(), message.size(), 0) < 0) {
    std::cerr << "Failed to send message\n";
  }
}

void TraceReplay::onIRQEndExec(S2EExecutionState* state, uint64_t pc) {}

void TraceReplay::onInstExecution(S2EExecutionState* state, uint64_t pc) {
  debug_stream(state) << "Inst Executing " << hexval(pc) << "\n";
}

// This function is used to simulate the handlers section in fuzzware
// config.yaml
void TraceReplay::onSkipExecution(S2EExecutionState* state, uint64_t pc) {
#if defined(TARGET_I386) || defined(TARGET_X86_64)
  uint64_t lr = state->regs()->getBp();
#elif defined(TARGET_ARM)
  uint64_t lr = state->regs()->getLr();
#else
#error Unsupported target architecture
#endif

  debug_stream(state) << "skip pc: " << hexval(pc) << " to " << hexval(lr - 1)
                      << "\n";
  state->regs()->setPc(lr - 1);
  throw CpuExitException();
}

ExecutionState* TraceReplay::forkPoint(S2EExecutionState* state) {
  state->jumpToSymbolicCpp();
  std::string name = "fork_point";
  klee::Executor::StatePair sp;

  // add a meaningless symbol for a cond to fork
  klee::ref<klee::Expr> var = state->createSymbolicValue<uint32_t>(name, 0);

  for (unsigned i = 1; i < 2; ++i) {
    klee::ref<klee::Expr> val = klee::ConstantExpr::create(i, var->getWidth());
    klee::ref<klee::Expr> cond = klee::NeExpr::create(var, val);

    sp = s2e()->getExecutor()->forkCondition(state, cond, true);
    assert(sp.first == state);
    assert(sp.second && sp.second != sp.first);
    if (sp.second) {
      // Re-execute the plugin invocation in the other state
      sp.second->pc = sp.second->prevPC;
    }
  }

  getWarningsStream(state)
      << "manual fork at pc: " << hexval(state->regs()->getPc()) << " state "
      << dynamic_cast<S2EExecutionState*>(sp.second)->getID() << "\n";
  return sp.second;
}

void TraceReplay::onIsrExecution(S2EExecutionState* state, uint64_t pc) {
  // if random not trigger interrupt
  if (random) return;
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  uint32_t isr_num = 0;
  uint64_t cur_isr_ptr = plgState->isr_inc();

  // DELETE IT, old input based Interrrupt Trigger
  // if (cur_isr_ptr >= isr_input.size()) {
  //   // std::string reason(hexval(pc));
  //   getWarningsStream(state) << "isr input drained \n";
  //   s2e()->getExecutor()->terminateState(*state, "isr input drained");
  // }
  // isr_num = isrs[isr_input[cur_isr_ptr] % isrs.size()];

  /////////////////////
  /// LEARN ISR

  if (learned_isr.empty()) {
    switch_mode(state);
  }

  if (learn_mode) {  // exit learn mode for this isr
    // not first isr, should finish current learn isr
    if (!learned_isr.empty()) {
      // all learn state is done
      working_list.erase(state);
      if (!working_list.size()) {
        getWarningsStream(state)
            << "LEARN finish for isr:" << current_isr << "\n";
        // For crash when learn;
        plgState->reset_hardware();
        plgState->reset_isr();
        const auto time_isr_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration_isr = time_isr_end - time_temp;
        getWarningsStream(state)
            << "LEARN ISR " << current_isr
            << " use time:" << F2Dec(duration_isr.count()) << " seconds\n";
        if (learned_isr.size() == isrs.size()) {  // learn mode finish
          switch_mode(state);
        }
      } else {
        s2e()->getExecutor()->terminateState(
            *state, "LEARN mode " + std::to_string(working_list.size()));
      }
    }
    // start learn isr
    isr_num = isrs[cur_isr_ptr];
    current_isr = isr_num;
    learned_isr.push_back(isr_num);
    plgState->push_isr(isr_num);
    getWarningsStream(state) << "LEARN start for isr:" << isr_num << "\n";
    time_temp = std::chrono::high_resolution_clock::now();
    interrupt_end = pc;
    working_list.insert(state);
  }

  //////////////////////
  /// LEARN FINISH MODE

  if (!learn_mode) {
    // TODO: multiple input resource
    for (const auto& isr : input_isr) {
      isr_num = isr;
      break;
    }
    plgState->push_isr(isr_num);

    // packet ++
    if (packet_mode) {
      uint64_t prev_packet = plgState->packet_inc();
      plgState->clr_in_ptr();
      debug_stream(state)
          << "from " << hexval(prev_packet)
          << " to next packet because trigger next input loop\n";
    }
  }

  getInfoStream(state) << "External Interrupt:" << isr_num << "\n";
  // if (learned_isr.find(isr_num) == learned_isr.end()) {
  // start learning current isr
  // if (learned_isr.size() < isrs.size()) {  // test
  //   getWarningsStream(state) << "LEARN start for isr:" << isr_num << "\n";
  //   // TEST not right, because learn sometimes skip one isr
  //   plgState->isr_dec();
  //   // TEST END
  //   // test
  //   isr_num = isrs[cur_isr_ptr];
  //   // test end
  //   current_isr = isr_num;
  //   learned_isr.insert(std::make_pair(isr_num, false));
  //   // set interrupt
  //   interrupt_end = pc;
  //   switch_mode(state);
  //   working_list.insert(state);
  // }

#if defined(TARGET_I386) || defined(TARGET_X86_64)
// fake
#elif defined(TARGET_ARM)
  // test
  auto cpu_state_ = state->regs()->getCpuState();
  cpu_state_->v7m.exception = 0;
  getInfoStream(state) << "test interrupt_flag " << cpu_state_->interrupt_flag
                       << " request:" << cpu_state_->interrupt_request
                       << " halted: " << cpu_state_->halted
                       << " excp_idx: " << cpu_state_->exception_index
                       << " pendings: " << cpu_state_->v7m.pending_exception
                       << " nvic: " << (uint64_t)cpu_state_->nvic
                       << " boot info: " << (uint64_t)cpu_state_->boot_info
                       << " cpsr: " << hexval(cpu_state_->uncached_cpsr)
                       << "\n";
  // test end
  s2e()->getExecutor()->setExternalInterrupt(isr_num);
#else
#error Unsupported target architecture
#endif
}

void TraceReplay::onJumpExecution(S2EExecutionState* state, uint64_t pc) {
  uint64_t pc_to = forcejump_map.find(pc)->second;
  state->regs()->setPc(pc_to);
  debug_stream(state) << "force jump from " << hexval(pc) << " to "
                      << hexval(pc_to) << "\n";
  throw CpuExitException();
}

void TraceReplay::load_packets(const std::string& dir_name,
                            std::vector<std::vector<uint8_t>>& packets) {
  getInfoStream(g_s2e_state)
      << "load packet based inputs from " << dir_name << "\n";
  // use for sort by name
  std::set<std::filesystem::path> files_;
  for (const auto& entry : std::filesystem::directory_iterator(dir_name)) {
    files_.insert(entry.path());
  }
  for (auto& file_ : files_) {
    std::vector<uint8_t> input_;
    load_input(file_, input_);
    if (!input_.size()) {
      getWarningsStream(g_s2e_state) << "Empty file: " << file_ << "\n";
      continue;
    }
    packets.push_back(input_);
  }
}

void TraceReplay::load_input(const std::string& file_name,
                          std::vector<uint8_t>& input) {
  getInfoStream(g_s2e_state) << "load_input: " << file_name << "\n";
  auto in_file = fopen(file_name.c_str(), "r");
  if (in_file == nullptr) {
    getWarningsStream() << "ERROR: cannot open " << file_name
                        << ", maybe not exist\n";
    exit(0);
  }

  int t;
  while ((t = fgetc(in_file)) != EOF) {
    input.push_back(t);
  }

  if (ferror(in_file)) {
    getWarningsStream() << "ERROR: encounter error when read from " << file_name
                        << "\n";
    exit(0);
  }

  fclose(in_file);
}

void TraceReplay::load_symbols(const std::string fname) {
  std::ifstream file(fname);
  std::string line;

  if (!file.is_open()) {
    getWarningsStream(g_s2e_state)
        << "Cannot load symbol file: " << fname << "\n";
  }

  uint32_t num_ = 0;
  while (std::getline(file, line)) {
    std::istringstream iss(line);
    std::string address_str, name;

    if (iss >> address_str >> name) {
      // convert hex string to unsigned long
      unsigned long address = std::stoul(address_str, nullptr, 16);
      symbols[address] = name;
    }
  }
  getInfoStream(g_s2e_state) << "Load " << num_ << " functions\n";
  file.close();
}

void TraceReplay::print_trace(S2EExecutionState* state, ref<Expr> expr) {
  std::stringstream ss;
  std::vector<ref<ReadExpr>> reads;

  ss << expr;
  findReads(expr, false, reads);
  for (auto read : reads) {
    // for example: v20_0x4008869c_0x2041d8_15_4_20
    auto seq = getSeq(read);
    if (seq != hardware_symb_seq.end()) {
      ss << ": trace ";
      for (auto i : seq->second) ss << " " << hexval(i);
      ss << "\n";
    } else {
      ss << "ERROR : no record in sequence \n";
    }
  }
  getWarningsStream(state) << ss.str();
}

void TraceReplay::onSymbolicAddress(S2EExecutionState* state,
                                 ref<Expr> virtualAddress,
                                 uint64_t concreteAddress, bool& concretize,
                                 CorePlugin::symbolicAddressReason reason) {
  uint64_t pc = state->regs()->getPc();
  if (reason == CorePlugin::symbolicAddressReason::PC) {
    getWarningsStream(state) << "onSymbolicAddress PC - PC: " << hexval(pc)
                             << " EIP: " << hexval(concreteAddress) << "\n";
    std::stringstream ss;
    std::vector<ref<ReadExpr>> reads;

    ss << virtualAddress;
    findReads(virtualAddress, false, reads);
    for (auto read : reads) {
      // for example: v20_0x4008869c_0x2041d8_15_4_20
      auto seq = getSeq(read);
      if (seq != hardware_symb_seq.end()) {
        ss << ": trace ";
        for (auto i : seq->second) ss << " " << hexval(i);
        ss << "\n";
      } else {
        ss << "ERROR : no record in sequence \n";
      }
    }
    getWarningsStream(state) << ss.str();
    crash_handler(state, pc, INVALID_PC, ss.str());
  } else {
    std::string msg("onSymbolicAddress(Memory)");
    llvm::raw_string_ostream ss(msg);
    ss << " PC: " << hexval(state->regs()->getPc())
       << " Concrete Address: " << hexval(concreteAddress) << " ";
    virtualAddress->print(ss);
    getWarningsStream(state) << ss.str() << "\n";
    // Query query(state->constraints(), virtualAddress);
    // auto range = state->solver()->getRange(query);
    // uint64_t max_ = dyn_cast<ConstantExpr>(range.second)->getZExtValue();
    // uint64_t min_ = dyn_cast<ConstantExpr>(range.first)->getZExtValue();
    // ss << " min: " << hexval(min_) << " max: " << hexval(max_) << "\n";
    // if (isInvalid(min_) || isInvalid(max_)) {
    //   crash_handler(state, pc, SYM_ADDR, ss.str());
    // }
  }
}

static bool symbhw_is_care(struct MemoryDesc* mr, uint64_t physaddr,
                           uint64_t size, void* opaque) {
  // return true;
  // TODO: debug to figure out why this cannot hook
  TraceReplay* iva = static_cast<TraceReplay*>(opaque);
  // g_s2e->getDebugStream() << "care: " << hexval(physaddr) << "\n";
  bool is_care = iva->isCare(physaddr);
  return is_care;
}

bool TraceReplay::isCare(uint64_t physaddr) {
  return isMmio(physaddr) || isInvalid(physaddr) || isHook(physaddr) ||
         isDMA(physaddr);
}

inline bool TraceReplay::isInvalid(uint64_t physaddr) {
  if (physaddr < mmio_base_addr) {
    if (physaddr > ram_end) return true;
    if (physaddr < null_region) return true;
  } else {
    if (physaddr < NVIC_ADDR) {
      if (physaddr > mmio_base_addr + mmio_size) {
        if (snapshot_rams.size() <= 2) return true;
        // Fuck MIMXRT1064-EVK, not obey ARM specification
        // its rom in 0x70000000 > 0x20000000
        if (physaddr < rom_start)
          return true;
        else if (physaddr < snapshot_rams[2] && physaddr > rom_end)
          return true;
        else if (physaddr > snapshot_rams[2] + snapshot_rams[3])
          return true;
      }
    } else {
      if (physaddr > NVIC_END) return true;
    }
  }
  return false;
}

inline bool TraceReplay::isMmio(uint64_t physaddr) {
  return physaddr >= mmio_base_addr && physaddr <= mmio_base_addr + mmio_size;
}

inline bool TraceReplay::isHook(uint64_t physaddr) {
  return std::binary_search(hook_read.begin(), hook_read.end(), physaddr);
}

inline bool TraceReplay::isDMA(uint64_t physaddr) {
  // dma_addr not zero means DMA available
  return (dma_addr && physaddr >= dma_addr && physaddr <= (dma_addr + dma_len));
}

static ref<Expr> symbhw_symbread(struct MemoryDesc* mr, uint64_t physaddress,
                                 const ref<Expr>& value,
                                 SymbolicHardwareAccessType type,
                                 void* opaque) {
  TraceReplay* iva = static_cast<TraceReplay*>(opaque);
  // DMA before MMIO because DMA may inside MMIO
  if (iva->isDMA(physaddress)) {
    return iva->onDMARead(g_s2e_state, physaddress, value);
  } else if (iva->isMmio(physaddress)) {
    if (iva->isRandom()) {
      return iva->onRandomRead(g_s2e_state, physaddress, value);
    } else {
      return iva->onSymbRead(g_s2e_state, physaddress, value);
    }
  } else if (iva->isInvalid(physaddress)) {
    return iva->onInvalidRead(g_s2e_state, physaddress, value);
  } else if (iva->isHook(physaddress)) {
    return iva->onHookRead(g_s2e_state, physaddress, value);
  } else {
    return value;
  }
}

klee::ref<klee::Expr> TraceReplay::onDMARead(S2EExecutionState* state,
                                          uint64_t physaddress,
                                          const klee::ref<klee::Expr>& value) {
  uint32_t offset = physaddress - dma_addr;
  // TODO: packet based input
  // if (offset > input.size()) {
  //   s2e()->getWarningsStream(state)
  //       << "Error: access packet overflow offset: " << offset
  //       << " len:" << input.size() << "\n";
  //   g_s2e_state->createSymbolicValue("DMA", 32);
  // }
  uint32_t pc = state->regs()->getPc();
  uint32_t size = value->getWidth() / 8;
  std::vector<uint64_t> map_vec = {size, pc, physaddress};
  std::vector<uint8_t> return_val;
  // add infer results
  if (learn_mode) {
    debug_stream(state) << "LEARN isr:" << current_isr
                        << " user DMA input pc:" << hexval(pc)
                        << " addr:" << hexval(physaddress) << "\n";
    input_isr.insert(current_isr);
    AddStateConstraints(state, InferType::READ);
  }
  // if mmio size always 1
  if (physaddress >= mmio_base_addr &&
      physaddress <= mmio_base_addr + mmio_size) {
    // !!! this is only for cc2538 RX FIFO !!!
    // maybe the ram is 4 bytes aligned?
    return_val.push_back(input[offset / 4]);
    for (size_t i = 1; i < size; i++) {
      return_val.push_back(0);
    }
  } else {
    for (size_t i = 0; i < size; i++) {
      if (learn_mode) {
        return_val.push_back(0);
      }
      // TODO: packet based
      return_val.push_back(input[offset + i]);
    }
  }
  std::stringstream ss;
  ss << hexval(physaddress) << "@" << hexval(pc);
  ss << "@" << offset << "@" << size;
  getInfoStream(state) << "DMA read pc " << hexval(pc) << " addr "
                       << hexval(physaddress) << " fuzzin " << return_val
                       << " offset " << offset << " size " << size << "\n";
  if (!analysis) {
    return ConstantExpr::create(Vec2Val(return_val), size * 8);
  }
  ref<Expr> user_expr_ =
      g_s2e_state->createSymbolicValue(ss.str(), size * 8, return_val);
  create_seq(user_expr_, map_vec, InMode::USER);
  return user_expr_;
}

void TraceReplay::write_inputs(S2EExecutionState* state) {
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  std::string file_dir =
      std::filesystem::path(s2e()->getConfigFilePath()).parent_path().string();
  // hardware inputs
  std::ofstream hardware_file(file_dir + "/" +
                                  input_file.substr(0, input_file.size() - 2) +
                                  "hardware_inputs",
                              std::ios::binary);

  if (!hardware_file.is_open()) {
    getWarningsStream(state)
        << "Unable to open hardware file " << file_dir << "/hardware_inputs"
        << "\n";
  }

  std::vector<uint32_t> hardware_input_;

  for (auto hardware_expr_ : plgState->get_hardware()) {
    uint32_t val = 0;
    ref<ConstantExpr> ce;
    if (isa<ConstantExpr>(hardware_expr_)) {
      ce = dyn_cast<ConstantExpr>(hardware_expr_);
    } else {
      ce = dyn_cast<ConstantExpr>(state->concolics->evaluate(hardware_expr_));
    }
    val = ce->getZExtValue();
    hardware_input_.push_back(val);
  }

  uint32_t size = hardware_input_.size();
  hardware_file.write(reinterpret_cast<const char*>(&size), sizeof(size));

  hardware_file.write(reinterpret_cast<const char*>(hardware_input_.data()),
                      hardware_input_.size() * sizeof(uint32_t));

  hardware_file.close();

  // isr inputs
  std::ofstream isr_file(file_dir + "/" +
                             input_file.substr(0, input_file.size() - 2) +
                             "isr_inputs",
                         std::ios::binary);

  if (!isr_file.is_open()) {
    getWarningsStream(state)
        << "Unable to open isr file " << file_dir << "/isr_inputs"
        << "\n";
  }

  size = plgState->get_isr().size();
  isr_file.write(reinterpret_cast<const char*>(&size), sizeof(size));

  isr_file.write(reinterpret_cast<const char*>(plgState->get_isr().data()),
                 plgState->get_isr().size() * sizeof(uint32_t));

  isr_file.close();
}

void TraceReplay::crash_handler(S2EExecutionState* state, uint64_t pc,
                             crash_t reason, std::string reason_str) {
  // reason first for debug
  getWarningsStream(state) << "[crash_handler] reason: " << reason_str << "\n";
  if (random) {
    getWarningsStream(state) << "random\n";
    s2e()->getExecutor()->terminateState(
        *state, "random selection replay symbolic state DFS");
    return;
  }
  if (fidelity) {
    // trace output
    if (in_trace) {
      // output crash snapshot
      uint32_t val_;
      getWarningsStream(state) << "output crash snapshot\n";
      for (size_t i = 0; i < 16; i++) {
        state->regs()->read(CPU_OFFSET(regs[i]), &val_, sizeof(val_), false);
        getWarningsStream(state) << "reg[" << i << "]:" << hexval(val_) << "\n";
      }
      getWarningsStream(state) << "ram start " << hexval(ram_start) << " end "
                               << hexval(ram_end) << "\n";
      uint8_t ram_[ram_end - ram_start + 1];
      for (size_t i = 0; i <= (ram_end - ram_start); i++) {
        state->mem()->read(ram_start + i, &ram_[i], sizeof(ram_[0]),
                           s2e::PhysicalAddress);
      }
      std::string file_dir = std::filesystem::path(s2e()->getConfigFilePath())
                                 .parent_path()
                                 .string();
      // hardware inputs
      std::ofstream memshot_file(
          file_dir + "/" + input_file.substr(0, input_file.size() - 2) +
              "crash_memshot",
          std::ios::binary);

      if (!memshot_file.is_open()) {
        getWarningsStream(state)
            << "Unable to open memshot file " << file_dir << "/hardware_inputs"
            << "\n";
      }

      uint32_t size = ram_end - ram_start + 1;
      memshot_file.write(reinterpret_cast<const char*>(&size), sizeof(size));

      memshot_file.write(reinterpret_cast<const char*>(ram_),
                         (ram_end - ram_start + 1) * sizeof(uint8_t));

      memshot_file.close();
      getWarningsStream(state) << "Trace finish, exit\n";
      exit(0);
    }
    in_trace = true;
    getWarningsStream(state)
        << "start output trace at pc: " << hexval(pc) << "\n";
    s2e()->getCorePlugin()->onConcreteDataMemoryAccess.connect(
        sigc::mem_fun(*this, &TraceReplay::onConcreteDataMemoryAccess));
    ResumeFromSnapshot(state, state->regs()->getPc());
    // switch_mode(state);
  } else {
    if (learn_mode && reason == INPUT_DRAINED) {
      getWarningsStream(state)
          << "WARNING: input drained in LEARN mode, do nothing\n";
      return;
    }
    if (!analysis) {
      getWarningsStream(state) << "not Fidelity and not analysis\n";
      auto time_end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double> duration = time_end - time_start;
      getWarningsStream(state)
          << "Execution Time: " << F2Dec(duration.count()) << " seconds\n";
      write_inputs(state);
      exit(0);
    }
  }
  return;
}

inline klee::ref<klee::Expr> TraceReplay::onInvalidRead(
    S2EExecutionState* state, uint64_t physaddress,
    const klee::ref<klee::Expr>& value) {
  uint64_t pc = state->regs()->getPc();
  std::string reason =
      "Invalid pointer Read at addr: " + hexval(physaddress).str() +
      " pc: " + hexval(pc).str();
  getWarningsStream(state) << reason << "\n" << value << "\n";
  crash_handler(state, pc, INVALID_READ, reason);
  return value;
}

klee::ref<klee::Expr> TraceReplay::onHookRead(S2EExecutionState* state,
                                           uint64_t physaddress,
                                           const klee::ref<klee::Expr>& value) {
  if (isa<ConstantExpr>(value)) {
    uint64_t start_ptr = 0x0;
    uint64_t pc = g_s2e_state->regs()->getPc();
    size_t size = value->getWidth() / 8;
    std::stringstream ss;
    ss << hexval(physaddress) << "@" << hexval(pc);
    ss << "@" << start_ptr << "@" << 0xff;
    std::vector<uint8_t> return_val;
    uint64_t const_val = dyn_cast<ConstantExpr>(value)->getZExtValue();
    Val2Vec(const_val, size, return_val);
    debug_stream(state) << "HOOK: read " << hexval(physaddress)
                        << " pc:" << hexval(pc)
                        << " data: " << hexval(const_val) << "\n";

    // create symbolic value and insert to seq
    std::vector<uint64_t> map_vec = {size, pc, physaddress};
    klee::ref<Expr> hook_expr_ =
        g_s2e_state->createSymbolicValue(ss.str(), size * 8, return_val);
    create_seq(hook_expr_, map_vec, InMode::HARDWARE);
    return hook_expr_;
  } else {
    uint64_t pc = g_s2e_state->regs()->getPc();
    debug_stream(state) << "hook constant read pc:" << hexval(pc)
                        << " addr:" << hexval(physaddress) << "\n";
    return value;
  }
}

void TraceReplay::Val2Vec(uint64_t in, unsigned size, std::vector<uint8_t>& out) {
  union {
    uint64_t value;
    uint8_t array[8];
  };

  value = in;
  out.resize(size);
  for (unsigned i = 0; i < size; ++i) {
    out[i] = array[i];
  }
}

inline void TraceReplay::AddStateConstraints(S2EExecutionState* state,
                                          InferType type) {
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  auto constraints = plgState->getConstraints();
  for (auto constraint : constraints) {
    AddConstraint(state, constraint, type);
    // TODO: FIXME: judge if this judge (type == InferType::READ) can be deleted
    if (type == InferType::READ) {
      AddConstraint(state, NotExpr::create(constraint), InferType::EXIT);
    }
  }
  plgState->clearConstraints();
}

inline std::stringstream TraceReplay::get_input(S2EExecutionState* state,
                                             uint64_t& in) {
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  std::stringstream in_ss;
  uint64_t cur_in_ptr = plgState->input_inc();
  if (packet_mode) {
    uint64_t cur_pkt_ptr = plgState->get_packet_ptr();
    in = packets[cur_pkt_ptr][cur_in_ptr];
    in_ss << cur_pkt_ptr << ":" << cur_in_ptr;
  } else {
    in = input[cur_in_ptr];
    in_ss << cur_in_ptr;
  }
  return in_ss;
}

ref<Expr> TraceReplay::onSymbRead(S2EExecutionState* state, uint64_t physaddress,
                               const ref<Expr>& value) {
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  uint64_t pc = state->regs()->getPc();
  uint64_t concreteValue = g_s2e_state->toConstantSilent(value)->getZExtValue();
  uint64_t key = pc << XLEN | physaddress;

  // skip
  auto skip_iter = skip_read.find(key);
  if (skip_iter != skip_read.end()) {
    debug_stream(state) << "skip user defined read point pc:" << hexval(pc)
                        << ",addr:" << hexval(physaddress) << "\n";
    return value;
  }
  // skip end

  // indirect call
  // FIXME: send all callind without thought
  if (callind.size()) {
    for (auto callind_ : callind) {
      debug_stream(state) << "Callind point to read original pc:"
                          << hexval(callind_.first)
                          << " target:" << hexval(callind_.second)
                          << " new:" << hexval(pc) << "\n";
      send_callind(callind_.first, pc);
    }
    callind.clear();
  }
  // end

  // DR (user address) read
  // TODO: change name of user address to DR data register
  if (std::binary_search(user_addrs.begin(), user_addrs.end(), physaddress)) {
    size_t size = value->getWidth() / 8;
    if (send_ghidra(state, pc, FAKE_READ) == GhidraResp::FAKE) {
      debug_stream(g_s2e_state) << "fake read pc " << hexval(pc) << " addr "
                                << hexval(physaddress) << "\n";
      return ConstantExpr::create(0x0, size * 8);
    }

    if (is_inputs_end(state)) {
      std::string reason("Input Drained at pc:" + hexval(pc).str() + "\n");
      crash_handler(state, pc, INPUT_DRAINED, reason);
    }

    std::stringstream ss;
    std::vector<uint8_t> return_val;
    uint64_t byte;
    std::stringstream in_ss = get_input(state, byte);
    ss << hexval(physaddress) << "@" << hexval(pc) << "@" << in_ss.str() << "@"
       << size;
    // TODO: normally
    return_val.push_back(byte);
    for (size_t i = 1; i < size; i++) {
      return_val.push_back(0);
    }
    debug_stream(g_s2e_state)
        << "user read pc " << hexval(pc) << " addr " << hexval(physaddress)
        << " original " << hexval(concreteValue) << " fuzzin " << return_val
        << " ptr " << in_ss.str() << "\n";

    std::vector<uint64_t> map_vec = {size, pc, physaddress};

    // send read pc
    if (learn_mode) {
      debug_stream(state) << "LEARN isr:" << current_isr
                          << " user input pc:" << hexval(pc) << "\n";
      input_isr.insert(current_isr);
      // if(!send_user(pc))
      AddStateConstraints(state, InferType::READ);
      // } else {
      //     debug_stream(state) << "User input in const loop " << hexval(pc)
      //     << "\n"; const_loop_user = true;
      // }
    }

    if (!analysis) {
      return ConstantExpr::create(Vec2Val(return_val), size * 8);
    }
    ref<Expr> user_expr_ =
        g_s2e_state->createSymbolicValue(ss.str(), size * 8, return_val);
    create_seq(user_expr_, map_vec, InMode::USER);
    return user_expr_;
  } else {  // not user defined read point
    ref<Expr> ret_expr_;
    uint8_t size = value->getWidth() / 8;
    auto result = infer_results.find(pc << 32 | physaddress);
    // if has been inferred, use the result
    if (result != infer_results.end()) {
      uint64_t res_val_ = result->second;
      // test
      if (pc == 0x20317a) {
        res_val_ = 0x0;
      }
      // test end
      getInfoStream(state) << "hardware infer read pc " << hexval(pc)
                           << " addr " << hexval(physaddress) << " size "
                           << hexval(size) << " original "
                           << hexval(concreteValue) << " new "
                           << hexval(res_val_) << "\n";

      // // test
      // if (plgState->get_in_ptr() >= input.size()) {
      //   getInfoStream(state) << "change SR\n";
      //   // TODO: change to other result
      //   return ConstantExpr::create(~result->second, size * 8);
      // }
      // // test
      ret_expr_ = ConstantExpr::create(res_val_, size * 8);
    } else {
      // test
      if (physaddress == 0x4000b140) {
        rtc0++;
        if (rtc0 < 3) {
          hardcode_stream(state) << "return 0x4000b140 value 0 \n";
          return ConstantExpr::create(0, size * 8);
        } else {
          hardcode_stream(state) << "return 0x4000b140 value 1 \n";
          return ConstantExpr::create(1, size * 8);
        }
      }
      // test end

      std::stringstream ss;
      ss << hexval(physaddress) << "@" << hexval(pc);
      ss << "@" << 0x0 << "@" << size;
      std::vector<uint64_t> map_vec = {size, pc, physaddress};
      // test
      std::vector<uint8_t> val_ = {0, 0, 0, 0};
      // test end
      ret_expr_ = g_s2e_state->createSymbolicValue(
          ss.str(), static_cast<Expr::Width>(size * 8), val_);
      debug_stream(state) << "No model read pc " << hexval(pc) << " addr "
                          << hexval(physaddress) << " original "
                          << hexval(concreteValue) << " symb " << ret_expr_
                          << "\n";
      create_seq(ret_expr_, map_vec, InMode::HARDWARE);
    }
    // if (!learn_mode) {
    plgState->push_hardware(ret_expr_);
    // }
    return ret_expr_;
  }
  return value;
}

ref<Expr> TraceReplay::onRandomRead(S2EExecutionState* state, uint64_t physaddress,
                                 const ref<Expr>& value) {
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  uint64_t pc = g_s2e_state->regs()->getPc();

  if (plgState->get_in_ptr() >= input.size() &&
      std::binary_search(user_addrs.begin(), user_addrs.end(), physaddress)) {
    std::string reason("Input Drained at PC:" + hexval(pc).str() + "\n");
    crash_handler(state, pc, INPUT_DRAINED, reason);
  }

  uint64_t concreteValue = g_s2e_state->toConstantSilent(value)->getZExtValue();

  if (std::binary_search(user_addrs.begin(), user_addrs.end(), physaddress)) {
    std::stringstream ss;
    uint64_t cur_in_ptr = plgState->input_inc();
    ss << hexval(physaddress) << "@" << hexval(pc);
    size_t size = value->getWidth() / 8;
    ss << "@" << cur_in_ptr << "@" << size;
    std::vector<uint8_t> return_val;
    return_val.push_back(input[cur_in_ptr]);
    for (size_t i = 1; i < size; i++) {
      return_val.push_back(0);
    }
    debug_stream(g_s2e_state)
        << "user read pc " << hexval(pc) << " addr " << hexval(physaddress)
        << " original " << hexval(concreteValue) << " fuzzin " << return_val
        << " ptr " << cur_in_ptr << "\n";

    std::vector<uint64_t> map_vec = {size, pc, physaddress};

    return ConstantExpr::create(Vec2Val(return_val), size * 8);
  } else {
    uint8_t size = value->getWidth() / 8;
    std::stringstream ss;
    ss << hexval(physaddress) << "@" << hexval(pc);
    ss << "@" << 0x0 << "@" << size;
    std::vector<uint64_t> map_vec = {size, pc, physaddress};
    std::vector<uint8_t> val_ = {0, 0, 0, 0};
    ref<Expr> hardware_expr_ = g_s2e_state->createSymbolicValue(
        ss.str(), static_cast<Expr::Width>(size * 8), val_);
    debug_stream(state) << "random hardware response read pc " << hexval(pc)
                        << " addr " << hexval(physaddress) << " original "
                        << hexval(concreteValue) << " symb " << hardware_expr_
                        << "\n";
    create_seq(hardware_expr_, map_vec, InMode::HARDWARE);
    return hardware_expr_;
  }
  return value;
}  // namespace plugins

static void symbhw_symbwrite(struct MemoryDesc* mr, uint64_t physaddress,
                             const ref<Expr>& value,
                             SymbolicHardwareAccessType type, void* opaque) {
  TraceReplay* iva = static_cast<TraceReplay*>(opaque);
  if (iva->isInvalid(physaddress)) {
    iva->onInvalidWrite(g_s2e_state, physaddress, value);
  } else if (physaddress == iva->getDMAReg()) {
    iva->onDMAWrite(g_s2e_state, physaddress, value);
  } else if (iva->isMmio(physaddress)) {
    iva->onSymbWrite(g_s2e_state, physaddress, value);
  }
}

void TraceReplay::onDMAWrite(S2EExecutionState* state, uint64_t physaddress,
                          const ref<Expr>& value) {
  uint64_t pc = state->regs()->getPc();
  auto const_expr = state->toConstantSilent(value);
  dma_addr = const_expr->getZExtValue();
  getInfoStream(state) << "Change DMA buffer address: " << hexval(dma_addr)
                       << " at pc: " << hexval(pc) << "\n";
}

void TraceReplay::onInvalidWrite(S2EExecutionState* state, uint64_t physaddress,
                              const ref<Expr>& value) {
  uint64_t pc = state->regs()->getPc();
  std::string reason =
      "Invalid pointer Write at addr: " + hexval(physaddress).str() +
      " pc: " + hexval(pc).str();
  getWarningsStream(state) << reason << "\n" << value << "\n";
  crash_handler(state, pc, INVALID_WRITE, reason);
}

void TraceReplay::onSymbWrite(S2EExecutionState* state, uint64_t physaddress,
                           const ref<Expr>& value) {
  if (std::binary_search(user_addrs.begin(), user_addrs.end(), physaddress)) {
    uint64_t pc = state->regs()->getPc();
    uint64_t zext_value = dyn_cast<ConstantExpr>(value)->getZExtValue();
    getWarningsStream() << "User write pc: " << hexval(pc)
                        << " address: " << hexval(physaddress) << " val "
                        << zext_value << "\n";
    if (learn_mode) {
      debug_stream(state) << "LEARN isr:" << current_isr
                          << " user output pc:" << hexval(pc) << "\n";
      input_isr.insert(current_isr);
      AddStateConstraints(state, InferType::WRITE);
    }
  }
  // getWarningsStream() << "HOOK: write addr:" << hexval(physaddress)
  //                     << " val:" << zext_value << "\n";
}

// function that is emitted before accessing memory at symbolic address
void TraceReplay::onSymbAddrMemAccess(S2EExecutionState* state,
                                   ref<Expr> VirtualAddress, ref<Expr> value,
                                   bool isWrite) {
  uint64_t pc = state->regs()->getPc();
  // evaluate symobolic regs
  ref<ConstantExpr> ce;
  ce = dyn_cast<klee::ConstantExpr>(state->concolics->evaluate(VirtualAddress));

  uint64_t address = ce->getZExtValue();
  // low possibility to call this because, replay make hardware input symbolic
  // mem access in trace
  if (in_trace) {
    uint32_t val_;
    getConcreteValue(state, value, &val_);
    debug_stream(state) << " pc:" << hexval(state->regs()->getPc())
                        << " address:" << hexval(address)
                        << " val:" << hexval(val_) << " size:" << 4
                        << " type:" << (isWrite ? "write" : "read") << "\n";
  }
  debug_stream(state) << "onSymbAddrMemAccess pc:" << hexval(pc)
                      << (isWrite ? " write " : " read ");
  s2e()->getDebugStream() << " addr " << hexval(address) << " symbolic ";
  VirtualAddress->print(s2e()->getDebugStream());
  s2e()->getDebugStream() << "\n";

  if (isInvalid(address)) {
    std::string reason_str = "Invalid mem access " + hexval(address).str() +
                             " at pc " + hexval(pc).str();
    crash_handler(state, pc, SYM_ADDR, reason_str);
  }
  if (!isa<ConstantExpr>(value)) {
    s2e()->getDebugStream() << "symbolic data ";
    VirtualAddress->print(s2e()->getDebugStream());
  }
}

uint64_t TraceReplay::hash_readname(std::string str, SubSymbName name) {
  int i = 0;
  long long ret = 0;
  std::vector<int> underlines;

  if (str.substr(0, 9) == "const_arr") {
    return CONST_KEY;
  }

  for (; i < str.size(); i++) {
    if (str[i] == '_') underlines.push_back(i);
  }
  std::string sub_str_;
  switch (name) {
    case SubSymbName::ADDR: {
      sub_str_ =
          str.substr(underlines[0] + 1, underlines[1] - underlines[0] - 1);
      break;
    }
    case SubSymbName::PC: {
      sub_str_ =
          str.substr(underlines[1] + 1, underlines[2] - underlines[0] - 1);
      break;
    }
    case SubSymbName::START: {
      sub_str_ =
          str.substr(underlines[2] + 1, underlines[3] - underlines[2] - 1);
      break;
    }
    case SubSymbName::SIZE: {
      sub_str_ =
          str.substr(underlines[3] + 1, underlines[4] - underlines[2] - 1);
      break;
    }
    default: {
      getWarningsStream(g_s2e_state)
          << "ERROR: unrecognized part of symbolic name \n";
    }
  }

  // fault handler
  try {
    ret = std::stoi(sub_str_, nullptr, 16);
  } catch (std::invalid_argument const& ex) {
    s2e()->getWarningsStream()
        << "std::invalid_argument::what(): " << ex.what() << '\n'
        << "name: " << str << " subname: "
        << str.substr(underlines[2] + 1, underlines[3] - underlines[2] - 1);
  } catch (std::out_of_range const& ex) {
    ret = std::stoll(sub_str_, nullptr, 16);
  }

  return ret;
}

void TraceReplay::onInstExecuted(S2EExecutionState* state,
                              std::vector<klee::ref<klee::Expr>> op_exprs) {
  uint64_t pc = state->regs()->getPc();
  debug_stream(state) << "pc: " << hexval(pc) << "\n";
  // for (size_t i = 0; i < op_exprs.size(); i++) {
  //     if (!isa<ConstantExpr>(op_exprs[i])) {
  //         debug_stream(state) << "op[" << i << "] pc:" << hexval(pc) << "
  //         expr:" << op_exprs[i] << "\n";
  //     }
  // }
}

void TraceReplay::onReg(
    ExecutionSignal* sig, S2EExecutionState* state /* current state */,
    TranslationBlock* block,
    uint64_t pc /* program counter of the instruction */,
    uint64_t read_regs /* registers read by the instruction */,
    uint64_t write_regs /* registers written by the instruction */,
    bool mem /* instruction accesses memory */) {
  // TODO: insert to
  using namespace klee;
  getDebugStream(state) << "onReg: " << hexval(read_regs)
                        << "write_regs: " << hexval(write_regs) << "\n";
  ref<Expr> regExpr;
  uint32_t i = 0;
  while (read_regs) {
    if (read_regs & 1) {
      regExpr =
          state->regs()->read(CPU_OFFSET(regs[i]), state->getPointerWidth());
      std::vector<ref<ReadExpr>> reads;
      if (!isa<ConstantExpr>(regExpr)) {
        s2e()->getDebugStream()
            << "regs[" << i << "] is symbolic pc:" << hexval(pc) << "\n";
        regExpr->print(s2e()->getDebugStream());
        s2e()->getDebugStream() << "\n";
        findReads(regExpr, false, reads);
        for (auto read : reads) {
          // v20_0x4008869c_0x2041d8_15_4_20
          auto seq = getSeq(read);
          if (seq != hardware_symb_seq.end()) {
            seq->second.push_back(pc);
            s2e()->getDebugStream()
                << "successfully push: " << getReadName(read) << "\n";
          } else {
            s2e()->getWarningsStream()
                << "error : not in sequence " << getReadName(read) << "\n";
          }
        }
      }
    }
    read_regs >>= 1;
    i++;
  }
}

void TraceReplay::ResumeFromSnapshot(S2EExecutionState* state, uint64_t pc) {
  getWarningsStream(state) << "Resume From Snapshot at pc: " << hexval(pc)
                           << "\n";
  // clear state value
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  plgState->clr_packet_ptr();
  plgState->clr_in_ptr();
  plgState->clr_isr_ptr();

  auto iter = snapshot.begin();
  // recover dma
  uint32_t dma_ = iter32(iter);
  dma_addr = dma_;
  debug_stream(state) << "Recover DMA address: " << hexval(dma_addr) << "\n";

  // recover normal registers
  uint32_t sp_ = 0;
  for (size_t i = 0; i < 16; i++) {
    uint32_t reg_ = iter32(iter);
    debug_stream(state) << "write r[" << i << "] :" << hexval(reg_) << "\n";
    state->regs()->write(CPU_OFFSET(regs[i]), reg_);
    if (i == 13) sp_ = reg_;
  }

#if defined(TARGET_ARM)
  auto cpu_state_ = state->regs()->getCpuState();
  // recover ARM specific registers
  uint32_t msp_ = iter32(iter);
  uint32_t psp_ = iter32(iter);
  uint32_t control_ = iter32(iter);
  cpu_state_->v7m.control = control_;
  debug_stream(state) << "write Control :" << hexval(control_) << "\n";
  // recover sp based on CONTORL->SPSEL is not correct, I do not know why?
  // if (control_ & 0x2 /*SPSEL*/) {
  if (psp_ == sp_) {
    cpu_state_->v7m.other_sp = msp_;
    debug_stream(state) << "write other sp: "
                        << hexval(cpu_state_->v7m.other_sp) << "\n";
    // current_sp is not used now
    // state->regs()->write(CPU_OFFSET(v7m.current_sp), psp_);
  } else {
    cpu_state_->v7m.other_sp = psp_;
    debug_stream(state) << "write other sp: "
                        << hexval(cpu_state_->v7m.other_sp) << "\n";
    // state->regs()->write(CPU_OFFSET(v7m.current_sp), msp_);
  }

  // recover the isrs
  isrs.clear();
  uint32_t num = 0;
  // 8 is the max ISER registers
  for (size_t i = 0; i < 7; i++) {
    uint32_t iser_ = iter32(iter);
    uint64_t flag_ = 0x1;
    while (flag_ != 0x100000000) {
      if (iser_ & flag_) {
        debug_stream(state) << "enable External Interrupt " << num << "\n";
        isrs.push_back(num);
      }
      num++;
      flag_ <<= 1;
    }
    // enable all external interrupts
    s2e()->getExecutor()->enableExternalInterruptAll(i);
  }

  // systick mode
  uint32_t systick_reg = iter32(iter);
  if (systick_reg) systick_mode = true;
#endif

#if defined(TARGET_I386) || defined(TARGET_X86_64)
  // fake
#elif defined(TARGET_ARM)
  s2e()->getExecutor()->disableSystickInterrupt(0);
  getWarningsStream(state) << "disable systick\n";
  if (systick_mode) {
    getWarningsStream(state) << "enable systick\n";
    s2e()->getExecutor()->disableSystickInterrupt(7);
  }
#else
#error Unsupported target architecture
#endif

  // recover SRAM memory
  size_t i = 0;
  size_t ram_iter = 0;
  while (iter != snapshot.end()) {
    // multiple rams
    if (i == snapshot_rams[ram_iter + 1]) {
      i = 0;
      ram_iter += 2;
      getWarningsStream(state)
          << "next snapshot iter " << (int)(ram_iter / 2) << " start "
          << hexval(snapshot_rams[ram_iter]) << "\n";
      if (ram_iter > snapshot_rams.size() - 2) {
        getWarningsStream(state) << "snapshot wrong\n";
        exit(0);
      }
    }
    state->mem()->write(snapshot_rams[ram_iter] + i++, *iter++,
                        s2e::PhysicalAddress);
  }
  debug_stream(state) << "recover SRAM for " << hexval(i) << " bytes\n";

  // prepare the symbolic region for DMA to overcome the not exact address in
  // SymbolicMemoryHook
  for (size_t i = 0; i < dma_len; i++) {
    state->mem()->write(
        dma_addr + i, state->createSymbolicValue("DMA_" + std::to_string(i), 8),
        s2e::PhysicalAddress);
  }

  snapshot_mode = false;

  // reset cpu exception state
#if defined(TARGET_I386) || defined(TARGET_X86_64)
  // fake
#elif defined(TARGET_ARM)
  // test init value
  // test end
#else
#error Unsupported target architecture
#endif

  // indicate the snapshot is finish
  throw CpuExitException();
}

void TraceReplay::onBlock(ExecutionSignal* signal, S2EExecutionState* state,
                       TranslationBlock* tb, uint64_t pc) {
  if (snapshot_mode) ResumeFromSnapshot(state, pc);
  if (exit_pcs.size() && learn_mode) {
    // if (!learn_reserved_state || state == learn_reserved_state) {
    //   learn_reserved_state = forkPoint(state);
    // }
    if (std::binary_search(exit_pcs.begin(), exit_pcs.end(), pc)) {
      uint32_t lr = 0;
      state->regs()->read(CPU_OFFSET(regs[14]), &lr, sizeof(lr), false);
      getWarningsStream(state) << "Exit learn at pc: " << hexval(pc)
                               << " lr: " << hexval(lr) << "\n";
      working_list.erase(state);
      // TODO: terminateState is right, but may drain all state, so hardcode now
      s2e()->getExecutor()->terminateState(
          *state, "LEARN mode " + std::to_string(working_list.size()));
    }
  }
  if (trace_block) signal->connect(sigc::mem_fun(*this, &TraceReplay::onBlockEx));
  if (user_only_mode && pc == infer_start) {
    signal->connect(sigc::mem_fun(*this, &TraceReplay::onInferStartEx));
  }
}

void TraceReplay::onBlockEnd(ExecutionSignal* signal, S2EExecutionState* state,
                          TranslationBlock* tb, uint64_t end_pc, bool valid,
                          uint64_t target) {
  if (valid || !learn_mode) {
    return;
  }
  // invalid consists of several situations : pop pc, blx lr, or blx r3
  // the last is what we want
  uint64_t start_pc = tb->pc;
  uint8_t jump_type = 0;
#ifdef CONFIG_SYMBEX
  jump_type = tb->se_tb_type;
#endif
  // TB_CALL_IND
  if (jump_type == 6) {
    debug_stream(state) << "Callind block start_pc:" << hexval(start_pc)
                        << " end_pc: " << hexval(end_pc) << "\n";
    invalid_call.insert(std::make_pair(start_pc, end_pc));
  }
}

void TraceReplay::onInferStartEx(S2EExecutionState* state, uint64_t pc) {
  // the signal connect is for pc, so everytime this pc will trigger this
  // function
  if (!infer_mode) {
    getWarningsStream(state)
        << "Switch to infer mode pc:" << hexval(pc) << "\n";
  }
  infer_mode = true;
}

void TraceReplay::onBlockEx(S2EExecutionState* state, uint64_t pc) {
  // jump out of constant loop
  DECLARE_PLUGINSTATE(TraceReplayState, state);
  if (constant_loop) {
    constant_loop = false;
    debug_stream(state) << "Constant Loop fist pc: " << hexval(pc) << " out_pc "
                        << hexval(plgState->out_pc) << "\n";
    if (pc == plgState->out_pc) {
      AddConstConstraint(state, pc, plgState->current_cond);
      working_list.erase(plgState->other_state);
      s2e()->getExecutor()->terminateState(*plgState->other_state,
                                           "constant loop");
    } else {
      working_list.erase(state);
      AddConstConstraint(state, pc, plgState->other_cond);
      // update wrong input because of constant loop
      s2e()->getExecutor()->terminateState(*state, "constant loop");
    }
  }

  uint32_t lr = 0;
  auto iter = symbols.find(pc);
  state->regs()->read(CPU_OFFSET(regs[14]), &lr, sizeof(lr), false);
  if (iter != symbols.end()) {
    debug_stream(state) << "Ex Block pc: " << hexval(pc) << " lr:" << hexval(lr)
                        << " " << iter->second << "\n";
  } else {
    debug_stream(state) << "Ex Block pc: " << hexval(pc) << " lr:" << hexval(lr)
                        << "\n";
  }

  // 2020-10069 crash
  if (pc == 0xa500) {
    uint32_t r5 = 0;
    state->regs()->read(CPU_OFFSET(regs[5]), &r5, sizeof(r5), false);
    getWarningsStream(state) << "r5: " << hexval(r5) << "\n";
    exit(0);
  }
  // 2020-10069 crash

  // test 2017-14201
  // if (pc == 0x8013d2e) {  // delayed_work_submit_to_queue
  //   delay = true;
  // }

  if (pc == 0x8013f2c) {
    uint32_t r0 = 0;
    state->regs()->read(CPU_OFFSET(regs[0]), &r0, sizeof(r0), false);
    getWarningsStream(state) << "ticks: " << hexval(r0) << "\n";
    r0 = 2001;
    state->regs()->write(CPU_OFFSET(regs[0]), &r0, sizeof(r0));
  }
  // test end
  // systick handler
  systick_times++;

  if (false && systick_times > 1000) {
#if defined(TARGET_I386) || defined(TARGET_X86_64)
    // fake
#elif defined(TARGET_ARM)
    s2e()->getExecutor()->disableSystickInterrupt(0);
    getWarningsStream(state) << "enable systick\n";
    s2e()->getExecutor()->disableSystickInterrupt(7);
#else
#error Unsupported target architecture
#endif
    systick_times = 0;
    if (delay) {
      // TODO: fix systick trigger
      state->regs()->setPc(0x8007f9c);
      getWarningsStream(state)
          << "force jump from " << hexval(pc) << " to z_clock_isr\n";
      throw CpuExitException();
    }
    // systick_disable = true;
  }

  if (infer_mode) {
    if (iter->second == std::string("memcpy")) {
      debug_stream(state) << "memcpy\n";
      ref<Expr> expr_len =
          state->regs()->read(CPU_OFFSET(regs[2]), state->getPointerWidth());
      if (isa<ConstantExpr>(expr_len)) {
        uint32_t len_ = 0;
        state->regs()->read(CPU_OFFSET(regs[2]), &len_, sizeof(len_), false);
        if (len_ > 0xf0000) {
          getWarningsStream(state)
              << "Invalid memcpy Len " << hexval(len_) << "\n";
          exit(0);
        }
      } else {
        std::pair<ref<Expr>, ref<Expr>> range;
        Query query(state->constraints(), expr_len);
        range = state->solver()->getRange(query);
        uint64_t max_ = dyn_cast<ConstantExpr>(range.second)->getZExtValue();
        std::stringstream reason;
        reason << "Expr memcpy length: " << expr_len << "\n max " << max_
               << " at lr " << hexval(lr) << "\n";
        getWarningsStream(state) << reason.str();
        // if(max_ > 2000) {
        //     analysis_mode = true;
        //     // s2e()->getExecutor()->terminateState(*state, reason.str());
        // }
      }
    }
  }

  if (pc == 0xe9b4) {
    if (block_num__ == 10) {
      hardcode_stream(state) << "Trigger External Interrupt 39 because of "
                                "500 basic blocks stuck\n";
      block_num__ = 0;
#if defined(TARGET_I386) || defined(TARGET_X86_64)
      // fake
#elif defined(TARGET_ARM)
      s2e()->getExecutor()->setExternalInterrupt(0xb);
      // rtc0 = true;
#else
#error Unsupported target architecture
#endif
    }
    block_num__++;
  }

  // stuck handler
  if (!plgState->find_block(pc)) {
    plgState->clear_block();
    plgState->insert_block(pc);
  } else if (plgState->block_stuck() &&
             !is_inputs_end(
                 state)) {  // trigger interrrupt when stuck, but not trigger
                            // when input end because of input drain
    plgState->clear_block();
    uint32_t isr_num = 0;
    isr_num = isrs[isr_num];
    if (!learn_mode) {
      getWarningsStream(state)
          << "Trigger interrupt " << isr_num
          << " because of stuck 1000 basic blocks at pc: " << hexval(pc)
          << "\n";

#if defined(TARGET_I386) || defined(TARGET_X86_64)
      // fake
#elif defined(TARGET_ARM)
      s2e()->getExecutor()->setExternalInterrupt(isr_num);
#else
#error Unsupported target architecture
#endif
    }
  } else {
    plgState->inc_block();
  }

  if (false && !learn_mode) {  // del false when static analysis works
    // switch state
    int diff = pc - select_pc;
    if (diff == 2 || diff == 4) {
      if (response == GhidraResp::JUMP) {  // sequentially execute but want jump
        S2EExecutionState* new_state =
            s2e()->getExecutor()->selectNextState(state);
        getWarningsStream(g_s2e_state)
            << "Successfully switch to " << new_state->getID() << "\n";
      }
    } else if (response == GhidraResp::NOT) {
      S2EExecutionState* new_state =
          s2e()->getExecutor()->selectNextState(state);
      getWarningsStream(g_s2e_state)
          << "Successfully switch to " << new_state->getID() << "\n";
    }

    response = GhidraResp::MAYBE;
    select_pc = 0x0;
    return;
  }

  // consider comment this because to hook situation like more
  // after return from callind without fork
  // if (callind.size() && (pc == callind[callind.size() - 1][0] + 2 || pc ==
  // callind[callind.size() - 1][0] + 4)) {
  //     integer_list callind_ = callind[callind.size() - 1];
  //     callind.pop_back();
  //     debug_stream(state) << "LEARN indirect call pc:" <<
  //     hexval(callind_[0])
  //     << "," << hexval(callind_[1]) <<
  //     "\n"; send_callind(callind_[0], callind_[1]); callind_pc = 0;
  //     callind_target = 0;
  // }

  // record the next block after indirect call
  if (is_callind) {
    is_callind = false;
    callind_target = pc;
    debug_stream(state) << "Callind direct pc:" << hexval(callind_pc)
                        << " target:" << hexval(callind_target) << "\n";
    // callind.insert(std::make_pair(callind_pc, pc));
    callind.insert(std::make_pair(callind_pc, callind_target));
  }

  // if encounter invalid call, record next block
  auto callind_iter = invalid_call.find(pc);
  if (callind_iter != invalid_call.end()) {
    // invalid_call.erase(pc);
    is_callind = true;
    callind_pc = callind_iter->second;
  }

  // random interrupt
  // only increase block_num when not in interrupt
  // if (random && !in_exception && block_num__++ == 1000) {
  if (random && block_num__++ == 1000) {
    block_num__ = 0;
    uint32_t isr_num = isrs[rand() % isrs.size()];
    getInfoStream(state) << "External Random Interrupt:" << isr_num << "\n";
    // can not put it inside onException because RIOT idle in pendsv
    // in_exception = true;
#if defined(TARGET_I386) || defined(TARGET_X86_64)
    // fake
#elif defined(TARGET_ARM)

    s2e()->getExecutor()->setExternalInterrupt(isr_num);
#else
#error Unsupported target architecture
#endif
  }
}

void TraceReplay::onExceptionExit(S2EExecutionState* state, uint32_t irq_no) {
  getInfoStream(state) << "onException exit, irq_no: " << irq_no << "\n";
  // in_exception = false;
}

void TraceReplay::onException(S2EExecutionState* state, unsigned vec,
                           uint64_t pc) {
  debug_stream(state) << "onException pc: " << hexval(pc) << " vec: " << vec
                      << "\n";
  if (vec == 3) {  // hard_fault
    if (learn_mode) {
      // clear exception
#if defined(TARGET_ARM)
      auto cpu_state_ = state->regs()->getCpuState();
      cpu_state_->v7m.exception = 0;
      cpu_state_->interrupt_flag -= 1;
      s2e()->getExecutor()->NVICReset();
#endif
      getInfoStream(state) << "HardFault at pc: " << hexval(pc)
                           << " in learn mode, kill state id  "
                           << state->getID() << "\n";

      // TODO: when isr trigger changed
      // set pc in idle to trigger onIsrExecution but not break original emit
      // and throw
      state->regs()->setPc(idle);
      throw CpuExitException();
    } else {
      s2e()->getExecutor()->terminateState(*state, "hard fault exception");
    }
  }
}

void TraceReplay::onStateFork(
    S2EExecutionState* state, const std::vector<S2EExecutionState*>& new_states,
    const std::vector<klee::ref<klee::Expr>>& new_conds) {
  // !! NOT USE THIS BECAUSE CANNOT CONNECT TARGET WITH STATE !!
  // uint64_t staticTargets[2];
  // if (!state->getStaticBranchTargets(&staticTargets[0], &staticTargets[1]))
  // {
  //   debug_stream(state) << " cannot get branch targets\n";
  // } else {
  //   debug_stream(state) << "branch target: " << hexval(staticTargets[0]) <<
  //   "
  //   "
  //                       << hexval(staticTargets[1]) << "\n";
  // }

  // random select the symbolic state
  if (random) {
    return;
  }

  if (!user_only_mode) {
    return;
  }

  DECLARE_PLUGINSTATE(TraceReplayState, state);

  if (user_condition) {
    for (auto state_ : new_states) {
      if (state_ != state) {
        debug_stream(state) << "add user state " << state_->getID() << "\n";
        user_states.insert(state_);
      }
    }
  } else {  // store current forking condition to corresponding state
    for (size_t i = 0; i < new_states.size(); i++) {
      if (constant_loop) {
        if (state->getID() == new_states[i]->getID()) {
          plgState->current_cond = new_conds[i];
        } else {
          plgState->other_cond = new_conds[i];
          plgState->other_state = new_states[i];
        }
      } else {
        TraceReplayState* NewState = static_cast<TraceReplayState*>(
            getPluginState(new_states[i], &TraceReplayState::factory));
        NewState->AddConstraint(new_conds[i]);
        // add new state's condition and pc for branch-flip ananlysis
        if (state->getID() != new_states[i]->getID()) {
          NewState->fork_cond = state->simplifyExpr(new_conds[i]);
          NewState->fork_pc = state->regs()->getPc();
        }
      }
    }
  }

  if (learn_mode) {
    if (!user_condition) {
      for (auto state_ : new_states) {
        if (state_ != state) {
          working_list.insert(state_);
          debug_stream(state) << "LEARN add state " << state_->getID() << "\n";
        }
      }
    }
  }

  // after LEARN add state
  user_condition = false;

  if (record_fork_states) {
    for (auto state_ : new_states) {
      plgState->add_prev_state(state_);
    }
    record_fork_states = false;
  }

  if (kill_cur_state) {
    kill_cur_state = false;
    // make sure to put it end of func because it will switch the state
    debug_stream(state) << "LEARN erase mode " << state->getID() << "\n";
    working_list.erase(state);
    s2e()->getExecutor()->terminateState(*state, " current state in loop");
    throw CpuExitException();
  }
  // else if (!learn_mode) { // really fork and in infer mode
  //     for (auto state_ : new_states) {
  //         if (state_ != state) {
  //             select_state = state_;
  //             debug_stream(state) << "other state " << state_->getID() <<
  //             "\n";
  //         }
  //     }
  // }
}

void TraceReplay::onStateForkDecide(S2EExecutionState* state, bool* doFork,
                                 const klee::ref<klee::Expr>& condition,
                                 bool* doSwitch) {
  // random select the symbolic state
  if (random) {
    uint64_t pc = state->regs()->getPc();
    bool switch_ = (rand() % 2);
    *doSwitch = switch_;
    getInfoStream(state) << "Random " << (switch_ ? "switch" : "remain")
                         << " the symbolic state at " << hexval(pc) << "\n";
    return;
  }

  DECLARE_PLUGINSTATE(TraceReplayState, state);
  uint64_t pc = state->regs()->getPc();

  // 2020-10061 test
  if (pc == 0x777c) {
    hardcode_stream(state) << "2020-10061 test switch\n";
    *doSwitch = true;
  }
  // 2020-10061 test end

  // CVE-2021-3323 fork user at pc 0x40cb7a but not onStateFork() so init it
  user_condition = false;
  // judge if there are all user inputs in condition
  switch (send_ghidra(state, pc)) {
    case GhidraResp::CONST: {
      constant_loop = true;
      debug_stream(state) << "constant loop pc: " << hexval(pc) << "\n";
      break;
    }
    case GhidraResp::MAYBE: {
      debug_stream(state) << " do not know about pc:" << hexval(pc) << "\n";
      if (plgState->get_fork() == pc) {
        if (plgState->get_fork_time() == loop_time) {
          std::vector<S2EExecutionState*> prev_states =
              plgState->get_prev_states();
          // TODO: fix this because no need to kill state
          // for (auto state_ : prev_states) {
          //     s2e()->getExecutor()->terminateState(*state_, " maybe
          //     loop");
          // }
          // kill_cur_state = true;
          getWarningsStream(state)
              << "switch state because of max fork time in same pc: "
              << hexval(pc) << "\n";
          *doSwitch = true;
          plgState->clear_fork();
          plgState->set_fork(0);
        } else {
          plgState->inc_fork();
          record_fork_states = true;
        }
      } else {
        plgState->clear_fork();
        record_fork_states = true;
        plgState->set_fork(pc);
      }
      break;
    }
    case GhidraResp::JUMP:
    case GhidraResp::NOT: {
      // not fork on time consuming condition
      uint32_t expr_len = ExprLength(condition);
      debug_stream(state) << "loop condition expr length " << expr_len << "\n";
      if (expr_len > max_condition_len) {
        debug_stream(state)
            << "not fork on long condition " << expr_len << "\n";
        *doFork = false;
        break;
      }
      if (learn_mode && learned_fork_pc.find(pc) != learned_fork_pc.end()) {
        debug_stream(state)
            << " not fork on the same pc: " << hexval(pc) << "\n";
        *doFork = false;
      } else {
        debug_stream(state) << " not a constant loop: " << response
                            << " pc:" << hexval(pc) << "\n";
        select_pc = pc;
        plgState->clear_fork();
        plgState->set_fork(0);
      }
      learned_fork_pc.insert(pc);
      break;
    }
    default: {
      getWarningsStream(state) << " not supported ghidra server response \n";
      break;
    }
  }
}

ExecutionState& TraceReplay::selectState() {
  ExecutionState* ret = states.back();
  if (!analysis_mode) {
    // select the last not user states
    for (auto it = states.rbegin(); it != states.rend(); ++it) {
      if (user_states.find(*it) != user_states.end()) continue;
      ret = *it;
      break;
    }
  }

  if (currentState == NULL) {
    currentState = ret;
  }

  if (select_state) {
    getWarningsStream(g_s2e_state)
        << "Searcher: select state:" << select_state->getID() << "\n";
    currentState = select_state;
  }

  S2EExecutionState* s2e_state = dynamic_cast<S2EExecutionState*>(currentState);
  debug_stream(g_s2e_state) << "current state " << s2e_state->getID() << "\n";

  return *currentState;
}

void TraceReplay::update(ExecutionState* current, const StateSet& addedStates,
                      const StateSet& removedStates) {
  bool firstTime = states.size() == 0;
  states.insert(states.end(), addedStates.begin(), addedStates.end());
  for (StateSet::const_iterator it = removedStates.begin(),
                                ie = removedStates.end();
       it != ie; ++it) {
    ExecutionState* es = *it;
    // remove user states
    user_states.erase(es);

    if (currentState == es) {
      currentState = NULL;
    }

    if (select_state == es) {
      getWarningsStream(g_s2e_state)
          << "Searcher: state removed: " << select_state->getID() << "\n";
      select_state = NULL;
    }

    if (es == states.back()) {
      states.pop_back();
    } else {
      bool ok = false;

      for (std::vector<ExecutionState*>::iterator it = states.begin(),
                                                  ie = states.end();
           it != ie; ++it) {
        if (es == *it) {
          states.erase(it);
          ok = true;
          break;
        }
      }

      assert(ok && "invalid state removed");
    }
  }

  if (firstTime) {
    currentState = states[0];
  }
}

}  // namespace plugins
}  // namespace s2e