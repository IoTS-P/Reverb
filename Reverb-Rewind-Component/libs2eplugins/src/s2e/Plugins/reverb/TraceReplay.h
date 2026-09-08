///
/// Copyright (C) 2010-2013, Dependable Systems Laboratory, EPFL
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

#ifndef S2E_PLUGINS_TraceReplay_H
#define S2E_PLUGINS_TraceReplay_H

#include <s2e/CorePlugin.h>
#include <s2e/Plugin.h>
#include <s2e/S2EExecutionState.h>

#include "Wrapper.h"

// socket
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

// State searcher
#include <klee/Searcher.h>

#include <chrono>
#include <queue>

#if defined(TARGET_I386) || defined(TARGET_X86_64)
#include <cpu/i386/cpu.h>
#elif defined(TARGET_ARM)
#include <cpu/arm/cpu.h>
#else
#error Unsupported target architecture
#endif

#include "helper-uRnR.h"

#define debug_stream(state) \
  s2e()->getDebugStream() << "[State " << state->getID() << "] "

#define hardcode_stream(state) \
  getWarningsStream(state) << "TODO: Remove this hard code "

#define iter32(iter) \
  ((*iter++ << 24) | (*iter++ << 16) | (*iter++ << 8) | *iter++)

// only support for arm32 now
#define XLEN 32

// constant key in symb_sequence for special condition
#define CONST_KEY (UINT64_MAX - 1)
#define MEM_KEY (UINT64_MAX - 2)
#define LOOP_TIME 3
#define NVIC_ADDR 0xe0000000
#define NVIC_END 0xe000efff
#define NVIC_MAX_IRQ 200  // max external irq number
namespace s2e {
namespace plugins {

using namespace klee;

class TraceReplay : public Plugin, public klee::Searcher {
  S2E_PLUGIN
 public:
  TraceReplay(S2E* s2e) : Plugin(s2e) {}

  // initialize for plugin with getConfig & connect signals
  void initialize();

  ////////////////////////////////////////////////////////////////////////////////////
  // Helper Funcs

  void write_inputs(S2EExecutionState* state);

  // void NVICReset(void* nvic);

  // get input value
  inline std::stringstream get_input(S2EExecutionState* state, uint64_t& input);

  // whether drain current packet in this read loop
  inline bool is_packet_end(S2EExecutionState* state);

  // whether all inputs is drained
  inline bool is_inputs_end(S2EExecutionState* state);

  // move the iter next packet
  inline void next_packet(S2EExecutionState* state);

  // is random select mode
  inline bool isRandom() { return random; }

  inline uint64_t getDMAReg() { return dma_reg; }
  // get Index from ReadExpr
  inline uint64_t getReadIndex(ref<Expr> read);

  inline ref<Expr> CreateConcatEqRead(S2EExecutionState* state, uint64_t key,
                                      ref<Expr> read);

  // create a equal expression to connect original read and unified read
  inline ref<Expr> CreateEqRead(S2EExecutionState* state, uint64_t key,
                                ref<Expr> read);

  /// @brief Conver an integer to vector
  /// @param in the input integer
  /// @param size the size of integer in byte
  /// @param out the output vector, every item is a byte
  void Val2Vec(uint64_t in, unsigned size, std::vector<uint8_t>& out);

  /// @brief Add Constraint to infer_constraints map
  /// @param state current symbolic state
  /// @param constraint the constraint need to be add
  inline void AddConstraint(S2EExecutionState* state, ref<Expr> constraint,
                            InferType type);

  /// @brief Add Constraints of state when user input to infer_constraints map
  /// @param state current symbolic state
  inline void AddStateConstraints(S2EExecutionState* state, InferType type);

  // add constant loop constraint
  inline uint64_t AddConstConstraint(S2EExecutionState* state, uint64_t pc,
                                     ref<Expr> constraint);

