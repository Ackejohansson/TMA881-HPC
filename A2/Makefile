CC = gcc
CFLAGS_INLINE = -O2 -lm -march=native

all: cell_distances cell_distances_pragma

cell_distances: cell_distances.c
	$(CC) $(CFLAGS_INLINE) -o $@ $<

cell_distances_pragma: cell_distances_pragma.c
	$(CC) $(CFLAGS_INLINE) -o $@ $<

clean:
	rm -f cell_distances
	rm -r cell_distances
	rm -r extracted

.PHONY: all clean