#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "units.h"
#include "log.h"

#define SHOW_ERROR_AND_EXIT(...) { log_write(__VA_ARGS__); exit(-1); }

struct unit_s {
   char symbol;
   unsigned int quantity;
};

struct unit_s time_units[] = {
   { 's', 1 },
   { 'm', 60 },
   { 'h', 3600 },
   { 'M', 0 },
   { 'Y', 0 },
   { '\0', 0 },
};

struct unit_s size_units[] = {
   { 'B', 1 },
   { 'K', 1024 },
   { 'M', 1024*1024 },
   { 'G', 1024*1024*1024 },
   { 'T', 0 },
   { '\0', 0 },
};

static unsigned int parse_unit_str(const char *s, const char *unit_name, struct unit_s *units);

unsigned int parse_time(const char *s) {
   return parse_unit_str(s, "time", time_units);
}

unsigned int parse_size(const char *s) {
   return parse_unit_str(s, "size", size_units);
}

static unsigned int parse_unit_str(const char *s, const char *unit_name, struct unit_s *units) {
   if(s == NULL) return 0;
   size_t len = strlen(s);
   if(len < 2) SHOW_ERROR_AND_EXIT("[config] invalid %s: %s", unit_name, s);

   unsigned int value = atoi(s);
   char unit_symbol = s[len-1];
   if(isdigit(unit_symbol)) SHOW_ERROR_AND_EXIT("[config] %s unit not specified: %s", unit_name, s);

   for(struct unit_s *unit = units; unit->symbol; unit++) {
      if(unit_symbol != unit->symbol) continue;
      if(unit->quantity == 0) SHOW_ERROR_AND_EXIT("[config] unit is too large: %s", s);
      return value * unit->quantity;
   }
}
