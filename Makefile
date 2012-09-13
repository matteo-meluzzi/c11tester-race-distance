include common.mk

OBJECTS = libthreads.o schedule.o model.o threads.o librace.o action.o \
	  nodestack.o clockvector.o main.o snapshot-interface.o cyclegraph.o \
	  datarace.o impatomic.o cmodelint.o \
	  snapshot.o malloc.o mymemory.o common.o

CPPFLAGS += -Iinclude -I. -rdynamic
LDFLAGS = -ldl -lrt
SHARED = -shared

# Mac OSX options
ifeq ($(UNAME), Darwin)
CPPFLAGS += -D_XOPEN_SOURCE -DMAC
LDFLAGS = -ldl
SHARED = -Wl,-undefined,dynamic_lookup -dynamiclib
endif

TESTS_DIR = test

program_H_SRCS := $(wildcard *.h) $(wildcard include/*.h)
program_C_SRCS := $(wildcard *.c) $(wildcard *.cc)
DEPS = make.deps

all: $(LIB_SO) $(DEPS) tests

$(DEPS): $(program_C_SRCS) $(program_H_SRCS)
	$(CXX) -MM $(program_C_SRCS) $(CPPFLAGS) > $(DEPS)

# Only include, rebuild make.deps when it's going to be used
ifeq ($(MAKECMDGOALS),$(DEPS))
include $(DEPS)
endif

debug: CPPFLAGS += -DCONFIG_DEBUG
debug: all

docs: *.c *.cc *.h
	doxygen

$(LIB_SO): $(OBJECTS)
	$(CXX) $(SHARED) -o $(LIB_SO) $(OBJECTS) $(LDFLAGS)

malloc.o: malloc.c
	$(CC) -fPIC -c malloc.c -DMSPACES -DONLY_MSPACES $(CPPFLAGS)

%.o: %.cc
	$(CXX) -fPIC -c $< $(CPPFLAGS)

clean:
	rm -f *.o *.so
	$(MAKE) -C $(TESTS_DIR) clean

mrclean: clean
	rm -rf docs

tags::
	ctags -R

tests:: $(LIB_SO)
	$(MAKE) -C $(TESTS_DIR)
