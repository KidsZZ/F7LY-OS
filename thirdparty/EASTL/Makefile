CROSS_COMPILE ?= 
CXX = $(CROSS_COMPILE)g++
AR = $(CROSS_COMPILE)ar


CXXFLAGS += -I include -nostdlib
CXXFLAGS += -I test/packages/EABase/include/Common 
CXXFLAGS += -I ../../kernel/
CXXFLAGS += -I ../../kernel/tm/
CXXFLAGS += -Wno-deprecated-declarations 

#cc_src = $(filter-out source/thread_support.cpp, $(wildcard source/*.cpp))
cc_src = $(wildcard source/*.cpp)
cc_obj = $(patsubst %.cpp,$(BUILDPATH)/thirdparty/EASTL/%.o,$(cc_src))



.PHONY: all clean initdir test

all: initdir $(BUILDPATH)/thirdparty/EASTL/libeastl.a
	@echo "libeastl.a is created..."
	@echo "libeastl.a is created in $(BUILDPATH)/thirdparty/EASTL/"

initdir:
	mkdir -p $(BUILDPATH)/thirdparty/EASTL
	mkdir -p $(BUILDPATH)/thirdparty/EASTL/source

$(BUILDPATH)/thirdparty/EASTL/libeastl.a: $(cc_obj)
	$(AR) rcs $@ $^

$(BUILDPATH)/thirdparty/EASTL/source/%.o: source/%.cpp
	$(CXX) -c $(CXXFLAGS) -MMD -MF $(BUILDPATH)/thirdparty/EASTL/source/$*.d -o $@ $<
clean:
	rm -rf $(BUILDPATH)/thirdparty/EASTL

test:	
	@echo $(cc_src)
	@echo $(cc_obj)