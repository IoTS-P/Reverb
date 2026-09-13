# how to use this script
# use continue to wait for firmware pass the init stage
import gdb
import os
import serial

path_ = os.path.dirname(os.path.abspath(__file__))
log_file = open(f'{path_}/step.log', 'w')
gdb.execute('set pagination off')
gdb.execute(f'set logging file {path_}/gdb.log')
gdb.execute('set logging enabled on')

# global variable
START_POINT = "loop"
CRASH_POINT = 0x8000758
start = False
symbols = {}
# switch of input because interrupt will return to same pc
should_input = True
read_pc = 0x8000b40
poc = [0x1, 0x1, 0x25, 0x25, 0xb, 0xa0, 0xaf, 0xa0, 0xaf, 0xa0, 0xa0, 0xa0, 
       0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xaf, 0x31]

poc_ptr = 0
block_num = 0

def info(log_):
    log_file.write(log_ + '\n')
    print(log_)
ser = serial.Serial('/dev/ttyACM0', 19200)

if ser.isOpen():                        # 判断串口是否成功打开
    info("打开串口成功。")
    info(ser.name)    # 输出串口号
else:
    info("打开串口失败。")

# read symbols
with open('./symbol', 'r') as file:
    symbs = file.readlines()

for line in symbs:
    if len(line) > 9:
        lines = line.split()
        if len(lines) < 3 or lines[2] != 'F':
            continue
        pc = int(lines[0], base=16)
        if 0x20000000 > pc > 0x100:
            symb = lines[-1]
            symbols[pc] = symb

def get_register(regname):
	"""Return a register's value."""
	regname = regname.strip()
	try:
		value = gdb.parse_and_eval(regname)
		return int(value) 
        # if value.type.code == gdb.TYPE_CODE_INT else long(value)
	except gdb.error:
		value = gdb.selected_frame().read_register(regname)
		return int(value)

def input_handler(pc):
    global poc_ptr
    global should_input
    if should_input:
        in_ = bytes(poc[poc_ptr:poc_ptr+1])
        poc_ptr += 1
        ser.write(in_) 
        info(f'input value {in_[0]:#x} at {pc:#x}')
        # continue to USART3_IRQHandler
        gdb.execute('continue')
    else:
        info(f'interrupt return')
    should_input = not bool(should_input)

def stop_handler(event):
    '''Stop Event Handler'''
    global start
    global block_num
    start = True
    pc = get_register('$pc')
    # xpsr = get_register('$xPSR')
    if pc == read_pc:
        input_handler(pc)
    elif pc == 0x8000c36:
        info("HardFault")
        exit(0)
    elif pc == 0x8002f88:
        dr_ = get_register('$r2')
        info(f'recv dr {hex(dr_)}')
    elif pc == 0x8002ecc:
        info('trigger transmit irq handler')
        gdb.execute('continue')
    elif pc == CRASH_POINT: # root cause loop 
        input_handler(pc)
    if (func_ := symbols.get(pc)) is not None:
        info(f"{pc:#x}: {func_}")
    else:
        info(hex(pc))

def exit_handler(event):
    '''Exit Event Handler'''
    if isinstance(event, gdb.events.ExitedEvent):
        info("eventy type: exit")
    log_file.close()


if isinstance(START_POINT, int):
    gdb.execute(f'hb *{hex(START_POINT)}')
elif isinstance(START_POINT, str):
    gdb.execute(f'hb {START_POINT}')
else:
    print("error: not supported START_POINT")
    exit(0)

# hook HARDFAULT
gdb.execute(f'hb HardFault_Handler')
gdb.execute(f'hb USART3_IRQHandler')
gdb.events.stop.connect(stop_handler)
gdb.events.exited.connect(exit_handler)
gdb.execute('continue')

while True:
    if start:
        gdb.execute('si')
