INCLUDE_FLAGS := -I. 
CPPFLAGS := -ggdb3 $(INCLUDE_FLAGS) -Wall -std=gnu++2a  -lfuse3
CC := g++

.PHONY: run clean all install
.SUFFIXES: .o .cpp .asm


OBJS =\
src/buffer_allocator.o	\
src/page_allocator.o	\
src/BTree.o	\
src/file_system.o	\
src/main.o \

all: cow

%.o : %.cpp
	$(CC) -c $(CPPFLAGS) $< -o $@

cow: $(OBJS)
	$(CC) -o $@ $(CPPFLAGS) $(OBJS)

clean:
	rm -f $(OBJS)
	rm -f cow

