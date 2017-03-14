#
LIBNAME =rapidjson
LIBSUFFIX =.so

# common prefix 
INCS =-I/usr/local/include -I/usr/include
LIBSDIR =-L/usr/local/lib -L/usr/lib 

# basic setup
CC =g++
LIBS =$(LIBSDIR) libluajit-5.1.a
OPTS =-O3 -Wall
LIBOPT =-c
CFLAGS =$(LIBOPT) $(OPTS) $(INCS) -fPIC
CFLAGS_LIB =-shared $(OPTS) $(LIBS) $(INCS)

.PHONY: all release clean debug

all: $(LIBNAME)$(LIBSUFFIX) $(LIBNAME).o clean_obj

$(LIBNAME)$(LIBSUFFIX): $(LIBNAME).o
	$(CC) -o $@ $^ $(CFLAGS_LIB)

$(LIBNAME).o: lua_rapidjson.cpp
	$(CC) -o $@ $^ $(CFLAGS)

release: $(LIBNAME)$(LIBSUFFIX) clean_obj
	$(if $(shell which strip), strip $<)
	$(if $(shell which upx), upx --best $<)

clean:
	\rm -f *.o *.dll *.so

clean_obj:
	[ -f $(LIBNAME).o ] && \rm $(LIBNAME).o

debug: CC += -DDEBUG
debug: all