  inline bool EvaluateConcatRead(S2EExecutionState* state,
                                 std::vector<ref<Expr>> constraints,
                                 ref<Expr> read, uint64_t key);

  // evaluate the constraints with solve results before real solve
  inline bool EvaluateConstraint(S2EExecutionState* state,
                                 std::vector<ref<Expr>> constraints,
                                 ref<Expr> expr, uint64_t key, size_t index);

  // Determines the extent of user input present in the given expression.
  ExprUser CheckExprUser(S2EExecutionState* state, ref<Expr> expr);

  inline uint64_t SolveConstraints(S2EExecutionState* state,
                                   std::vector<ref<Expr>> constraints,
                                   ref<Expr> expr);

  void updateInferResult(S2EExecutionState* state,
                         std::vector<ref<ReadExpr>> reads, InferType type);

  inline void StoreInferResult(S2EExecutionState* state, uint64_t result,
                               ref<Expr> read, uint64_t key, size_t index,
                               InferType type);
  // helper function of get readexpr's name
  inline std::string getReadName(const ref<Expr>& expr) const;

  // find readexpr from expr and store their names in read_names
  void findReadNames(const klee::ref<klee::Expr>& expr,
                     std::set<std::string>& read_names);

  /// @brief get the keys(pc+address) using ReadExpr
  /// @param read
  /// @return the key contains pc and address of read
  inline uint64_t getReadKey(S2EExecutionState* state,
                             const ref<ReadExpr> read);

  /// @brief print the
  /// @param state
  /// @param expr
  void print_trace(S2EExecutionState* state, ref<Expr> expr);

  inline std::map<klee::ref<Expr>, std::vector<uint64_t>>::iterator getSeq(
      const klee::ref<Expr> expr, InMode* mode = nullptr);

  /// @brief send packet to ghidra server
  /// @param pc program counter
  /// @return the ghidra response
  GhidraResp send_ghidra(S2EExecutionState* state, uint64_t pc,
                         ghidra_msg_t type = CONSTANT_LOOP);

  /// @brief switch mode betwen learn and infer
  void switch_mode(S2EExecutionState* state);

  /// @brief send indirect call position and target
  /// @param pc inst pc
  /// @param target target pc
  void send_callind(uint64_t pc, uint64_t target);

  /// @brief send user input position
  /// @param state : current symbolic state
  /// @param pc : program counter
  bool send_user(S2EExecutionState* state, uint64_t pc);

  // create sequence and insert to deal with createTempRead
  void create_seq(klee::ref<Expr> expr, std::vector<uint64_t>& seqs,
                  InMode mode);

  ////////////////////////////////////////////////////////////////////////////////////
  // Model Parser

  ExecutionState* forkPoint(S2EExecutionState *state);

  void load_model_from_yml(const std::string& file_name);

  // load function symbols from file
  void load_symbols(const std::string fname);

  void load_input(const std::string& file_name, std::vector<uint8_t>& input);

  void load_packets(const std::string& dir_name,
                    std::vector<std::vector<uint8_t>>& packets);

  void split2vec_with_size(uint64_t val, uint32_t size,
                           std::vector<uint8_t>& vec);

  uint64_t hash_readname(std::string str, SubSymbName name);

  void crash_handler(S2EExecutionState* state, uint64_t pc, crash_t reason,
                     std::string reason_str);

  ////////////////////////////////////////////////////////////////////////////////////
  // Signal Handlers

  /// @brief judge if memory region we cared
  /// @param physaddr physical address
  /// @return true if care
  bool isCare(uint64_t physaddr);

  /// @brief judge if memory region in DMA range
  /// @param physaddr physical address
  /// @return true if in DMA range
  inline bool isDMA(uint64_t physaddr);

  /// @brief judge if memory region is invalid, such as null
  /// @param physaddr physical address
  /// @return true if invalid
  inline bool isInvalid(uint64_t physaddr);

