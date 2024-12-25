#
# Small makefile for testing with non-Microsoft compilers
#

include ../Makefile.config

#all: testSnap
all: Snap.o

testSnap: testSnap.cpp Snap.o 
	$(CC) $(CXXFLAGS) -o testSnap testSnap.cpp Snap.o -I$(CGLIB) $(LDFLAGS) $(LIBS)

#
# This is the important line. Here we build Snap.o, it uses all of
# snap-core and all of glib-core, It builds with glib-core's makefile also
# There isn't a makefile inside of glib-core (?)
# I need to trim as much from glib-core and snap-core as possible.
#

Snap.o: *.h *.cpp ../glib-core/*.h ../glib-core/*.cpp
	$(CC) -c $(CXXFLAGS) Snap.cpp -I$(CGLIB)

lib: Snap.o
	rm -f libsnap.a
	ar -cvq libsnap.a Snap.o

libinstall: lib
	cp libsnap.a /usr/local/lib/libsnap.a

clean:
	rm -f *.o *.a testSnap testSnap.exe *.plt *.tab *.png

