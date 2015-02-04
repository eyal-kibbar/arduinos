

CC	:= avr-g++
AR	:= avr-ar
OBJCP	:= avr-objcopy
CFLAGS	:= -ffunction-sections -fdata-sections -Os -fpermissive -fno-exceptions
LDFLAGS	:= -Wl,-Os -Wl,--gc-sections
OBJDIR	:= obj/$(PLATFORM)
BAUD	:= 9600
# Platform flags
CFLAGS_uno	:= -mmcu=atmega328p
DEFINES_uno	:= F_CPU=16000000L
DEVICE_uno	:= /dev/ttyACM0
AVRNAME_uno	:= ATMEGA328P

CFLAGS	:= $(CFLAGS) $(CFLAGS_$(PLATFORM))
DEFINES	:= $(DEFINES) $(DEFINES_$(PLATFORM))

all: uno

# include arduino sdk
ARDUINO_SDK_DIR := /usr/share/arduino
AVRDUDE_FLAGS := -C $(ARDUINO_SDK_DIR)/hardware/tools/avrdude.conf -F -V -p $(AVRNAME_$(PLATFORM)) -b $(BAUD) -P $(DEVICE_$(PLATFORM))

INCS += $(ARDUINO_SDK_DIR)/hardware/arduino/variants/standard
DEFINES += ARDUINO=106


# Arduino core library
###############################################################################
DIR_ARDUINOCORE := $(ARDUINO_SDK_DIR)/hardware/arduino/cores/arduino
OBJS_ARDUINOCORE := \
	CDC.o			\
	HardwareSerial.o	\
	HID.o			\
	IPAddress.o		\
	new.o			\
	Print.o			\
	Stream.o		\
	Tone.o			\
	USBCore.o		\
	WMath.o			\
	WString.o		\
	WInterrupts.o		\
	wiring_analog.o		\
	wiring.o		\
	wiring_digital.o	\
	wiring_pulse.o		\
	wiring_shift.o

OBJS_ARDUINOCORE := $(patsubst %.o, $(OBJDIR)/%.o, $(OBJS_ARDUINOCORE))
DIRS += $(DIR_ARDUINOCORE)
INCS += $(DIR_ARDUINOCORE)

$(OBJDIR)/libarduinocore.a: $(OBJS_ARDUINOCORE)
	@echo AR $(notdir $@) $(notdir $^)
	@$(AR) rcs $@ $^

# avr libc
###############################################################################
DIR_AVRLIBC := $(ARDUINO_SDK_DIR)/hardware/arduino/cores/arduino/avr-libc
OBJS_AVRLIBC := \
	malloc.o \
	realloc.o

OBJS_AVRLIBC := $(patsubst %.o, $(OBJDIR)/%.o, $(OBJS_AVRLIBC))
DIRS += $(DIR_AVRLIBC)
INCS += $(DIR_AVRLIBC)

$(OBJDIR)/libavrc.a: $(OBJS_AVRLIBC)
	@echo AR $(notdir $@) $(notdir $^)
	@$(AR) rcs $@ $^


# lib arduino main
###############################################################################

DIR_ARDUINOMAIN := $(ARDUINO_SDK_DIR)/hardware/arduino/cores/arduino/avr-libc
OBJS_ARDUINOMAIN := \
	main.o

OBJS_ARDUINOMAIN := $(patsubst %.o, $(OBJDIR)/%.o, $(OBJS_ARDUINOMAIN))
DIRS += $(DIR_ARDUINOMAIN)
INCS += $(DIR_ARDUINOMAIN)

$(OBJDIR)/libarduinomain.a: $(OBJS_ARDUINOMAIN)
	@echo AR $(notdir $@) $(notdir $^)
	@$(AR) rcs $@ $^

INCS := $(patsubst %, -I%, $(INCS))
LIBS := $(patsubst %, -l%, $(LIBS))
DEFINES := $(patsubst %, -D%, $(DEFINES))

vpath %.cpp $(DIRS)
vpath %.c $(DIRS)
vpath %.o $(OBJDIR)

$(OBJDIR)/%.o: %.cpp
	@echo CC $(notdir $<)
	@$(CC) $(CFLAGS) $(INCS) $(DEFINES) $(LIBS) -c $< -o $@

$(OBJDIR)/%.o: %.c
	@echo CC $(notdir $<)
	@$(CC) $(CFLAGS) $(INCS) $(DEFINES) $(LIBS) -c $< -o $@


$(OBJDIR)/firmware.hex : $(OBJDIR)/firmware.elf
	$(OBJCP) -O ihex -R .eeprom $^ $@

upload: $(OBJDIR)/firmware.hex
	$(AVRDUDE) $(AVRDUDE_FLAGS) -U flash:w:$<

uno:
	@echo "building $@"
	@mkdir -p $(OBJDIR)/$@
	@$(MAKE) PLATFORM=$@ build-$@

clean:
	rm -rf $(OBJDIR)