  /// @brief judge if memory region we hook
  /// @param physaddr physical address
  /// @return true if hooked
  inline bool isHook(uint64_t physaddr);

  /// @brief judge if memory region is mmio
  /// @param physaddr physical address
  /// @return true if is mmio
  inline bool isMmio(uint64_t physaddr);

  /// @brief handler when read from invalid region
  /// @param state current symbolic state
  /// @param physaddress physical address
  /// @param value original constant expression or already symbolic, directly
  /// read from memory
  /// @return Symbolic Expression created by user to trace
  inline klee::ref<klee::Expr> onInvalidRead(
      S2EExecutionState* state, uint64_t physaddress,
      const klee::ref<klee::Expr>& value);

  // handler when access invalid pc
  void onInvalidPCAccess(S2EExecutionState* state, uint64_t pc);

  void onConcreteDataMemoryAccess(S2EExecutionState* state, uint64_t vaddr,
                                  uint64_t value, uint8_t size, unsigned flags);

  void onInstExecuted(S2EExecutionState* state,
                      std::vector<klee::ref<klee::Expr>> out_expr);

  void onInstExecution(S2EExecutionState* state, uint64_t pc);

  /// @brief signal handler before fork
  /// @param state current symbolic state
  /// @param doFork not fork if set it false
  /// @param condition fork condition
  /// @param conditionFork recompute the condition to "switch to opposite state"
  /// but execute in current state
  void onStateForkDecide(S2EExecutionState* state, bool* doFork,
                         const klee::ref<klee::Expr>& condition,
                         bool* doSwitch);

  /// @brief handler after fork
  /// @param state current symbolic state
  /// @param new_states vector of newly forked states
  /// @param new_conds conditions related to the newly states
  void onStateFork(S2EExecutionState* state,
                   const std::vector<S2EExecutionState*>& new_states,
                   const std::vector<klee::ref<klee::Expr>>& new_conds);

  /// @brief handler when translate instruction at pc
  /// @param signal execution signal
  /// @param state current symbolic state
  /// @param tb translation block
  /// @param pc program counter
  void onTranslateInstruction(ExecutionSignal* signal, S2EExecutionState* state,
                              TranslationBlock* tb, uint64_t pc);

  /// @brief skip functions handler
  /// @param state current symbolic state
  /// @param pc program counter
  void onSkipExecution(S2EExecutionState* state, uint64_t pc);

  /// @brief force jump handler
  /// @param state current symbolic state
  /// @param pc program counter
  void onJumpExecution(S2EExecutionState* state, uint64_t pc);

  /// @brief interrupt handler
  /// @param state current symbolic state
  /// @param pc program counter
  void onIsrExecution(S2EExecutionState* state, uint64_t pc);

  void onIRQEndExec(S2EExecutionState* state, uint64_t pc);

  /// @brief hook point execution handler
  /// @param state current symbolic state
  /// @param pc program counter
  void onHookExecution(S2EExecutionState* state, uint64_t pc);

  /// @brief handler when translate register
  /// @param sig execution signal
  /// @param state current symbolic state
  /// @param block current basic block
  /// @param pc program counter
  /// @param read_regs registers read by the instruction
  /// @param write_regs regsiters written by the instruction
  /// @param mem instruction memory access
  void onReg(ExecutionSignal* sig, S2EExecutionState* state /* current state */,
             TranslationBlock* block,
             uint64_t pc /* program counter of the instruction */,
             uint64_t read_regs /* registers read by the instruction */,
             uint64_t write_regs /* registers written by the instruction */,
             bool mem /* instruction accesses memory */);

  /// @brief handler when translate basic block
  /// @param signal execution signal
  /// @param state current symbolic state
  /// @param tb translation basic block
  /// @param pc program counter
  void onBlock(ExecutionSignal* signal, S2EExecutionState* state,
               TranslationBlock* tb, uint64_t pc);

