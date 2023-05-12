
test1: c.out
	echo Primo test
	./c.out provadir 1 

test2: c.out
	echo Secondo test
	./c.out provadir1 5

test3: c.out
	echo Terzo test
	valgrind --leak-check=full --track-fds=yes --trace-children=yes c.out