# Additional linux build rules

dirs-y += src/linux_fuzzing src/linux src/generic

src-y += linux_fuzzing/harness.c
src-y += linux/timer.c linux/console.c linux/watchdog.c
src-y += linux/pca9685.c linux/spidev.c linux/analog.c linux/hard_pwm.c
src-y += linux/i2c.c linux/gpio.c generic/crc16_ccitt.c generic/alloc.c
src-y += linux/sensor_ds18b20.c

CFLAGS += -fsanitize=address -fno-lto -DFUZZING=1
LDFLAGS += -fsanitize=fuzzer
CFLAGS_klipper.elf += -lutil -lrt -lpthread


CC=afl-clang-fast

docker:
	cd test/fuzzing && sudo docker build . -t klipper-fuzz