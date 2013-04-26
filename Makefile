ROOT	:= $(abspath .)

libdirs_	= /usr/lib
libs_		=
include_ 	= /usr/include $(ROOT)/src

include Makefile.config

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

CFLAGS		+= -std=c11

INCLUDE		+= $(addprefix -I,$(include_))
LIBS		+= $(addprefix -l,$(libs_))
LIBDIRS		+= $(addprefix -L,$(subst :, ,$(libdirs_)))
CONFIG_H	= mempool_config.h

objects		:= $(patsubst src/%.c,src/%.o,$(wildcard src/*.c))

build : $(objects)
	$(CC) $(LINKFLAGS) $(LIBDIRS) $(LIBS) $^

src/%.o : $(ROOT)/src/%.c config
	cd src; $(CC) $(CFLAGS) $(FLAGS) $(INCLUDE) -c $<

config : Makefile.config config/$(BACKEND).h | cfg_header

cfg_header :
	@echo "#ifndef LIBMEMPOOL_CONFIG" > src/$(CONFIG_H); \
	echo "#define LIBMEMPOOL_CONFIG" >> src/$(CONFIG_H); \
	cat config/$(BACKEND).h >> src/$(CONFIG_H); \
	echo "#define LIBMEMPOOL_MULTITHREADED " $(MULTITHREADED) >> src/$(CONFIG_H); \
	echo "#define LIBMEMPOOL_COLORED " $(MULTITHREADED) >> src/$(CONFIG_H); \
	echo "#endif" >> src/$(CONFIG_H)

doc : FORCE
	$(DOCTOOL) $(DOCFLAGS) `find src -name *.[c]`

clean : FORCE
	rm -f $(LIBNAME).so; rm -f `find src -name "*.o"`; rm src/$(CONFIG_H)

FORCE :
