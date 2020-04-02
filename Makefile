# ------------------------------------------------
# Makefile based on gcc
#
# author: whqee
# ChangeLog :
#	2020-04-02 - first version
# ------------------------------------------------

TARGET = tiny_server
BUILD_DIR = build

ifndef	CC
CC=cc
endif
ifndef	SZ
SZ=size
endif

# debug build?
DEBUG = 1
# optimization
OPT = -Og

# C sources
C_SOURCES =  \
tiny_server.c

# C defines
C_DEFS =  \
# -DMULTI_CNAME_SERVICE

# C includes
C_INCLUDES =  

# compile gcc flags
CFLAGS = $(C_DEFS) $(C_INCLUDES) $(OPT)

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

# libraries
LIBS = -lc -lpthread
LIBDIR = 
LDFLAGS = $(LIBDIR) $(LIBS)

## default action: build all
all: $(BUILD_DIR)/$(TARGET)


## build the application:
# list of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR) 
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@
	
$(BUILD_DIR):
	mkdir $@	

## clean up
clean:
	-rm -fR $(BUILD_DIR)
