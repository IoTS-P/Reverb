
// FIXME: many of the default imports are not needed
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.component.DecompilerUtils;
import ghidra.app.script.GhidraScript;
import ghidra.graph.DefaultGEdge;
import ghidra.graph.GDirectedGraph;
import ghidra.graph.GraphFactory;
import ghidra.program.model.address.*;
import ghidra.program.model.block.*;
import ghidra.program.model.block.graph.CodeBlockEdge;
import ghidra.program.model.block.graph.CodeBlockVertex;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.pcode.*;
import ghidra.program.model.reloc.*;
import ghidra.program.model.scalar.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.util.*;
import ghidra.util.exception.CancelledException;
import java.io.BufferedReader;
// socket
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.Set;
import java.util.Stack;
import java.util.concurrent.CancellationException;

public class ControlFlowGraph extends GhidraScript {

  // edges between inter functions
  private Map<Function, List<CodeBlockReference>> func_edges;
  private Map<Function, FunctionControlFlowGraph> func_graphs;
  // Server Socket
  ServerSocket server;
  BasicBlockModel basicBlockModel;

  // we currently have two mode
  // learning mode and infer mode
  enum Mode {
    LEARN,
    INFER,
  }

  enum PathDecision {
    JUMP,
    NOT,
    MAYBE,
    CONST, // constant loop
  }

  public static Mode mode;

  public static String pc_pcode(PcodeOp pcode) {
    return pcode.getSeqnum().getTarget().toString() + " " + pcode.toString();
  }

  public CodeBlock get_block(Address addr) {
    try {
      CodeBlock[] blocks =
          basicBlockModel.getCodeBlocksContaining(addr, getMonitor());
      if (blocks.length > 1) {
        printf("WARNING: multiple block in one address %s\n", addr);
      }
      if (blocks.length == 0) {
        printf("WARNING: no block in address %s\n", addr);
        return null;
      }
      return blocks[0];
    } catch (ghidra.util.exception.CancelledException e) {
      printf("ERROR when get_block: addr: %s ,%s\n", addr, e.getMessage());
    }
    return null;
  }

  public class CodeBlockAttributes {
    private CodeBlock block;
    private List<CodeBlockReference> jumps;
    private List<CodeBlockReference> calls;
    // TODO: consider change this map to a signal if there is a indirect load
    private Map<Address, PcodeOp> indirect_loads;
    // TODO: consider del it
    private Map<Address, PcodeOp> indirect_store;
    // TODO: consider change this to a signal if there is a indirect call
    // consider using DFE or just once.
    private List<CodeBlockReference> indirect_call;
    // mmio address
    private boolean has_user_input;
    private boolean constant_loop;

    public CodeBlockAttributes(CodeBlock codeblock) {
      block = codeblock;
      jumps = new ArrayList();
      calls = new ArrayList();
      indirect_loads = new HashMap();
      indirect_store = new HashMap();
      indirect_call = new ArrayList();
      has_user_input = false;
      // we add this to avoid path explosion
      constant_loop = false;
    }

    public List<CodeBlockReference> getSuccessors() {
      // TODO: return seperate list
      List<CodeBlockReference> all = new ArrayList<>();
      all.addAll(jumps);
      all.addAll(calls);
      return all;
    }

    public List<CodeBlockReference> getCalls() { return calls; }

    public List<CodeBlockReference> getJumps() { return jumps; }

    public boolean has_indirect_load() { return !indirect_loads.isEmpty(); }

    public boolean has_indirect_call() { return !indirect_call.isEmpty(); }

    public boolean has_indirect() {
      return has_indirect_call() || has_indirect_load();
    }

    public void print_block() {
      printf("[+] Code Block Start Address: %s\n",
             block.getFirstStartAddress().toString());

      for (CodeBlockReference ref : jumps)
        printf("%s --> %s [jump]\n", ref.getReferent(), ref.getReference());
      for (CodeBlockReference ref : calls)
        printf("%s --> %s [call]\n", ref.getReferent(), ref.getReference());
      if (has_user_input) {
        printf("Has User Input\n");
      }
    }

    // add edge
    public void addEdge(CodeBlockReference edge) {
      // edge == null is for block with no next block such as return
      if (edge != null) {
        if (edge.getFlowType().isJump()) {
          jumps.add(edge);
        } else if (edge.getFlowType().isCall()) {
          calls.add(edge);
        } else {
          // normal execute without jump
          jumps.add(edge);
        }
      }
    }

