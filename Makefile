CC = gcc

myshell: shell2.c
	$(CC) shell2.c -o myshell

clean:
	rm -f myshell