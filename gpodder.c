#include "gpodder.h"

#include <string.h>
#include <stdio.h>
#include <search.h>

#include <curl/curl.h>

#define JSMN_PARENT_LINKS
#include <jsmn.h>

static int get_events_str(const char* user, const char* password, long timestamp, char** events_str);
static int dummy_get_events_str(const char* user, const char* password, long timestamp, char** events_str);
static int parse_events_str(char* events_str, EnCours** events, size_t* encours_length, long* timestamp);

#define URL_BUF_SIZE 64

typedef enum {
	INVAL,
	PLAY,
	DOWNLOAD,
	DELETE
} Action;

typedef struct {
	const char* episode;
	int position;
	int total;
	void* next;
} Play;

struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
 
  mem->memory = realloc(mem->memory, mem->size + realsize + 1);
  if(mem->memory == NULL) {
    /* out of memory! */ 
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }
 
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
 
  return realsize;
}

static int
get_events_str(const char* user, const char* password, long timestamp, char** events_str)
{
	CURL *curl;
	CURLcode res;
	char errbuf[CURL_ERROR_SIZE] = {0};
	char* url = malloc(URL_BUF_SIZE);
	struct MemoryStruct chunk;

	chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */ 
	chunk.size = 0;    /* no data at this point */ 

	// curl -v -u user:pass http://gpodder.net/api/2/episodes/user.json?since=1445824406 > /tmp/actionsnonagg.json
	snprintf(url, URL_BUF_SIZE, "http://gpodder.net/api/2/episodes/%s.json?since=%li", user, timestamp);
	
	printf("D: gpodder url %s\n", url);
	
	curl = curl_easy_init();

	if(curl)
	{
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
		curl_easy_setopt(curl, CURLOPT_USERNAME, user);
		curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
		curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
		
		res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);

		if(res != CURLE_OK)
		{
			size_t len = strlen(errbuf);
			fprintf(stderr, "E: libcurl: (%d) ", res);
			if(len)
			{
				fprintf(stderr, "%s%s", errbuf,
					((errbuf[len - 1] != '\n') ? "\n" : ""));
			}
			else
			{
				fprintf(stderr, "%s\n", curl_easy_strerror(res));
			}
			return -1;
		}
		
		*events_str = chunk.memory;
		return 0;
  	}else{
  		fprintf(stderr, "E: couldn't initialize curl\n");
		return -1;
	}
}

static int
dummy_get_events_str(const char* user, const char* password, long timestamp, char** events_str)
{
	struct MemoryStruct chunk;
	size_t len;
	FILE* src;
	char* buf = malloc(4096);

	chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */ 
	chunk.size = 0;    /* no data at this point */ 

	src = fopen("events.json", "r");
	if(src == NULL)
	{
		perror("open dummy events.json");
		free(buf);
		return -1;
	}
	
	while((len = fread(buf, 1, 4094, src)) > 0)
	{
		WriteMemoryCallback(buf, 1, len, &chunk);
	}

	if(ferror(src))
	{
		perror("read dummy events.json");
		free(buf);
		free(chunk.memory);
		return -1;
	}
	
	*events_str = chunk.memory;
	free(buf);
	
	return 0;
}

static int
isKey(jsmntok_t* token, const char* events_str, const char* key)
{
	const char* st = events_str+(token->start);
	return token->type == JSMN_STRING
		&& strstr(st, key) == st
		&& strlen(key) == (token->end - token->start);
}

static int
isNum(jsmntok_t* token, const char* events_str)
{
	const char* st = events_str+(token->start);
	return token->type == JSMN_PRIMITIVE
		&& st[0] >= '0'
		&& st[0] <= '9';
}

#define TOKENS_SIZE 10000