    // add indirect load
    public void addLoadInd(Address addr, PcodeOp pcode) {
      if (!indirect_loads.containsKey(addr)) {
        indirect_loads.put(addr, pcode);
      } else {
        printf("same indirect load %s %s\n", addr, pcode);
      }
    }

    // add indirect store
    public void addStoreInd(Address addr, PcodeOp pcode) {
      if (!indirect_store.containsKey(addr)) {
        indirect_store.put(addr, pcode);
      } else {
        printf("same indirect store %s %s\n", addr, pcode);
      }
    }

    // add indirect call
    public void addIndCall(CodeBlockReference edge) { indirect_call.add(edge); }

    public boolean has_user_in() {
      if (has_user_input) {
        printf("DEBUG: %s has user input\n", this.block.getFirstStartAddress());
      }
      return has_user_input;
    }

    public boolean has_user_call() {
      for (CodeBlockReference ref : calls) {
        Function dest_ = getFunctionContaining(ref.getReference());
        if (!func_graphs.containsKey(dest_)) {
          GenGraph(dest_);
        }
        if (func_graphs.get(dest_).has_user()) {
          printf("DEBUG: %s has user input\n", dest_);
          return true;
        }
      }
      return false;
    }

    public boolean block_has_user() { return has_user_in() || has_user_call(); }

    // change it to if there is one user read in this block, the block is become
    // read block
    public void add_user(/* Address addr */) {
      // if (indirect_loads.containsKey(addr)) {
      // indirect_loads.remove(addr);
      // has_user_input = true;
      // } else {
      // printf("Not in indirect_loads consider finish or error handler %s \n ",
      // addr);
      // }
      indirect_loads.clear();
      has_user_input = true;
    }

    public void fix_indirect(Address target) {
      if (indirect_call.isEmpty()) {
        return;
      }
      Function dest_ = getFunctionContaining(target);
      CodeBlock dest_block_ = get_block(target);
      if (!func_graphs.containsKey(dest_)) {
        GenGraph(dest_);
      }
      Address source = indirect_call.get(0).getReferent();
      indirect_call.clear();
      CodeBlockReferenceImpl edge = new CodeBlockReferenceImpl(
          block, dest_block_, RefType.UNCONDITIONAL_CALL, target, source);
      calls.add(edge);
    }

    public boolean isTarget() {
      if (mode == Mode.LEARN) {
        return has_indirect();
      } else if (mode == Mode.INFER) {
        return block_has_user();
      } else {
        printf("Discorrect mode %d \n", mode);
        return false;
      }
    }

    public void set_const_loop() {
      printf("DEBUG: block %s is constant loop\n",
             this.block.getFirstStartAddress());
      constant_loop = true;
    }

    public boolean is_const_loop() { return constant_loop; }

    public CodeBlock getBlock() { return this.block; }
  }

  public class CodeBlockDirectedGraph {
    private Map<CodeBlock, CodeBlockAttributes> edges;

    public CodeBlockDirectedGraph() { edges = new HashMap(); }

    public void addVertex(CodeBlock vertex) {
      if (!edges.containsKey(vertex)) {
        edges.put(vertex, new CodeBlockAttributes(vertex));
      }
    }

    public void addEdge(CodeBlock source, CodeBlockReference edge) {
      if (!edges.containsKey(source)) {
        edges.put(source, new CodeBlockAttributes(source));
      }
      edges.get(source).addEdge(edge);
    }

    // add indirect call edge
    public void AddIndCall(CodeBlock source, CodeBlockReference edge) {
      if (!edges.containsKey(source)) {
        edges.put(source, new CodeBlockAttributes(source));
      }
      edges.get(source).addIndCall(edge);
    }

    // add indirect load
    public void AddIndLoad(CodeBlock source, Address addr, PcodeOp pcode) {
      if (!edges.containsKey(source)) {
        edges.put(source, new CodeBlockAttributes(source));
      }
      edges.get(source).addLoadInd(addr, pcode);
    }

    // add indirect store
    public void AddIndStore(CodeBlock source, Address addr, PcodeOp pcode) {
      if (!edges.containsKey(source)) {
        edges.put(source, new CodeBlockAttributes(source));
      }
      edges.get(source).addStoreInd(addr, pcode);
    }

    // add user input when decompile or receive from dynamic execution
    public void AddUserIn(Address pc) {
      CodeBlock source = get_block(pc);
      if (!edges.containsKey(source)) {
        edges.put(source, new CodeBlockAttributes(source));
        printf("WARNING: no code block\n");
      }
      edges.get(source).add_user();
    }

