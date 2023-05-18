

def: collector.c master.c unboundedqueue.c 
	gcc -Wall -pedantic collector.c unboundedqueue.c -o c.out
	gcc -Wall -pedantic master.c unboundedqueue.c -o m.out -lm
	gcc -Wall -pedantic main.c -o main

test1:
	./main . 1 

test2: 
	./main . 3

test3: 
	valgrind --leak-check=full --track-fds=yes --trace-children=yes ./main . 5