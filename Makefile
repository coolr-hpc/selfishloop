
TARGET=omp-selfishloop

omp-selfishloop : omp-selfishloop.c rdtsc.h
	gcc -o $@ -O2 -Wall -fopenmp $< -lgomp

clean:
	rm -f $(TARGET)

distclean: clean
	rm -f *~


