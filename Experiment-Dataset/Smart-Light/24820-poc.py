in_24820 = """1e 41 d8 e3 23 00 ff ff 42 7a a6 20 e1 fe 8d ae 7f b4 12 e3 ff 8b f3 f3 f3 f3 f3 10 15"""

# the hardware generated rssi and crc 
FCS = "ee ea"

ins = []
for i in in_24820.split():
    ins.append(int(i, 16))

for i in FCS.split():
    ins.append(int(i, 16))

with open("./24820.in", "wb") as file:
    file.write(bytes(ins))
