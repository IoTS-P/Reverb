with open('./reg.snapshot', 'r') as file:
    lines = file.readlines()

reg_bytes = []
for l_ in lines:
    s_ = l_.split()
    if len(s_) > 1:
        reg_val_ = int(s_[1][2:], base=16).to_bytes(4, "big")
        reg_bytes.append(reg_val_)
        print(f"write reg: {s_[0]}")

with open("./mqtt.mem", "rb") as file:
    mem_ = file.read()

with open("smart_light.snapshot", "wb") as file:
    for regb_ in reg_bytes:
        file.write(regb_)
    print("Write Memory Snapshot")
    file.write(mem_)
