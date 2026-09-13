#!/usr/bin/python3
import re
import serial
import binascii,time


# test
lines = []
lines.append('81 04 81 01 81 81 81 01\n')
#lines.append('81\n')
# test end 

f_out = open("out.log","w",encoding='utf-8')


ser = serial.Serial("/dev/ttyACM0", 19200)    # 打开COM17，将波特率配置为115200，其余参数使用默认值
if ser.isOpen():                        # 判断串口是否成功打开
    print("打开串口成功。")
    print(ser.name)    # 输出串口号
else:
    print("打开串口失败。")

total_line = len(lines)
prev = 0
count = 0
for (idx,line) in enumerate(lines):
    last_ = (total_line - idx)
    print(f"last {last_} In: {line}")
    f_out.write("In: " + line)
    d=bytes.fromhex(line)
    print(d)
    ser.write(d)

    time.sleep(0.5)
    prev = 0
    while True:
        prev = count 
        count = ser.inWaiting()
        if count > 0 and prev == count:
            break
        time.sleep(1)

    if count>0:
        data=ser.read(count)
        data = data.decode('unicode_escape')
        data = data.replace('\n', "\\n")
        data = data.replace('\r', '')
        datalist = data.split('\\n')
        for i in datalist:
            print(i)
            f_out.write(i+"\n")
    
# log the error 
print("sleep::::")
time.sleep(10)
count = ser.inWaiting()
if count > 0:
    data=ser.read(count)
    data = data.decode('unicode_escape')
    data = data.replace('\n', "\\n")
    data = data.replace('\r', '')
    datalist = data.split('\\n')
    for i in datalist:
        print(i)
        f_out.write(i+"\n")
f_out.close()
