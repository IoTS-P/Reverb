in_24821 = """14 41 d8 e3 23 00 ff ff 42 7a a6 20 e1 fe 8d ae e6 40 40"""

# the hardware generated rssi and crc 
FCS = "ee ea"

ins = []
for i in in_24821.split():
    ins.append(int(i, 16))

for i in FCS.split():
    ins.append(int(i, 16))

with open("./24821.in", "wb") as file:
    file.write(bytes(ins))