    public void AddConstLoop(CodeBlock block) {
      if (!edges.containsKey(block)) {
        edges.put(block, new CodeBlockAttributes(block));
        printf("WARNING: no code block\n");
      }
      edges.get(block).set_const_loop();
    }

    public void FixIndirect(Address pc, Address target) {
      CodeBlock source = get_block(pc);
      if (!edges.containsKey(source)) {
        edges.put(source, new CodeBlockAttributes(source));
        printf("WARNING: no code block\n");
      }
      edges.get(source).fix_indirect(target);
    }

    public List<CodeBlockReference> getSuccessors(CodeBlock vertex) {
      if (!edges.containsKey(vertex)) {
        throw new IllegalArgumentException(
            "Vertex does not exist in the graph.");
      }
      return edges.get(vertex).getSuccessors();
    }

    public void printGraph() {
      for (Map.Entry<CodeBlock, CodeBlockAttributes> edge : edges.entrySet()) {
        edge.getValue().print_block();
      }
    }

    public List<CodeBlockReference> getCalls() {
      List<CodeBlockReference> call_refs = new ArrayList<>();
      for (Map.Entry<CodeBlock, CodeBlockAttributes> entry : edges.entrySet()) {
        CodeBlockAttributes value = entry.getValue();
        call_refs.addAll(value.getCalls());
      }
      return call_refs;
    }

    // seletect the current path
    public PathDecision infer_select(Address pc) {
      if (learn_select(pc) == PathDecision.CONST) {
        return PathDecision.CONST;
      }
      CodeBlock block = get_block(pc);
      printf("DEBUG: block %s\n", block);
      // two queues represent one in target, the other not
      Queue<CodeBlockAttributes> working_q1 = new LinkedList<>();
      Queue<CodeBlockAttributes> working_q2 = new LinkedList<>();
      CodeBlockAttributes attr_ = get_attribute(block);

      // fork point means jump, only concern jump in first iteration
      if (attr_.getJumps().size() != 2) {
        printf("WARNING: not two jumps %s, %d\n", block,
               attr_.getJumps().size());
        attr_.print_block();
      }
      for (CodeBlockReference ref : attr_.getJumps()) {
        if (ref.getFlowType().isJump()) {
          working_q1.add(get_attribute(ref.getDestinationBlock()));
        } else {
          working_q2.add(get_attribute(ref.getDestinationBlock()));
        }
      }

      // TODO: constant loop in spi_sam_finish
      while (true) {
        if (working_q1.isEmpty()) {
          printf("DEBUG: jump block queue empty \n");
          return PathDecision.NOT;
        }
        // means target branch no target
        CodeBlockAttributes block1_ = working_q1.poll();

        if (working_q2.isEmpty()) {
          printf("DEBUG: execute block queue empty \n");
          return PathDecision.JUMP;
        }
        // means other branch no target
        CodeBlockAttributes block2_ = working_q2.poll();

        if (block1_.isTarget()) {
          printf("DEBUG: block is target: %s\n",
                 block1_.getBlock().getFirstStartAddress());
          return PathDecision.JUMP;
        }
        if (block2_.isTarget()) {
          printf("DEBUG: block is target: %s\n",
                 block2_.getBlock().getFirstStartAddress());
          return PathDecision.NOT;
        }

        for (CodeBlockReference ref : block1_.getSuccessors()) {
          working_q1.add(get_attribute(ref.getDestinationBlock()));
        }
        for (CodeBlockReference ref : block2_.getSuccessors()) {
          working_q1.add(get_attribute(ref.getDestinationBlock()));
        }
      }
      // return PathDecision.MAYBE;
    }

    public PathDecision learn_select(Address pc) {
      CodeBlock block = get_block(pc);
      printf("DEBUG: learn mode select %s\n", block);
      CodeBlockAttributes attr_ = get_attribute(block);

      attr_.print_block();

      if (attr_.is_const_loop()) {
        return PathDecision.CONST;
      }

      // something like spi_sam_finish
      for (CodeBlockReference ref : attr_.getSuccessors()) {
        CodeBlock dest_ = ref.getDestinationBlock();
        CodeBlockAttributes dest_attr_ = get_attribute(dest_);
        if (dest_attr_.is_const_loop()) {
          return PathDecision.CONST;
        }
      }

      return PathDecision.MAYBE;
    }

