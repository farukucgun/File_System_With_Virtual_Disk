all: libvsfs.a create_format app

libvsfs.a: 	vsfs.c
	gcc -Wall -c vsfs.c
	ar -cvq libvsfs.a vsfs.o
	ranlib libvsfs.a

create_format: create_format.c
	gcc -Wall -o create_format  create_format.c   -L. -lvsfs 

app: 	app.c
	gcc -Wall -o app app.c -L. -lvsfs

clean: 
	rm *.o *~ libvsfs.a app create_format

cleanall: 
	rm *.o *~ libvsfs.a app vdisk create_format
