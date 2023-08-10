#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>

#include "db.h"

extern void hexdump(const unsigned char *p, int lengthBytes);

#define DB_MAX_CHANNELS 64
static int ltn_db_count = 0;
struct ltn_db_entry_s db[DB_MAX_CHANNELS];

const struct ltn_db_entry_s *ltn_db_get(int index)
{
	if (index >= ltn_db_count)
		return NULL;

	return &db[index];
}

const struct ltn_db_entry_s *ltn_db_get_by_key(char key)
{
	for (int i = 0; i < ltn_db_count; i++) {
		if (db[i].key[0] == key)
			return &db[i];
	}

	return NULL;
}

int ltn_db_get_count()
{
	return ltn_db_count;
}

int ltn_db_load(const char *cfgfile)
{
	char *dir = strdup(cfgfile);
	dir = dirname(dir);

	printf("%s(%s)\n", __func__, cfgfile);
	memset(&db, 0, sizeof(db));

	FILE *fh = fopen(cfgfile, "rb");
	if (!fh)
		return -1;

	while (!feof(fh)) {
		char line[1024];
		fgets(&line[0], sizeof(line), fh);

		if (feof(fh))
			break;

		if (line[0] == '#')
			continue;

		if (strlen(line) < 2)
			continue;

		int r = sscanf(line, "%[^,],%[^,],%d,%[^,]\n",
			&db[ltn_db_count].key,
			&db[ltn_db_count].title,
			&db[ltn_db_count].lineNr,
			&db[ltn_db_count].filename);
		if (r != 4)
			continue;

		db[ltn_db_count].filename[ strlen(db[ltn_db_count].filename) - 1 ] = 0;

		//printf("  Loading %s\n", db[ltn_db_count].filename);
		FILE *fh = fopen(db[ltn_db_count].filename, "rb");
		if (!fh) {
			char relativepath[512];
			sprintf(relativepath, "%s/%s", dir, db[ltn_db_count].filename);

			//printf("  Loading %s\n", relativepath);
			fh = fopen(relativepath, "rb");
			if (!fh) {
				fprintf(stderr, "Unable to open VANC file %s\n\n", relativepath);
				continue;
			}
		}

		/* Load arbitrary data, re-align length for V210 and into something the blackmagic SDK wants to playout. */
		size_t l = fread(&db[ltn_db_count].payload[0], 2, 1024, fh);
		db[ltn_db_count].payloadWords = l;

		fclose(fh);

		printf("[%2d][%s,", ltn_db_count, db[ltn_db_count].key);
		printf("%s,", db[ltn_db_count].title);
		printf("%d,", db[ltn_db_count].lineNr);
		printf("%s,", db[ltn_db_count].filename);
		printf("%d]\n", db[ltn_db_count].payloadWords);

		//hexdump((const unsigned char *)&db[ltn_db_count].payload[0], db[ltn_db_count].payloadWords * 2);

		ltn_db_count++;
		
	}

	return 0;
}


