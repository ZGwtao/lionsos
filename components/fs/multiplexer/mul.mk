
FS_MUL_SRC_DIR	:= $(realpath $(dir $(lastword $(MAKEFILE_LIST))))

FS_MUL_CFLAGS := \
	-I$(FS_MUL_SRC_DIR)/config

FS_MUL_OBJ	:= \
	multiplexer/mul.o
	
multiplexer:
	mkdir -p multiplexer

multiplexer/%.o: CFLAGS += $(FS_MUL_CFLAGS)
multiplexer/%.o: $(FS_MUL_SRC_DIR)/%.c $(FAT_LIBC_INCLUDE) | multiplexer
	$(CC) -c $(CFLAGS) $< -o $@

multiplexer.elf: $(FS_MUL_OBJ) $(FAT_LIBC_LIB)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

-include $(FS_MUL_OBJ:.o=.d)