    public CodeBlockAttributes get_attribute(CodeBlock block) {
      if (!edges.containsKey(block)) {
        Function func = getFunctionContaining(block.getFirstStartAddress());
        printf("DEBUG: another function %s\n", func);
        if (!func_graphs.containsKey(func)) {
          GenGraph(func);
        }
        FunctionControlFlowGraph fcfg = func_graphs.get(func);
        return fcfg.get_attribute(block);
      }
      return edges.get(block);
    }
  }

  public class FunctionControlFlowGraph {
    // protected GDirectedGraph<CodeBlockVertex, CodeBlockEdge> cfg;
    protected CodeBlockDirectedGraph cfg;
    protected Function function;
    private HighFunction hfunc;
    private DecompInterface decomplib;
    private boolean has_user_in;

    public void SetupDecomp() {
      this.decomplib = new DecompInterface();
      this.decomplib.openProgram(currentProgram);
    }

    public CodeBlockAttributes get_attribute(CodeBlock block) {
      return this.cfg.get_attribute(block);
    }

    public HighFunction dec_hfunc(Function f) {
      HighFunction hfunc = null;
      DecompileResults dRes = null;
      try {
        dRes = decomplib.decompileFunction(f, 60, getMonitor());
        hfunc = dRes.getHighFunction();
      } catch (Exception e) {
        printf("Exception in Decompilation\n");
        e.printStackTrace();
      }
      if (!dRes.decompileCompleted()) {
        printf(dRes.getErrorMessage());
        printf("\n");
      }

      if (hfunc == null) {
        printf("ERROR: cannot decompile function\n");
        printf(dRes.getErrorMessage());
      } else {
        printf(hfunc.getFunction().getName());
        printf("\n");
      }
      return hfunc;
    }

    public FunctionControlFlowGraph() {
      this.cfg = new CodeBlockDirectedGraph();
    }

    public long mem_access(PcodeOp pcode) {
      long pcode_addr = pcode.getSeqnum().getTarget().getOffset();
      Varnode var = pcode.getInput(1);
      while (!var.isAddress()) {
        PcodeOp def_pcode = var.getDef();
        if (def_pcode == null) {
          return 0;
        }
        switch (def_pcode.getOpcode()) {
        case PcodeOp.PTRADD:
        case PcodeOp.PTRSUB: {
          if (!def_pcode.getInput(1).isConstant()) {
            printf("WARNING: PTR NO CONSTANT %s\n", def_pcode);
          }
          var = def_pcode.getInput(0);
          break;
        }
        case PcodeOp.CAST:
        case PcodeOp.COPY: {
          var = def_pcode.getInput(0);
          break;
        }
        case PcodeOp.INT_ADD:
        case PcodeOp.INT_SUB: {
          var = def_pcode.getInput(0);
          Varnode var_ = def_pcode.getInput(1);
          if (!var.isAddress() && var_.isAddress()) {
            var = def_pcode.getInput(1);
          }
          break;
        }
        default: {
          printf("Consider Indirect Load %s \n", pc_pcode(def_pcode));
          return 0;
        }
        }
      }
      Address data_addr = var.getAddress();
      long data_pos = data_addr.getOffset();
      long dif = (data_pos > pcode_addr) ? (data_pos - pcode_addr)
                                         : (pcode_addr - data_pos);
      // indirect data load in text section, probably mmio
      if (dif < 0x300) {
        String data_s = String.valueOf(getDataAt(data_addr).getValue());
        if (data_s.charAt(0) != '-') {
          Long data_l = Long.parseLong(data_s.substring(2), 16);
          return data_l;
        }
        return 0;
      } else {
        printf("too far\n");
        return 0;
      }
    }