static int
parse_events_str(char* events_str, EnCours** encours, size_t* encours_length, long* timestamp)
{
	jsmn_parser parser;
	jsmntok_t* tokens;
	int ret;
	int i;
	int j;
	int k;
	char* episode;
	Action action;
	ENTRY e;
	ENTRY* ep;
	Play* play;
	Play* first_play;
	Play* cur_play;
	size_t n_play;
	EnCours* enc;
	int position;
	int total;
	
	*encours_length = 0;

	first_play = calloc(1, sizeof(Play));
	cur_play = first_play;
	n_play = 0;
	
	tokens = calloc(TOKENS_SIZE, sizeof(jsmntok_t));
	if(tokens ==  NULL)
	{
		fprintf(stderr, "E: parse_events_str can't allocate tokens\n");
		return -1;
	}
	
	jsmn_init(&parser);

	ret = jsmn_parse(&parser, events_str, strlen(events_str), tokens, TOKENS_SIZE);

	if(ret<0)
	{
		if(ret == JSMN_ERROR_INVAL)
		{
			fprintf(stderr, "E: invalid JSON string\n");
		}
		else if(ret == JSMN_ERROR_NOMEM)
		{
			fprintf(stderr, "E: not enough memory\n");
		}
		else if(ret == JSMN_ERROR_PART)
		{
			fprintf(stderr, "E: json incomplete\n");
		}
		return ret;
	}
	else
	{
		printf("D: %i tokens\n", ret);
		if(ret>0)
		{
			hcreate(ret/8);
			if(tokens[0].type == JSMN_OBJECT && tokens[0].size == 2)
			{
				if(isKey(tokens+1, events_str, "timestamp"))
				{
					if(tokens[2].type == JSMN_PRIMITIVE)
					{
						*timestamp = atol(events_str+tokens[2].start);
						printf("D: timestamp:%li\n", *timestamp);
					}
				}
				if(isKey(tokens+3, events_str, "actions"))
				{
					if(tokens[4].type == JSMN_ARRAY){
						printf("D: actions array\n");
						for(i=0, j=5;i<tokens[4].size;i++)
						{
							//printf("D: event %i %i %i\n", j, tokens[j].type, tokens[j].size);
							if(tokens[j].type == JSMN_OBJECT)
							{
								episode = NULL;
								for(k=0;k<tokens[j].size;k++)
								{
									//printf("D: token %i %i %i\n", j+1+k*2, tokens[j+1+k*2].type, tokens[j+1+k*2].size);
									if(isKey(tokens+j+1+k*2, events_str, "action"))
									{
										if(isKey(tokens+j+1+k*2+1, events_str, "download"))
										{
											printf("D: download\n");
											action = DOWNLOAD;
										}
										else if(isKey(tokens+j+1+k*2+1, events_str, "play"))
										{
											printf("D: play\n");
											action = PLAY;
										}
										else if(isKey(tokens+k*2+1, events_str, "delete"))
										{
											printf("D: delete\n");
											action = DELETE;
										}
										else
										{
											printf("E: invalid action %.*s\n", tokens[k*2+1].end - tokens[k*2+1].start, events_str+tokens[k*2+1].start);
										}
									}
									else if(isKey(tokens+j+1+k*2, events_str, "episode"))
									{
										events_str[tokens[j+1+k*2+1].end] = '\0';
										episode = events_str+tokens[j+1+k*2+1].start;
									}
									else if(isKey(tokens+j+1+k*2, events_str, "position"))
									{
										if(isNum(tokens+j+1+k*2+1, events_str))
										{
											position = atoi(events_str+tokens[j+1+k*2+1].start);
										}
										else
										{
											position = 0;
										}
									}
									else if(isKey(tokens+j+1+k*2, events_str, "total"))
									{
										if(isNum(tokens+j+1+k*2+1, events_str))
										{
											total = atoi(events_str+tokens[j+1+k*2+1].start);
										}
										else
										{
											total = 0;
										}
									}
								}
								if(episode != NULL)
								{
									e.key = episode;
									ep = hsearch(e, FIND);
									if(ep == NULL)
									{
										switch(action)
										{
										case DELETE:
											e.data = NULL;
											ep = hsearch(e, ENTER);
											printf("D: DELETE %s\n", episode);
											break;
										case PLAY:
											play = malloc(sizeof(Play));
											play->episode = episode;
											play->position = position;
											play->total = total;
											play->next = NULL;
											cur_play->next = play;
											cur_play = play;
											n_play++;
											e.data = play;
											ep = hsearch(e, ENTER);
											printf("D: PLAY %s\n", episode);
											break;
										case DOWNLOAD:
										case INVAL:
										default:
											break;
										}
									}
								}
							}
							j+= tokens[j].size+1;
						}
					}
				}
			}
			printf("D: found %li plays\n", n_play);
			enc = calloc(n_play, sizeof(EnCours));
			*encours = enc;
			*encours_length = 0;
			if(n_play>0)
			{
				for(play=first_play->next;play!=NULL;play=play->next)
				{
					e.key = (char*)play->episode;
					printf("D: %li %s\n", *encours_length, e.key);
					if((ep = hsearch(e, FIND))!= NULL && ep->data != NULL
						&& (play->position == 0 || play->position != play->total))
					{
						printf("D: YAY %li %s\n", *encours_length, e.key);
						enc[*encours_length].uri = play->episode;
						enc[*encours_length].filename = play->episode;
						enc[*encours_length].position = play->position;
						(*encours_length)++;
					}
				}
			}
			printf("D: tokens[1] %i %i - %i %i %.*s\n", tokens[1].type, tokens[1].start, tokens[1].end, tokens[1].size, tokens[1].end - tokens[1].start, events_str+tokens[1].start);
			printf("D: tokens[2] %i %i - %i %i %.*s\n", tokens[2].type, tokens[2].start, tokens[2].end, tokens[2].size, tokens[2].end - tokens[2].start, events_str+tokens[2].start);
			//printf("D: tokens[3] %i %i - %i %i %.*s\n", tokens[3].type, tokens[3].start, tokens[3].end, tokens[3].size, tokens[3].end - tokens[3].start, events_str+tokens[3].start);
			//printf("D: tokens[4] %i %i - %i %i %.*s\n", tokens[4].type, tokens[4].start, tokens[4].end, tokens[4].size, tokens[4].end - tokens[4].start, events_str+tokens[4].start);
			for(i=5;i<5+tokens[5].size;i++)
			{
				printf("D: tokens[%i] %i %i - %i %i %.*s\n", i, tokens[i].type, tokens[i].start, tokens[i].end, tokens[i].size, tokens[i].end - tokens[i].start, events_str+tokens[i].start);
			}
		}
	}
	
	return 0;
}


int get_en_cours(EnCours** res, size_t* res_count)
{
	int ret;
	char* events_str;
	const char* user = "elelay";
	const char* password = getenv("GPODDER_PASSWORD");
	long timestamp = 1445824406L;
	
	//ret = get_events_str(user, password, timestamp, (const char**) &events_str);
	ret = dummy_get_events_str(user, password, timestamp, &events_str);
	if(ret)
	{
		return ret;
	}
	
	printf("res: %.100s...\n", events_str);
	
	ret = parse_events_str(events_str, res, res_count, &timestamp);
	
	if(!ret)
	{
		printf("D: %li en cours, timestamp=%li\n", *res_count, timestamp);
	}
	
	return ret;
}
