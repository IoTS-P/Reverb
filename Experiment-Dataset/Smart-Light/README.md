elf需要转bin：arm-none-eabi-objcopy -O binary /home/s/boge/InVulAna-script/realworld/cc2538/smart_light/smart_light.elf /home/s/boge/InVulAna-script/realworld/cc2538/smart_light/smart_light.bin



先克隆下来riot。再按官网教程make，那个很容易连不上。用最新的cc2538_bsl仓库稳定些。
但debug可以直接用riot的debug。