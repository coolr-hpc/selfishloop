
TARGET=omp-selfishloop

omp-selfishloop : omp-selfishloop.c selfish_json.c selfish_stat.c
	gcc -o $@ -O2 -Wall -fopenmp $^ -lgomp -lm

clean:
	rm -f $(TARGET)

distclean: clean
	rm -f *~