  void onBlockEnd(ExecutionSignal* signal, S2EExecutionState* state,
                  TranslationBlock* tb, uint64_t end_pc, bool valid,
                  uint64_t target);

  /// @brief resume the simulator from snapshot
  /// @param state current symbolic state
  /// @param pc program counter
  void ResumeFromSnapshot(S2EExecutionState* state, uint64_t pc);

  /// @brief handler when execution basic block, emit when hook in block
  /// translation
  /// @param state current symbolic state
  /// @param pc program counter
  void onBlockEx(S2EExecutionState* state, uint64_t pc);

  void onException(S2EExecutionState* state, unsigned vec, uint64_t pc);

  void onExceptionExit(S2EExecutionState* state, uint32_t irq_no);

  /// @brief handler when encounter infer start pc
  /// @param state current symbolic state
  /// @param pc program counter
  void onInferStartEx(S2EExecutionState* state, uint64_t pc);

  void onHookBlockEx(S2EExecutionState* state, uint64_t pc);

  void onSymbolicAddress(S2EExecutionState* state,
                         klee::ref<klee::Expr> virtualAddress,
                         uint64_t concreteAddress, bool& concretize,
                         CorePlugin::symbolicAddressReason reason);

  void onSymbAddrMemAccess(S2EExecutionState* state, ref<Expr> VirtualAddress,
                           ref<Expr> value, bool isWrite);

  /// @brief IMPORTANT handler when read mmio region
  /// @param state current symbolic state
  /// @param physaddress physical address
  /// @param value original constant expression or already symbolic, directly
  /// read from memory
  /// @return Symbolic Expression created by user if want hook
  klee::ref<klee::Expr> onSymbRead(S2EExecutionState* state,
                                   uint64_t physaddress,
                                   const klee::ref<klee::Expr>& value);

  /// @brief Random handler when read mmio region
  /// @param state current symbolic state
  /// @param physaddress physical address
  /// @param value original constant expression or already symbolic, directly
  /// read from memory
  /// @return Symbolic Expression created by user if want hook
  klee::ref<klee::Expr> onRandomRead(S2EExecutionState* state,
                                     uint64_t physaddress,
                                     const klee::ref<klee::Expr>& value);

  /// @brief handler when read hook region
  /// @param state current symbolic state
  /// @param physaddress physical address
  /// @param value original constant expression or already symbolic, directly
  /// read from memory
  /// @return Symbolic Expression created by user to trace
  klee::ref<klee::Expr> onDMARead(S2EExecutionState* state,
                                  uint64_t physaddress,
                                  const klee::ref<klee::Expr>& value);

  /// @brief handler when read hook region, mainly used for debug
  /// @param state current symbolic state
  /// @param physaddress physical address
  /// @param value original constant expression or already symbolic, directly
  /// read from memory
  /// @return Symbolic Expression created by user to trace
  klee::ref<klee::Expr> onHookRead(S2EExecutionState* state,
                                   uint64_t physaddress,
                                   const klee::ref<klee::Expr>& value);

  /// @brief handler when write to memory
  /// @param state current symbolic state
  /// @param physaddress physical address
  /// @param value original constant expression or already symbolic, directly
  /// read from memory
  void onSymbWrite(S2EExecutionState* state, uint64_t physaddress,
                   const klee::ref<klee::Expr>& value);

  /// @brief handler when write to invalid address
  /// @param state current symbolic state
  /// @param physaddress physical address
  /// @param value original constant expression or already symbolic, directly
  /// read from memory
  void onInvalidWrite(S2EExecutionState* state, uint64_t physaddress,
                      const klee::ref<klee::Expr>& value);

  /// @brief handler when write to DMA register
  /// @param state current symbolic state
  /// @param physaddress physical address
  /// @param value original constant expression or already symbolic
  void onDMAWrite(S2EExecutionState* state, uint64_t physaddress,
                  const klee::ref<klee::Expr>& value);

