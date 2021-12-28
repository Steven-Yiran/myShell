CFLAGS=-g -Wall -pedantic

mysh: mysh.c
	gcc $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f mysh