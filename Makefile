CFLAGS=-Wall

OBJECTS=check_nat64.o
OUTPUT=check_nat64

all: $(OUTPUT)

$(OUTPUT): $(OBJECTS)
	$(CC) $(LDFLAGS) $< -o $@

clean:
	rm $(OUTPUT) $(OBJECTS)

.PHONY: all clean
