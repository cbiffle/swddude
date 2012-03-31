AS=arm-none-eabi-as
LD=arm-none-eabi-ld
OBJCOPY=arm-none-eabi-objcopy

test.bin: test.elf
	$(OBJCOPY) -I elf32-littlearm -O binary $^ $@

test.elf: test.o
	$(LD) -T lpc1343.ld -o $@ $^

test.o: test.s
	$(AS) -o test.o test.s

clean:
	-rm test.bin
	-rm test.elf
	-rm test.o
