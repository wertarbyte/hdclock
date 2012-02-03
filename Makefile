MCU = attiny2313
F_CPU = 16000000
TARGET = hdclock
SRC = hdclock.c USI_TWI_Master.c

include avr-tmpl.mk

