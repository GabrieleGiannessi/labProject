

def: collector.c master.c unboundedqueue.c 
	gcc -Wall -pedantic collector.c unboundedqueue.c -o c.out
	gcc -Wall -pedantic master.c unboundedqueue.c -o m.out -lm

test1:
	echo 'Primo test'
	./m.out . 1 

test2: 
	echo Secondo test
	./m.out . 5

test3: 
	echo Terzo test
	valgrind --leak-check=full --track-fds=yes --trace-children=yes ./m.out . 5