  /////////////////////
  // Searcher

  ExecutionState& selectState();
  void update(ExecutionState* current, const StateSet& addedStates,
              const StateSet& removedStates);
  bool empty() { return states.empty(); }
  void printName(llvm::raw_ostream& os) { os << "MyOwnSearcher\n"; }

 private:
  /////////////////////
  // Configs

  std::vector<uint64_t> snapshot_rams;

  // use packet based input as external input
  bool packet_mode = false;

  // max loop time
  uint64_t loop_time = 0;

  // the pc where infer mode start
  // no need in plgstate because one-state mode before
  uint64_t infer_start = 0;

  uint64_t ram_start = 0;

  uint64_t ram_end = 0;

  uint64_t rom_start = 0;

  uint64_t rom_end = 0;

  uint32_t max_condition_len = 300;

  // Hook the read instructions with these address
  std::vector<uint64_t /*addr*/> hook_read;

  // config the null_region
  // TODO: consider to make it a config
  uint64_t null_region = 0x1000;

  // bytes input from input file;
  std::vector<uint8_t> input;

  // packets based input
  std::vector<std::vector<uint8_t>> packets;

  // learn exit pcs
  std::vector<uint64_t /*pc*/> exit_pcs;

  // snapshot
  std::vector<uint8_t> snapshot;

  // skip_points
  std::vector<uint64_t /*pc*/> skip_points;

  // skip read pc and address combined
  std::set<uint64_t /*pc|addr*/> skip_read;

  // force jump
  std::map<uint64_t /*jump_from*/, uint64_t /*jump_to*/> forcejump_map;

  // pc of idle for interrupt_triggers
  uint64_t idle = 0;

  // interrupt numbers which need to be run
  std::vector<uint64_t /*isr_num*/> isrs;

  // address which is user input
  std::vector<uint64_t /*mmio_addr*/> user_addrs;

  // for debug mode
  bool trace_block = true;

  // kill current state
  bool kill_cur_state = false;

  // kill fork states
  bool record_fork_states = false;

  bool user_only_mode = true;

  bool analysis = false;

  // use snapshot or not
  bool snapshot_mode = false;

  bool fidelity = false;

  // random select the symbolic state
  bool random = false;

  /////////////////////
  // Inner states

  ExecutionState* learn_reserved_state = nullptr;

  std::chrono::time_point<std::chrono::high_resolution_clock> time_start;

  std::chrono::time_point<std::chrono::high_resolution_clock> time_temp;

  // queue to save loop pc
  std::queue<uint64_t> fork_pc_queue;

  // bool in_exception = false;

  // this is for generate trace based on recovered hardware response for on more
  // time to avoid triggering crash without finishing the learn mode
  bool in_trace = false;

  // socket
  int client_socket;

  // port of ghidra server
  int ghidra_port = 22733;

  // jump_pc
  uint64_t to_pc;

  // symbol for debug
  std::map<uint64_t, std::string> symbols;

  uint64_t mmio_base_addr = 0x40000000;
  uint64_t mmio_size = 0x20000000;

  bool systick_mode = false;

  uint64_t systick_times = 0x0;

  // test
  uint64_t block_num__ = 0;
  // test end

  /////////////////////
  // hardware infer

  // const loop map, to speed up.
  std::map<uint64_t /*pc*/, uint64_t> const_loop_cache;
  // fake read map, to speed up.
  std::map<uint64_t /*pc*/, GhidraResp> fake_read_cache;

  // forking reads
  // Consider not fork on the same state
  std::map<uint64_t /*key:pc*/, bool> fork_constraints;

  // learn constraints
  std::map<uint64_t /*key: pc+address*/, std::vector<ref<Expr>>>
      infer_constraints;

  // learn write constraints
  std::map<uint64_t /*key: pc+address*/, std::vector<ref<Expr>>>
      infer_write_constraints;