    public boolean loop_detection(CodeBlock block, CodeBlockReference ref) {
      // printf("DEBUG: loop_detection target: %s\n", block);
      AddressSet addrs = new AddressSet();
      List<CodeBlockReference> refs = new ArrayList<>();
      CodeBlockReference ref_ = null;
      refs.add(ref);
      addrs = block.union(addrs);
      int i = 0;
      try {
        while (i < 4) {
          if (i >= refs.size()) {
            return false;
          }
          ref_ = refs.get(i);
          CodeBlock dest_ = ref_.getDestinationBlock();
          // printf("DEBUG: loop_detection target: %s\n", dest_);
          addrs = dest_.union(addrs);
          if (dest_ == block)
            break;

          CodeBlockReferenceIterator ref_iter =
              dest_.getDestinations(getMonitor());
          while (ref_iter.hasNext()) {
            CodeBlockReference ref__ = ref_iter.next();
            refs.add(ref__);
          }
          i++;
        }
        // not a cycle
        if (i == 4) {
          return false;
        }
        AddressIterator addr_it = addrs.getAddresses(true);
        while (addr_it.hasNext()) {
          Address addr_ = addr_it.next();
          Iterator<PcodeOpAST> pcode_ = hfunc.getPcodeOps(addr_);
          while (pcode_.hasNext()) {
            PcodeOp op = pcode_.next();
            if (op.getOpcode() == PcodeOp.STORE) {
              printf("ERROR: STORE " + pc_pcode(op) + "\n");
              return false;
            }
            if (op.getOpcode() == PcodeOp.CALL) {
              printf("ERROR: CALL " + pc_pcode(op) + "\n");
              return false;
            }
            if (op.getOpcode() == PcodeOp.MULTIEQUAL) {
              printf("ERROR: MULTIEQUAL" + pc_pcode(op) + "\n");
              return false;
            }
            if (op.getOpcode() == PcodeOp.INDIRECT) {
              printf("SEEN: INDIRECT " + pc_pcode(op) + "\n");
              continue;
            }
            Varnode out_var_ = op.getOutput();
            if (out_var_ != null && !out_var_.isConstant()) {
              Set<PcodeOp> pcodes =
                  DecompilerUtils.getForwardSliceToPCodeOps(out_var_);
              for (PcodeOp pcode : pcodes) {
                Address pcode_addr_ = pcode.getSeqnum().getTarget();
                if (!addrs.contains(pcode_addr_)) {
                  printf("ERROR: varnode is out of loop\n");
                  return false;
                }
              }
            }
          }
        }
      } catch (CancelledException e) {
        printf("getAddress when loop_detection %s\n", e.getMessage());
      }
      // choose block in constant loop chain
      CodeBlock block_i = null;
      while (true) {
        block_i = ref_.getSourceBlock();
        this.cfg.get_attribute(block_i).set_const_loop();
        if (block_i == block)
          break;
        for (CodeBlockReference ref_i : refs) {
          if (ref_i.getDestinationBlock() == block_i) {
            ref_ = ref_i;
          }
        }
      }
      return true;
    }

    /*
     * Creates a control flow graph for the input function
     */
    public FunctionControlFlowGraph(Function function) {
      SetupDecomp();
      this.hfunc = dec_hfunc(function);
      this.cfg = new CodeBlockDirectedGraph();
      this.function = function;
      BasicBlockModel basicBlockModel = new BasicBlockModel(currentProgram);
      AddressSetView addrSet = function.getBody();
      try {
        CodeBlockIterator codeBlockIter =
            basicBlockModel.getCodeBlocksContaining(addrSet, getMonitor());

        // go through each block and add the outgoing edges to the graph
        while (codeBlockIter.hasNext()) {
          CodeBlock block = codeBlockIter.next();
          // this is because some block no reference
          this.cfg.addEdge(block, null);
          printf("DEBUG: Analyse: %s\n", block);
          // check the indirect call
          AddressIterator addrs = block.getAddresses(true);
          while (addrs.hasNext()) {
            Address addr = addrs.next();
            Iterator<PcodeOpAST> pcode_ = this.hfunc.getPcodeOps(addr);
            while (pcode_.hasNext()) {
              PcodeOp op = pcode_.next();
              Address op_addr = op.getSeqnum().getTarget();
              switch (op.getOpcode()) {
              case PcodeOp.CALLIND: {
                printf("%s, %s\n", op.getSeqnum().getTarget(), op.toString());
                CodeBlockReferenceImpl edge = new CodeBlockReferenceImpl(
                    block, null, RefType.COMPUTED_CALL, null,
                    op.getSeqnum().getTarget());
                this.cfg.AddIndCall(block, edge);
                break;
              }
              case PcodeOp.STORE: {
                // long mem_addr = mem_access(op);
                // if (mem_addr == 0)
                // this.cfg.AddIndStore(block, op_addr, op);
                this.cfg.AddIndStore(block, op_addr, op);
                break;
              }
              case PcodeOp.LOAD: {
                // long mem_addr = mem_access(op);
                // if (mem_addr == 0) {
                // this.cfg.AddIndLoad(block, op_addr, op);
                // } else if (mem_addr >= 0x40000000 && mem_addr <= 0x60000000)
                // { printf("MMIO: %x, %s, %s\n", mem_addr,
                // op.getSeqnum().getTarget(), op.toString());
                // this.cfg.AddUserIn(op_addr);
                // this.has_user_in = true;
                // }
                this.cfg.AddIndLoad(block, op_addr, op);
                break;
              }
              }
            }
          }

          CodeBlockReferenceIterator ref_iter =
              block.getDestinations(getMonitor());

          // using the CodeBlockReference to add each edge
          while (ref_iter.hasNext()) {
            CodeBlockReference ref = ref_iter.next();
            // printf("DEBUG: ref %s\n" , ref);
            if (ref.getFlowType().isJump()) {
              long source = ref.getReferent().getOffset();
              long dest = ref.getReference().getOffset();
              if (dest < source && loop_detection(block, ref)) {
                printf("DEBUG: add const loop %s \n", block);
                this.cfg.AddConstLoop(block);
                continue;
              }
            }
            this.cfg.addEdge(block, ref);
          }
        }
      } catch (CancelledException e) {
        e.printStackTrace();
      }
    }

