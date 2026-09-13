# how to use this script
# use continue to wait for firmware pass the init stage
import gdb
import os

path_ = os.path.dirname(os.path.abspath(__file__))
log_file = open(f"{path_}/step.log", "w")
gdb.execute("set pagination off")
gdb.execute(f"set logging file {path_}/gdb.log")
gdb.execute("set logging enabled on")

# global config
# cc2538 irq handler can be trigger multiple times for read, write etc. this is the start of the read part in the irq handler
START_POINT = 0x201df6
mem_manage_pc = 0x202b4c
hardfault_pc = 0x202af8

# global variable
start = False
# switch of input because interrupt will return to same pc
nvic_pendsv = {
    0x2016ce: "isr_rfcorerxtx",
    0x2020dc: "_msg_receive",
    0x2021d6: "_msg_send",
    0x20236a: "mutex_lock",
    0x20250e: "sched_switch",
    0x2025a6: "thread_yield",
    0x20272e: "_thread_flags_wait_any",
    0x2027e2: "thread_flags_set",
    0x202a94: "cpu_switch_context_exit",
    0x202ade: "isr_svc",
    0x20375c: "evtimer_add",
    0x20c94a: "isr_sleepmode",
    0x20ca18: "irq_handler",
    0x20cef0: "isr_uart0",
    0x20d90c: "ztimer_handler",
}

pendsv_breakpoint = False


def info(log_):
    log_file.write(log_ + "\n")
    print(log_)


def get_register(regname):
    """Return a register's value."""
    regname = regname.strip()
    try:
        value = gdb.parse_and_eval(regname)
        return int(value)  # if value.type.code == gdb.TYPE_CODE_INT else long(value)
    except gdb.error:
        value = gdb.selected_frame().read_register(regname)
        return int(value)


def stop_handler(event):
    """Stop Event Handler"""
    global start
    global pendsv_breakpoint
    start = True
    pc = get_register("$pc")
    info(hex(pc))
    if pc == START_POINT:
        info("start record")
        start = True
    if (name_ := nvic_pendsv.get(pc)) is not None:
        info(f"trigger pendsv from {name_}")
        if not pendsv_breakpoint:
            pendsv_breakpoint = True
            gdb.execute("hb isr_pendsv")
        gdb.execute("continue")
    if pc == mem_manage_pc:
        info("in mem_manage_fault")
        exit(0)
    elif pc == hardfault_pc:
        info("in hard_fault_default")
    return

def exit_handler(event):
    """Exit Event Handler"""
    if isinstance(event, gdb.events.ExitedEvent):
        info("eventy type: exit")
    log_file.close()


if isinstance(START_POINT, int):
    gdb.execute(f"hb *{hex(START_POINT)}")
elif isinstance(START_POINT, str):
    gdb.execute(f"hb {START_POINT}")
else:
    print("error: not supported START_POINT")
    exit(0)

# hook HARDFAULT
gdb.execute("hb hard_fault_default")
gdb.execute("hb mem_manage_default")
gdb.events.stop.connect(stop_handler)
gdb.events.exited.connect(exit_handler)
gdb.execute("continue")

while True:
    if start:
        gdb.execute("si")