  // learn exit constraints
  std::map<uint64_t /*key: pc+address*/, std::vector<ref<Expr>>>
      infer_exit_constraints;

  // solved constraints
  std::set<ref<Expr>> added_constraints;

  // unified read expressions for solver
  std::map<uint64_t /* key: pc+address*/, std::vector<ref<Expr>>> unified_reads;

  std::map<uint64_t /* key: pc+address*/, ref<Expr>> concat_reads;

  // constraints for constant loop
  std::pair<uint64_t /*fork pc*/, ref<Expr>> const_constraints;

  // infer results
  std::map<uint64_t /*key:pc+address*/, uint32_t /*value*/> infer_results;

  // exit infer results
  std::map<uint64_t /*key:pc+address*/, uint32_t /*value*/> infer_exit_results;

  // write infer results
  std::map<uint64_t /*key:pc+address*/, uint32_t /*value*/> infer_write_results;

  // infer mode
  bool infer_mode = false;

  // learning mode
  bool learn_mode = false;

  // constant loop
  bool constant_loop = false;

  // user input in constant loop
  bool const_loop_user = false;

  bool user_condition = false;

  // interrupt end block
  uint64_t interrupt_end = 0x0;

  // original state before learn
  // TODO: if the switch will handle this, consider delete it
  S2EExecutionState* original_state;

  // working list for hardware infer
  std::set<S2EExecutionState*> working_list;

  // learned fork
  std::set<uint32_t /* pc */> learned_fork_pc;

  // learned isr
  std::vector<uint32_t /*isr_num*/> learned_isr;

  // isr which contains external inputss
  std::set<uint32_t /*isr_num*/> input_isr;

  // current isr
  uint32_t current_isr = 0;

  // the map to trace indirect call
  std::map<uint64_t /*block_pc*/, uint64_t /*pc*/> invalid_call;

  // last block execution encounter callind
  bool is_callind = false;

  // pc for indirect call
  uint64_t callind_pc = 0x0;

  // target for indirect call
  uint64_t callind_target = 0x0;

  // callind stack
  std::set<std::pair<uint64_t /*src*/, uint64_t /*target*/>> callind;

  // state when select
  S2EExecutionState* select_state = NULL;

  // the pc of last block when select state
  uint64_t select_pc = 0x0;

  // ghidra response
  GhidraResp response;

  // the correct hardware input;
  std::map<uint64_t /*pc|address*/, uint64_t /*value*/> solve_inputs;

  /////////////////////
  // State Searcher

  std::vector<klee::ExecutionState*> states;

  std::set<klee::ExecutionState*> user_states;

  klee::ExecutionState* currentState;

  /////////////////////
  // Records

  std::string input_file;

  // expr input
  ExprList in_expr_;

  // expressions output
  ExprList out_expr_;

  // map for hardware input symbolic calculate sequence
  // the vector is consist of [ReadIndex, pc, address, has_unified_read, ...]
  std::map<klee::ref<Expr> /*read_expr*/, std::vector<uint64_t> /*sequences*/>
      hardware_symb_seq;
  std::map<klee::ref<Expr> /*read_expr*/, std::vector<uint64_t> /*sequences*/>
      user_symb_seq;

  /////////////////////
  // Analysis

  bool analysis_mode = false;

  std::pair<crash_t, uint64_t /*pc*/> orig_reason;

  uint32_t crash_state_id = 0;

  std::stringstream report;

  /////////////////////
  // DMAs

  // the address of memory which save the DMA packet
  uint32_t dma_addr = 0;

  // the config DMA packet lenghth
  // 4 for cc2538 RX FIFO
  uint32_t dma_len = 4 * 128;

  // the register to save DMA packet RAM start address
  uint32_t dma_reg = 0;

  // test
  uint32_t rtc0 = 0;
  bool delay = false;
};

}  // namespace plugins
}  // namespace s2e

#endif  // S2E_PLUGINS_TraceReplay_H
