reg_str = """
dma            0x3fffffff
r0             0x20000120          536871200
r1             0x20000004          536870916
r2             0x10                16
r3             0x8004335           134234933
r4             0x0                 0
r5             0x0                 0
r6             0x0                 0
r7             0x0                 0
r8             0x0                 0
r9             0x0                 0
r10            0x0                 0
r11            0x0                 0
r12            0x7a00              31232
sp             0x2002fff0          0x2002fff0
lr             0x8000c6d           134220909
pc             0x8000b40           0x8000b40 <Modbus::poll(unsigned short*, unsigned char)>
msp            0x2002fff0          0x2002fff0
psp            0x0                 0x0
control        0x0                 0 '\000'
NVIC_ISER1     0x0
NVIC_ISER2     0x80
NVIC_ISER3     0x0
NVIC_ISER4     0x0
NVIC_ISER5     0x0
NVIC_ISER6     0x0
NVIC_ISER7     0x0
NVIC_ISER8     0x0
"""

reg_bytes = []

for l_ in reg_str.split("\n"):
    s_ = l_.split()
    if len(s_) > 1:
        reg_val_ = int(s_[1][2:], base=16).to_bytes(4, "big")
        reg_bytes.append(reg_val_)
        print(f"write reg: {s_[0]}")

with open("./plc.mem", "rb") as file:
    mem_ = file.read()

with open("plc.snapshot", "wb") as file:
    for regb_ in reg_bytes:
        file.write(regb_)
    print("Write Memory Snapshot")
    file.write(mem_)
