CXX=clang++
CXXFLAGS=-std=c++23 -Wall
LDFLAGS=

DODC_SOURCES = cfg_base_t.cc dodc.cc dodc_gmp_ecm.cc dodc_msieve.cc dodc_cado_nfs.cc multiprocessing.cc string_utils.cc

DODC_OBJECTS := $(patsubst %.cc,%.o,$(DODC_SOURCES))

DEPENDS := $(patsubst %.cc,%.d,$(DODC_SOURCES))

all: dodc

dodc: $(DODC_OBJECTS)
	$(CXX) $(LDFLAGS) $^ -o $@

-include $(DEPENDS)

%.o: %.cc Makefile
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(DODC_OBJECTS) $(DEPENDS) dodc
