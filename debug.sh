/usr/local/gcc-arm-none-eabi-9-2020-q2-update/bin/arm-none-eabi-gdb build/nrf52840_xxaa.out -ex "target remote localhost:2331" -ex "break main" -ex "mon reset 0"
