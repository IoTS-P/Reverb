reg_str = """
dma            0x3fffffff
r0             0x0                 0
r1             0x0                 0
r2             0x1                 1
r3             0x1                 1
r4             0x200708e8          537331944
r5             0x20070964          537332068
r6             0x20070a00          537332224
r7             0x2007098c          537332108
r8             0x2007093c          537332028
r9             0x20070940          537332032
r10            0x20070938          537332024
r11            0x8db1028           148574248
r12            0x0                 0
sp             0x20087fb8          0x20087fb8
lr             0x80af7             527095
pc             0x8064e             0x8064e <loop()+110>
msp            0x20087fb8          0x20087fb8
psp            0x835b331c          0x835b331c
control        0x0                 0
NVIC_ISER1     0x100
NVIC_ISER2     0x0
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

with open("./cve.mem", "rb") as file:
    mem_ = file.read()

with open("heat_press.snapshot", "wb") as file:
    for regb_ in reg_bytes:
        file.write(regb_)
    print("Write Memory Snapshot")
    file.write(mem_)
