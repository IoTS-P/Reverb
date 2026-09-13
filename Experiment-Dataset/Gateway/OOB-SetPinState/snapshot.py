reg_str = """
dma            0x3fffffff
r0             0x1                 1
r1             0xff                255
r2             0x2000016d          536871277
r3             0x1                 1
r4             0x0                 0
r5             0x0                 0
r6             0x0                 0
r7             0x0                 0
r8             0x0                 0
r9             0x0                 0
r10            0x0                 0
r11            0x0                 0
r12            0x0                 0
sp             0x20013fe8          0x20013fe8
lr             0x8002ad1           134228689
pc             0x8002ad0           0x8002ad0 <loop()+52>
msp            0x20013fe8          0x20013fe8
psp            0x0                 0x0
control        0x0                 0 '\000'
NVIC_ISER1     0x0
NVIC_ISER2     0x40
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

with open("./gateway.mem", "rb") as file:
    mem_ = file.read()

with open("gateway.snapshot", "wb") as file:
    for regb_ in reg_bytes:
        file.write(regb_)
    print("Write Memory Snapshot")
    file.write(mem_)
