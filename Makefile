all: p3_grader

threads.o: thread.cpp
	g++ -Wall -g -c thread.cpp -o threads.o

autograder_main.o: autograder_main.c
	g++ -c -o autograder_main.o autograder_main.c

p3_grader: autograder_main.o threads.o
	g++ -o p3_grader autograder_main.o threads.o

clean:
	rm -f p3_grader *.o
