## you have command.cc command.h tokenizer.cc tokenizer.h you need compile them and link
## them to one object file called  : "myshell" 
## so we should run make , that will create our object myshell ,
## ./myshell should run your application 
CC = g++
CFLAGS = -Wall -g

OBJS = command.o tokenizer.o

myshell: $(OBJS)
	$(CC) $(CFLAGS) -o myshell $(OBJS)

command.o: command.cc command.h tokenizer.h
	$(CC) $(CFLAGS) -c command.cc

tokenizer.o: tokenizer.cc tokenizer.h
	$(CC) $(CFLAGS) -c tokenizer.cc

clean:
	rm -f *.o myshell