    public void printGraph() { this.cfg.printGraph(); }

    public List<CodeBlockReference> getCalls() { return this.cfg.getCalls(); }

    public Function getFunc() { return this.function; }

    public void AddUserIn(Address pc) {
      cfg.AddUserIn(pc);
      has_user_in = true;
    }

    public void FixIndirect(Address pc, Address target) {
      cfg.FixIndirect(pc, target);
    }

    public PathDecision InferSelect(Address pc) { return cfg.infer_select(pc); }

    public PathDecision LearnSelect(Address pc) { return cfg.learn_select(pc); }

    public boolean has_user() { return has_user_in; }
  }

  public void printGraph() {
    for (Map.Entry<Function, FunctionControlFlowGraph> entry :
         func_graphs.entrySet()) {
      printf("[*] Function Name: %s\n", entry.getKey());
      entry.getValue().printGraph();
    }
  }

  public void receive_load(Address pc) {
    Function func = getFunctionContaining(pc);
    if (func_graphs.containsKey(func)) {
      FunctionControlFlowGraph fcfg = func_graphs.get(func);
      fcfg.AddUserIn(pc);
    }
  }

  public void receive_indirect(Address pc, Address target) {
    Function func = getFunctionContaining(pc);
    if (func_graphs.containsKey(func)) {
      FunctionControlFlowGraph fcfg = func_graphs.get(func);
      fcfg.FixIndirect(pc, target);
    }
  }

  // Entry of the whole logic
  public String choose_path(Address pc) {
    CodeBlock block = get_block(pc);
    Function func = getFunctionContaining(pc);
    // this the initial point to decompile and connect the control flow graph
    if (!func_graphs.containsKey(func)) {
      GenGraph(func);
    }

    PathDecision result = PathDecision.NOT;
    if (mode == Mode.LEARN) {
      result = func_graphs.get(func).LearnSelect(pc);
    } else {
      result = func_graphs.get(func).InferSelect(pc);
    }

    switch (result) {
    case CONST: {
      return "CONST";
    }
    case JUMP: {
      return "JUMP";
    }
    case MAYBE: {
      return "MAYBE";
    }
    case NOT: {
      return "NOT";
    }
    default: {
      return "NOT";
    }
    }
  }

  public void GenGraph(Function in_func) {
    Stack<Function> workList = new Stack<>();
    Set<Function> analyzedFunc = new HashSet<>();
    workList.push(in_func);
    while (!workList.isEmpty()) {
      Function func = workList.pop();
      if (!analyzedFunc.contains(func)) {
        analyzedFunc.add(func);
        FunctionControlFlowGraph fcfg = new FunctionControlFlowGraph(func);
        List<CodeBlockReference> calls = fcfg.getCalls();
        func_edges.put(func, calls);
        func_graphs.put(func, fcfg);
        Set<Function> funcs = new HashSet<>();
        // deduplication
        for (CodeBlockReference call : calls) {
          funcs.add(getFunctionContaining(call.getReference()));
        }
        for (Function work_func : funcs) {
          workList.push(work_func);
        }
      }
    }
  }

  public void add_indirect(Address pc, Address target) {
    Function func = getFunctionContaining(pc);
    if (!func_graphs.containsKey(func)) {
      GenGraph(func);
    }
    FunctionControlFlowGraph fcfg = func_graphs.get(func);
    fcfg.FixIndirect(pc, target);
  }

