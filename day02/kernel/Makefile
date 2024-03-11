uniq = $(if $1,$(firstword $1) $(call uniq,$(filter-out $(firstword $1), $1)))

SOURCE_ROOT = .
BUILD_ROOT = ./build
TARGET = $(BUILD_ROOT)/kernel.elf
ENTRY_POINT = KernelEntryPoint

CCOMP   = clang
CXXCOMP = clang++
LINKER  = ld.lld

CSRCS   = $(shell find $(SOURCE_ROOT)/ -type f -name '*.c')
CXXSRCS = $(shell find $(SOURCE_ROOT)/ -type f -name '*.cpp')
ASMSRCS = $(shell find $(SOURCE_ROOT)/ -type f -name '*.asm')
FNTSRC  = $(SOURCE_ROOT)/hankaku.txt
COBJS   = $(patsubst $(SOURCE_ROOT)/%.c,$(BUILD_ROOT)/%.o,$(CSRCS))
CXXOBJS = $(patsubst $(SOURCE_ROOT)/%.cpp,$(BUILD_ROOT)/%.o,$(CXXSRCS))
ASMOBJS = $(patsubst $(SOURCE_ROOT)/%.asm,$(BUILD_ROOT)/%.o,$(ASMSRCS))
FNTOBJ  = $(BUILD_ROOT)/hankaku.o

SRC_DIRECTORIES = $(call uniq,$(dir $(sort $(CSRCS) $(CXXSRCS) $(ASMSRCS))))
BUILD_DIRECTORIES = $(patsubst $(SOURCE_ROOT)/%,$(BUILD_ROOT)/%,$(SRC_DIRECTORIES))

DEPENDS = $(join \
	$(dir $(CXXOBJS) $(COBJS)),\
	$(notdir $(CXXOBJS:.o=.d) $(COBJS:.o=.d))\
)

CPPFLAGS += -I$(SOURCE_ROOT)
CFLAGS   += -O3 -Wall -g --target=x86_64-elf -ffreestanding -mno-red-zone
CXXFLAGS += -O3 -Wall -g --target=x86_64-elf -ffreestanding -mno-red-zone \
            -fno-exceptions -fno-rtti -std=c++17
LDFLAGS  += --entry $(ENTRY_POINT) -z norelro --image-base 0x100000 --static

.PHONY: all
all: build_directory $(TARGET)

build_directory:
	@mkdir -p $(BUILD_DIRECTORIES)

# for debugging a makefile
.PHONY: print_vars
print_vars:
	@echo source_dirs: $(SRC_DIRECTORIES)
	@echo build_dirs: $(BUILD_DIRECTORIES)

.PHONY: clean
clean:
	find ./build -type f -name '*.o' -delete

# link
$(TARGET): $(CXXOBJS) $(COBJS) $(ASMOBJS) $(FNTOBJ) Makefile
	$(LINKER) $(LDFLAGS) -o $@ $(CXXOBJS) $(COBJS) $(ASMOBJS) $(FNTOBJ) -lc -lc++ -lc++abi -lm -lfreetype


# c/cpp files compilation(start)
$(BUILD_ROOT)/%.o: $(SOURCE_ROOT)/%.cpp Makefile
	$(CXXCOMP) $(CPPFLAGS) $(CXXFLAGS) -o $@ -c $<

$(BUILD_ROOT)/%.d: $(SOURCE_ROOT)/%.cpp
	$(CXXCOMP) $(CPPFLAGS) $(CXXFLAGS) -MM $< > $@
	$(eval OBJ = $(<:.cpp=.o))
	sed --in-place 's|$(notdir $(OBJ))|$(OBJ)|' $@

$(BUILD_ROOT)/%.o: $(SOURCE_ROOT)/%.c Makefile
	$(CCOMP) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

$(BUILD_ROOT)/%.d: $(SOURCE_ROOT)/%.c
	$(CCOMP) $(CPPFLAGS) $(CFLAGS) -MM $< > $@
	$(eval OBJ = $(<:.c=.o))
	sed --in-place 's|$(notdir $(OBJ))|$(OBJ)|' $@
# c/cpp files compilation(end)


# asm files compilation
$(BUILD_ROOT)/%.o: $(SOURCE_ROOT)/%.asm Makefile
	nasm -f elf64 -o $@ $<


# font file generation(start)
$(FNTOBJ): $(BUILD_ROOT)/hankaku.bin
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

$(BUILD_ROOT)/hankaku.bin: $(FNTSRC)
	../tools/makefont.py -o $@ $<

$(BUILD_ROOT)/%.d: %.bin
	touch $@
# font file generation(end)


.PHONY: depends
depends:
	$(MAKE) $(DEPENDS)

-include $(DEPENDS)