# Global target; when 'make' is run without arguments, this is what it should do
BIN = ./bin
OBJ = ./obj

INC = . platform/inc core/inc
SRC = . platform/src core/src

TARGET = lcd_test
all: prepare $(TARGET)

prepare:
	@echo CFLAGS: $(CFLAGS)
	mkdir -p $(OBJ)
 
INCLUDES = $(wildcard $(addsuffix /*.h, $(INC)))
SOURCES = $(wildcard $(addsuffix /*.c, $(SRC)))

# These variables hold the name of the compilation tool, the compilation flags and the link flags
# We make use of these variables in the package manifest
CC = gcc
override CFLAGS += -Wall $(addprefix -I, $(INC))
override LDFLAGS += -lm
 
DEPS = $(INCLUDES)
OBJECTS = $(patsubst %.c, $(OBJ)/%.o, $(notdir $(SOURCES)))
 
.SECONDEXPANSION:
# This rule builds individual object files, and depends on the corresponding C source files and the header files
#$(OBJ)/%.o: $$(addsuffix /%.c, $(SRC)) $(DEPS)
$(OBJ)/%.o: ./%.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(OBJ)/%.o: platform/src/%.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)
 
$(OBJ)/%.o: core/src/%.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJECTS)
	@echo $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)
 
# To clean build artifacts, we specify a 'clean' rule, and use PHONY to indicate that this rule never matches with a potential file in the directory
.PHONY: prepare clean
 
clean:
	rm -f $(TARGET) $(OBJ)/*.o  