  // iterative because need to maintain the addrs.
  // TODO: detect cycle
  private AddressSet loop_detection(CodeBlock start, CodeBlock block,
                                    AddressSet addrs, int depth) {
    if (depth == 4) {
      return null;
    }
    if (block == start) {
      return addrs;
    }
    // init
    if (block == null) {
      block = start;
    }
    try {
      CodeBlockReferenceIterator refs = block.getDestinations(getMonitor());
      addrs = addrs.union(block);
      while (refs.hasNext()) {
        CodeBlockReference ref = refs.next();
        // skip not jump edge
        if (!ref.getFlowType().isJump()) {
          continue;
        }
        AddressSet res =
            loop_detection(start, ref.getDestinationBlock(), addrs, depth + 1);
        if (res != null) {
          return res;
        }
      }
    } catch (CancelledException e) {
      printf("Exception: getDestinations when loop_detection %s\n",
             e.getMessage());
    }
    return null;
  }

  public String fake_read(Address pc) {
    FunctionControlFlowGraph temp_ = new FunctionControlFlowGraph();
    temp_.SetupDecomp();
    HighFunction hfunc = temp_.dec_hfunc(getFunctionContaining(pc));
    Iterator<PcodeOpAST> pcode_ = hfunc.getPcodeOps(pc);
    boolean has_load = false;
    while (pcode_.hasNext()) {
      PcodeOp op = pcode_.next();
      // if no load, may be optimized
      if (op.getOpcode() != PcodeOp.LOAD) {
        continue;
      }
      has_load = true;
      Varnode out_var_ = op.getOutput();
      if (out_var_ != null && !out_var_.isConstant()) {
        Set<PcodeOp> pcodes =
            DecompilerUtils.getForwardSliceToPCodeOps(out_var_);
        if (pcodes.size() > 0) {
          return "NOT";
        }
      }
    }
    if (has_load == false) {
      String pc_str = String.format("%x", pc.getOffset());
      printf("[fake_read]: has no load " + pc_str + " ,may be optimized\n");
    }
    return "FAKE";
  }

  public String check_loop(Address pc) {
    CodeBlock block = get_block(pc);
    // just test before rebase the whole file
    FunctionControlFlowGraph temp_ = new FunctionControlFlowGraph();
    temp_.SetupDecomp();
    HighFunction hfunc = temp_.dec_hfunc(getFunctionContaining(pc));
    AddressSet addrs = new AddressSet();

    // check loop chain
    // TODO: return multiple AddressSet because there may be multiple block
    // chain
    addrs = loop_detection(block, null, addrs, 0);
    if (addrs == null) {
      return "NOT";
    }

    // get the branch target which is jump outs of the loop
    String out_addr_ = "";
    try {
      CodeBlockReferenceIterator refs = block.getDestinations(getMonitor());
      while (refs.hasNext()) {
        CodeBlockReference ref = refs.next();
        Address dest_ = ref.getDestinationAddress();
        if (!addrs.contains(dest_)) {
          out_addr_ = String.format("%x", dest_.getOffset());
          printf("JUMP OUT OF LOOP" + out_addr_ + "\n");
        }
      }
    } catch (CancelledException e) {
      printf("Exception: getDestinations when loop_detection %s\n",
             e.getMessage());
    }
    // check loop constant
    AddressIterator addr_it = addrs.getAddresses(true);
    while (addr_it.hasNext()) {
      Address addr_ = addr_it.next();
      Iterator<PcodeOpAST> pcode_ = hfunc.getPcodeOps(addr_);
      while (pcode_.hasNext()) {
        PcodeOp op = pcode_.next();
        if (op.getOpcode() == PcodeOp.STORE) {
          printf("ERROR: STORE " + pc_pcode(op) + "\n");
          return "NOT";
        }
        if (op.getOpcode() == PcodeOp.CALL) {
          printf("ERROR: CALL " + pc_pcode(op) + "\n");
          return "NOT";
        }
        if (op.getOpcode() == PcodeOp.MULTIEQUAL) {
          printf("ERROR: MULTIEQUAL" + pc_pcode(op) + "\n");
          // return "NOT";
          // for 3322
          continue;
        }
        if (op.getOpcode() == PcodeOp.INDIRECT) {
          printf("SEEN: INDIRECT " + pc_pcode(op) + "\n");
          continue;
        }
        Varnode out_var_ = op.getOutput();
        if (out_var_ != null && !out_var_.isConstant()) {
          Set<PcodeOp> pcodes =
              DecompilerUtils.getForwardSliceToPCodeOps(out_var_);
          for (PcodeOp pcode : pcodes) {
            Address pcode_addr_ = pcode.getSeqnum().getTarget();
            if (!addrs.contains(pcode_addr_)) {
              printf("ERROR: varnode is out of loop\n");
              return "NOT";
            }
          }
        }
      }
    }
    return "CONST " + out_addr_;
  }

