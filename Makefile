

def: collector.c master.c unboundedqueue.c 
	gcc -g -Wall -pedantic collector.c unboundedqueue.c -o c.out
	gcc -g -Wall -pedantic master.c unboundedqueue.c -o m.out -lm
	gcc -g -Wall -pedantic main.c -o main

test1:
	./main . 1 

test2: 
	./main . 3

test3: 
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --track-fds=yes --trace-children=yes -s  ./m.out . 5
