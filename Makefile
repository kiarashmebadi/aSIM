CC      := gcc
CFLAGS  := -O2 -Wall -std=c11
IECSDK  ?= $(HOME)/libiec61850-sdk

INCLUDES := -I$(IECSDK)/include -I/usr/include/libxml2
LIBS     := -L$(IECSDK)/lib -liec61850 -ljansson -lxml2 -lpthread
BIN      := iec61850_csv_server

SRC      := main.c mapping.c model_iec.c icd_parser.c

all: $(BIN)
$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) -o $@ $(LIBS)

clean:
	rm -f $(BIN)
