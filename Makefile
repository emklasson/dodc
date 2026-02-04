CXX=clang++
CXXFLAGS=-std=c++23 -Wall
LDFLAGS=

DODC_SOURCES = dodc.cc dodc_gmp_ecm.cc dodc_msieve.cc dodc_cado_nfs.cc multiprocessing.cc
SCHEDULE_BG_SOURCES = schedule_bg.cc multiprocessing.cc

DODC_OBJECTS := $(patsubst %.cc,%.o,$(DODC_SOURCES))
SCHEDULE_BG_OBJECTS := $(patsubst %.cc,%.o,$(SCHEDULE_BG_SOURCES))

DEPENDS := $(patsubst %.cc,%.d,$(DODC_SOURCES)) $(patsubst %.cc,%.d,$(SCHEDULE_BG_SOURCES))

all: dodc schedule_bg

dodc: $(DODC_OBJECTS)
	$(CXX) $(LDFLAGS) $^ -o $@

schedule_bg: $(SCHEDULE_BG_OBJECTS)
	$(CXX) $(LDFLAGS) $^ -o $@

-include $(DEPENDS)

%.o: %.cc Makefile
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(DODC_OBJECTS) $(SCHEDULE_BG_OBJECTS) $(DEPENDS) dodc schedule_bg
