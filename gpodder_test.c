#include "gpodder.h"


int main()
{
	int ret;
	EnCours* enc;
	size_t enc_len;
	size_t i;
	
	ret = get_en_cours(&enc, &enc_len);
	
	if(!ret)
	{
		for(i=0;i<enc_len;i++)
		{
			printf("%s    %is\n", enc[i].filename, enc[i].position);
		}
	}
	
	return ret;
}