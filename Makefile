CXX=       	g++
CXXFLAGS= 	-g -gdwarf-2 -std=gnu++11 -Wall -Iinclude -fPIC
LDFLAGS=	-Llib
AR=		ar
ARFLAGS=	rcs

LIB_HEADERS=	$(wildcard include/sfs/*.h)
LIB_SOURCE=	$(wildcard src/library/*.cpp)
LIB_OBJECTS=	$(LIB_SOURCE:.cpp=.o)
LIB_STATIC=	lib/libsfs.a

SHELL_SOURCE=	$(wildcard src/shell/*.cpp)
SHELL_OBJECTS=	$(SHELL_SOURCE:.cpp=.o)
SHELL_PROGRAM=	bin/folks
SHELL_LINK=	bin/sfssh

all:    $(LIB_STATIC) $(SHELL_PROGRAM) $(SHELL_LINK)

%.o:	%.cpp $(LIB_HEADERS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(LIB_STATIC):		$(LIB_OBJECTS) $(LIB_HEADERS)
	$(AR) $(ARFLAGS) $@ $(LIB_OBJECTS)

$(SHELL_PROGRAM):	$(SHELL_OBJECTS) $(LIB_STATIC)
	$(CXX) $(LDFLAGS) -o $@ $(SHELL_OBJECTS) -lsfs

$(SHELL_LINK):		$(SHELL_PROGRAM)
	cp $(SHELL_PROGRAM) $@


test:	$(SHELL_PROGRAM) $(SHELL_LINK)
	@for test_script in tests/test_*.sh; do $${test_script}; done

clean:
	rm -f $(LIB_OBJECTS) $(LIB_STATIC) $(SHELL_OBJECTS) $(SHELL_PROGRAM) $(SHELL_LINK)

.PHONY: all clean
