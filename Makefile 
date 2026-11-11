ifeq ($(PS3DEV),)
$(error PS3DEV environment variable is not set)
endif

include $(PS3DEV)/ppu/include/Makefile.pkg

TARGET		:= cobra.elf
BUILD		:= build
SFO_XML		:= sfo.xml
TITLE_ID	:= COBR00001

OBJS		:= main.o

CFLAGS		:= -O3 -Wall -Wextra -ffunction-sections -fdata-sections
CXXFLAGS	:= $(CFLAGS)
ASFLAGS		:= 
LDFLAGS		:= -Wl,--gc-sections
LIBS		:= -lrsx -lgcm_sys -lio -lsysutil -lrt -lm

all: $(TARGET).pkg

$(TARGET).pkg: $(TARGET).self $(SFO_XML)
	pkg --contentid UP0001-$(TITLE_ID)_00-0000000000000000 $(SFO_XML) $(TARGET).self $@

$(TARGET).self: $(TARGET)
	ppu-lv2-objcopy -O binary $< $@.bin
	# Sixtype / Self utility will generate the self
	scetool -v --sce-type=SELF --compress-data=TRUE --encrypt $(TARGET).bin $@

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(TARGET).self $(TARGET).bin $(TARGET).pkg $(OBJS)
