#ifndef GPODDER_H
#define GPODDER_H

#include <stdlib.h>

typedef struct {
	const char* uri;
	const char* filename;
	int position;
} EnCours;

int get_en_cours(EnCours** res, size_t* res_count);


#endif        //  #ifndef GPODDER_H
