all: oss user
oss: oss.c queue.c queue.h
	gcc oss.c queue.c queue.h -o oss 

clean:
	-rm oss user