  // Packet is what information exchange between ghidra and S2E
  // [type][pc][target]
  // [1][ 10 ][ 10 ]
  // "10xpcpcpcpc0xtargetin"
  // type: 0 means let ghidra to select the path, pc is current program counter
  // type: 1 means add information to ghidra, such as indirect load and call,
  // target means the real load address or call address
  // type: 2 means switch the mode between LEARN to INFER
  public String process(String packet) {
    if (packet.length() == 0) {
      printf("ERROR: null packet\n");
      return null;
    }
    char type_c = packet.charAt(0);
    int type = Integer.parseInt(String.valueOf(type_c));
    switch (type) {
    case 0: {
      Address pc = toAddr(Integer.parseInt(packet.substring(3), 16));
      return check_loop(pc);
      // return choose_path(pc);
    }
	case 5: {
		Address pc = toAddr(Integer.parseInt(packet.substring(3), 16));
		return getInstructionAt(pc).getMnemonicString().toString();
	}
    case 4: {
      Address pc = toAddr(Integer.parseInt(packet.substring(3), 16));
      return fake_read(pc);
    }
    case 1: {
      Address pc = toAddr(Integer.parseInt(packet.substring(3, 11), 16));
      if (packet.length() == 11) { // receive user addr load
                                   // receive_load(pc);
        String pc_str = String.format("%x", pc.getOffset());
        printf("receive load " + pc_str + "\n");
      } else if (packet.length() == 21) {
        Address target = toAddr(Integer.parseInt(packet.substring(13), 16));
        // add_indirect(pc, target);
        String pc_str = String.format("%x", pc.getOffset());
        String target_str = String.format("%x", target.getOffset());
        printf("add indirect pc " + pc_str + " target " + target_str + "\n");
      } else {
        printf("ERROR: not correct packet size %s\n", packet);
      }
      break;
    }
    case 2: {
      if (packet.length() > 1) {
        printf("ERROR: not correct packet %s\n", packet);
      } else if (mode == Mode.LEARN) {
        printf("DEBUG: switch mode to INFER\n");
        mode = Mode.INFER;
      }
      break;
    }
    case 3: {
      if (packet.length() > 1) {
        printf("ERROR: not correct packet %s\n", packet);
      } else if (mode == Mode.INFER) {
        printf("DEBUG: switch mode to LERAN\n");
        mode = Mode.LEARN;
      }
      break;
    }
    default: {
      printf("ERROR: Not Supported type value %d\n", type_c);
      break;
    }
    }
    return null;
  }

  public void socket_server(int port) {
    try {
      server = new ServerSocket(port);
      printf("Socket Server started on port %d\n", port);
      while (true) {
        Socket client = server.accept();
        printf("Client Connected: %s\n",
               client.getInetAddress().getHostAddress());
        // update the mode
        mode = Mode.INFER;
        // get the input stream and the output stream
        BufferedReader reader =
            new BufferedReader(new InputStreamReader(client.getInputStream()));
        OutputStream outstream = client.getOutputStream();
        byte[] buf = new byte[100];
        int len;
        String packet = "new";

        while (true) {
          // get the message from client
          packet = reader.readLine();

          if (packet == null) {
            printf("Client disconnected, wait for another client\n");
            break;
          }

          printf("Received data from client: %s\n", packet);
          if (packet.equals("close")) {
            printf("Close Socket\n");
            server.close();
            return;
          }
          String response = process(packet);
          if (response != null) {
            printf("Response: %s\n", response);
            outstream.write(response.getBytes());
          }
        }
      }
    } catch (IOException e) {
      printf("ERRROR in socketserver: %s\n", e.getMessage());
      e.printStackTrace();
    }
  }

  public void run() throws Exception {
    // FIXME: change address in this line to the start address of the function
    // you want to analyze.
    mode = Mode.INFER;
    basicBlockModel = new BasicBlockModel(currentProgram);
    func_edges = new HashMap<>();
    func_graphs = new HashMap<>();
    socket_server(22733);
    // Address addr = currentAddress;
    // GenGraph(getFunctionContaining(addr));
    // printGraph();
  }
}
