
VERSION := 1
DIRS 	:= src # Use DIRS for adding dirs to path
INCS 	:= inc # Use INCS to for include dirs

include Arduino.mk

OBJS 	:= arduinos.o heap.o
OBJS 	:= $(patsubst %, $(OBJDIR)/%, $(OBJS))


# Add a library target
$(OBJDIR)/libarduinos-$(VERSION).a: $(OBJS)
	@echo AR $(notdir $@) $(notdir $^)
	@$(AR) rcs $@ $^

# Add a firmware target
#$(OBJDIR)/firmware.elf: $(OBJDIR)/libarduinomain.a $(OBJDIR)/libarduinos-$(VERSION).a $(OBJDIR)/libarduinocore.a $(OBJDIR)/libavrc.a
#	$(CC) $(CFLAGS_$(PLATFORM)) $(LDFLAGS) -o $@ $^ -lm

# Define the target rule
build-$(PLATFORM): $(OBJDIR)/libarduinos-$(VERSION).a
	@true

