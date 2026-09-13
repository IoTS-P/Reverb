in_24818 = """24 41 18 e1 23 00 ff ff e8 00 49 00 00 00 6b 6b b4 6b 64 00 6e 6e e8 00 00 00 00 6b 6b b4 6b 64 00 6e 6e"""

# the hardware generated rssi and crc 
FCS = "ee ea"

ins = []
for i in in_24818.split():
    ins.append(int(i, 16))

for i in FCS.split():
    ins.append(int(i, 16))

with open("./24818.in", "wb") as file:
    file.write(bytes(ins))
