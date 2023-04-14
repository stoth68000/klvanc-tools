#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>

struct ltn_db_entry_s {
	char key[2];
	char title[256];
	int lineNr;
	char filename[512];

	int payloadWords;
	uint16_t payload[1024];
};

int ltn_db_load(const char *cfgfile);
int ltn_db_get_count();

const struct ltn_db_entry_s *ltn_db_get(int index);
const struct ltn_db_entry_s *ltn_db_get_by_key(char key);
