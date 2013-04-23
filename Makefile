export ROOT	:= $(abspath .)

include Makefile.config

build : src/*.o
	$(CC) $(LINKFLAGS) $(LIBDIRS) $(LIBS) $(wildcard src/*.o)

src/%.o : $(ROOT)/src/%.c
	cd src; $(CC) $(CFLAGS) $(FLAGS) $(INCLUDE) -c $^

doc : FORCE
	$(DOCTOOL) $(DOCFLAGS) `find src -name *.[c]`

clean : cleanobjs
	rm -f $(LIBNAME).so 

cleanobjs : FORCE
	rm -f `find src -name "*.o"`

FORCE :
