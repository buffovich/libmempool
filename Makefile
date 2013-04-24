ROOT	:= $(abspath .)

libdirs_	= /usr/lib
libs_		=
include_ 	= /usr/include $(ROOT)/src

include Makefile.config

include config/Makefile.$(BACKEND)

LIBNAME			= libmempool
CC				?= gcc
LINKFLAGS		= -Xlinker --no-as-needed -Xlinker -Bdynamic -shared -Xlinker --export-dynamic -o $(LIBNAME).so
FLAGS			= -fPIC -Wall -Wextra

ifdef DEBUG
DEBUG_FORMAT	?= gdb
FLAGS			+= -O0 -g -g$(DEBUG_FORMAT)$(DEBUG)
else
OPTIMIZE		?= 3
FLAGS			+= -O$(OPTIMIZE)
#LINKFLAGS		+= -s
endif

CFLAGS		+= -std=c11 -D MULTITHREADED=$(MULTITHREADED) -D COLORED=$(COLORED) -D MALLOC=$(MALLOC) -D FREE=$(FREE) -D POSIX_MEMALIGN=$(POSIX_MEMALIGN)

INCLUDE		+= $(addprefix -I,$(include_))
LIBS		+= $(addprefix -l,$(libs_))
LIBDIRS		+= $(addprefix -L,$(subst :, ,$(libdirs_)))

build : src/*.o
	$(CC) $(LINKFLAGS) $(LIBDIRS) $(LIBS) $^

src/%.o : $(ROOT)/src/%.c config
	cd src; $(CC) $(CFLAGS) $(FLAGS) $(INCLUDE) -c $<

config : Makefile.config config/Makefile.$(BACKEND)

doc : FORCE
	$(DOCTOOL) $(DOCFLAGS) `find src -name *.[c]`

clean : cleanobjs
	rm -f $(LIBNAME).so 

cleanobjs : FORCE
	rm -f `find src -name "*.o"`

FORCE :
