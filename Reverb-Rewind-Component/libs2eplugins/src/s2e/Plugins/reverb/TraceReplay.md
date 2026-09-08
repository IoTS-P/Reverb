# input Vul Analysis


# Snapshot Image Format

| Name        | Len(byte) |
| ----------- | --------- |
| original DMA| 4         |
| regular reg | 64        |
| MSP         | 4         |
| PSP         | 4         |
| Control     | 4         |
| NVIC->ISER  | 16        |
| SRAM        | x         |

## TODO

1. constraint chain
2. 
## Questions

1. concolic = concrete + symbolic execution
2. 怎么把一个 ref<Expr> 拆成一个 vector => read 
3. 好像可以一个一个读，基本单位是**bytes**么 -> s2e 还要具体看一下，他 symbread 怎么是 create 啊
4. 如果bytes的话，那如果**拆开**怎么办啊，如果不是，那创建一堆size==1 会不会开销很大啊 - 可以看看compare
5. 符号执行会不会变化原有的路径啊，感觉应该属于污点分析
6. onSymbRead 传入的 value 的参数是什么， 怎么判断是不是初始化的时候
7. read 和 write hook 的位置 FunctionHandlers.cpp 的 handlerTraceMmioAccess 并注入到 llvm 中

