/* main.c
   Copyright 2015 Eric Le Lay
   This file is part of LecteurAudio.

    LecteurAudio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    LecteurAudio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with LecteurAudio.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <mpd/async.h>
#include <mpd/client.h>
#include <mpd/status.h>
#include <mpd/song.h>
#include <mpd/entity.h>
#include <mpd/search.h>
#include <mpd/tag.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <time.h>

#include <curl/curl.h>

#include "ecran.h"
#include "controles.h"

#define BRIGHT 1
#define RED 31
#define GREEN 32
#define YELLOW 33
#define BG_BLACK 40
#define COLOR_CODE 0x1B

#define DEFAULT_LOG_FILE "/var/log/la.out"
#define DEFAULT_ERROR_FILE "/var/log/la.err"
#define DEFAULT_WLAN_ITF "wlan0"
//#define CONFIG_SLEEP 1

typedef enum {
	LA_STATE_PLAYING, LA_STATE_MENU, LA_STATE_RESUME, LA_STATE_LIST, LA_STATE_ADD_REPLACE, LA_STATE_VOLUME, LA_STATE_SETTINGS, LA_STATE_RADIO
} LaState;

static void print_list(int old_state_list);
static void print_settings();
static int do_shutdown(struct mpd_connection *conn);
static int print_status(struct mpd_connection *conn);
static int do_sleep(struct mpd_connection* conn);
static int do_wifi_status();
static void free_list_state();
static int reconnect_to_mpd(struct mpd_connection **conn);
static int do_update_played(struct mpd_connection *conn);
static int do_play(struct mpd_connection* conn);


LaState state;

int state_menu;

char** list_contents = NULL;
char** list_uris = NULL;
bool* list_is_dir = NULL;
int list_length;
int state_list;
char* state_list_path;
int state_list_dir_index;
int state_list_rl_offset;
int* resume_played;
bool ignore_next_idle;

#define LIST_RADIOS_LEN 2
const char* list_radios[LIST_RADIOS_LEN] = {
	"France Inter",
	"Radio Rennes"
};
const char* list_radios_uris[LIST_RADIOS_LEN] = {
	"http://audio.scdn.arkena.com/11008/franceinter-midfi128.mp3",
	"http://sv2.vestaradio.com:5750/;stream.mp3"
};

int state_add_replace;

int state_settings;

static char log_buffer[16];

#define LA_SIG_INACTIVE 123
timer_t timer_inactive;
volatile sig_atomic_t inactive_flag = 0;
bool asleep = false;

#define LA_SIG_SLEEP 124
timer_t timer_sleep;
volatile sig_atomic_t sleep_flag = 0;

#define LOG_INFO(x, ...) {printf("    [info]" x "\n", __VA_ARGS__);}
#define LOG_WARNING(x, ...) \
{\
	fprintf(stderr, "%c[%d;%d;%dm[WARNING](%s:%d) %s : " x "\n", COLOR_CODE, BRIGHT, YELLOW, BG_BLACK, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);\
	printf("%c[%dm", 0x1B, 0);\
}

#define LOG_ERROR(x, ...) \
{\
	fprintf(stderr, x, __VA_ARGS__);\
	fflush(stderr);\
	snprintf(log_buffer,16, x, __VA_ARGS__);\
	la_lcdPosition(0,0);\
	la_lcdPuts(log_buffer);\
}

#define CHECK_CONNECTION(conn) \
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) { \
		LOG_ERROR("%s", mpd_connection_get_error_message(conn)); \
		return -1; \
	}


static int
reconnect_to_mpd(struct mpd_connection **conn)
{
	*conn = mpd_connection_new(NULL, 0, 30000);
	if (*conn == NULL) {
		LOG_ERROR("%s", "Out of memory");
		return -1;
	}

	if (mpd_connection_get_error(*conn) == MPD_ERROR_SUCCESS)
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

static int
connect_to_mpd(struct mpd_connection **conn)
{
	int retry = 30;
	int t = 1;
	int ret;
	// pour attendre que le service soit bien démarré
	while(t <= retry){
		ret = reconnect_to_mpd(conn);
		if (ret == -1)  // fatal
		{
			return ret;
		}

		if (ret == 0)
		{
			return 0;
		}
		else
		{
			LOG_ERROR("%d/%d %s", t, retry, mpd_connection_get_error_message(*conn));
			mpd_connection_free(*conn);
			*conn = NULL;
			sleep(3);
		}
		t++;
	}
	return -1;
}

static int
do_clear_current(struct mpd_connection* conn)
{
	if(do_update_played(conn))
	{
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}

	printf("D: do_clear_current\n");
	if(!mpd_run_clear(conn))
	{
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}
	printf("D: do_clear_current end\n");

	return 0;
}

static int
do_replace_playing_with_selected(struct mpd_connection* conn, bool replace)
{
	char* file = list_uris[state_list];

	printf("D: switch to %s\n", file);

	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);

	if(replace)
	{
		if(do_clear_current(conn))
		{
			return -1;
		}
	}

	if(!mpd_run_add(conn, file))
	{
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}

	if(do_play(conn))
	{
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}

	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}

	return 0;
}

static int
do_resume_selected(struct mpd_connection* conn)
{
	char* file = list_uris[state_list];
	int played = resume_played[state_list];

	printf("D: switch to %s at %i\n", file, played);

	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);

	if(do_clear_current(conn))
	{
		LOG_ERROR("do_resume_selected %s failed\n", "do_clear_current");
		return -1;
	}

	if(!mpd_run_add(conn, file))
	{
		LOG_ERROR("do_resume_selected %s", mpd_connection_get_error_message(conn));
		return -1;
	}

	if(!mpd_run_seek_pos(conn, 0, played))
	{
		LOG_ERROR("do_resume_selected %s", mpd_connection_get_error_message(conn));
		return -1;
	}

	if(do_play(conn))
	{
		LOG_ERROR("do_resume_selected %s", mpd_connection_get_error_message(conn));
		return -1;
	}

	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}

	return 0;
}

static int
fetch_and_print_volume(struct mpd_connection *conn)
{
	struct mpd_status *status;
	int volume;


	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);

	status = mpd_run_status(conn);
	if (!status) {
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}

	volume = mpd_status_get_volume(status);

	la_lcdClear();
	la_lcdHome();

	if(volume == -1)
	{
		la_lcdPuts("E: no volume");
	}
	else
	{
		snprintf(log_buffer,16, "%2i%%", volume);
		la_lcdPuts(log_buffer);
	}

	mpd_status_free(status);

	mpd_response_finish(conn);
	CHECK_CONNECTION(conn);

	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}

	return 0;
}

static int
do_change_volume(struct mpd_connection *conn, int inc, bool set)
{
	struct mpd_status *status;
	int volume;

	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);

	if(set)
	{
		if(!mpd_run_set_volume(conn, inc))
		{
			LOG_ERROR("Unable to set volume to %i\n",inc);
			return -1;
		}
	}
	else
	{
		if(!mpd_run_change_volume(conn, inc))
		{
			LOG_ERROR("Unable to change volume %i\n",inc);
			return -1;
		}
	}

	status = mpd_run_status(conn);
	if (!status) {
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}

	volume = mpd_status_get_volume(status);

	la_lcdClear();
	la_lcdHome();

	if(volume == -1)
	{
		la_lcdPuts("E: no volume");
	}
	else
	{
		snprintf(log_buffer,16, "%2i%%", volume);
		la_lcdPuts(log_buffer);
	}

	mpd_status_free(status);

	mpd_response_finish(conn);
	CHECK_CONNECTION(conn);

	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}

	return 0;
}


static int
print_status(struct mpd_connection *conn)
{
	struct mpd_status *status;
	struct mpd_song *song;
	char *value;
	char* tmp;

	la_lcdClear();

	song = mpd_run_current_song(conn);
	if (song != NULL) {

		if((value = (char*)mpd_song_get_tag(song, MPD_TAG_TITLE, 0)) == NULL)
		{
			if((value = (char*)mpd_song_get_uri(song)) == NULL)
			{
				value = "<NO URI>";
			}
			else
			{
				tmp = strrchr(value, '/');
				if(tmp != NULL)
				{
					value = tmp+1;
				}
			}
		}

		la_lcdPosition(0,0);
		la_lcdPuts(value);

		if((value = (char*)mpd_song_get_tag(song, MPD_TAG_ARTIST, 0)) == NULL)
		{
			if((value = (char*)mpd_song_get_uri(song)) == NULL)
			{
				value = "<NO URI>";
			}
			else
			{
				tmp = strrchr(value, '/');
				if(tmp == NULL)
				{
					value = "<NO DIR>";
				}
				else
				{
					*tmp='\0';
					tmp = strrchr(value, '/');
					if(tmp != NULL)
					{
						value = tmp+1;
					}
				}
			}
		}

		la_lcdPosition(0,1);
		la_lcdPuts(value);


		mpd_song_free(song);
	}

	mpd_response_finish(conn);

	status = mpd_run_status(conn);
	if (!status) {
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}

	la_lcdPosition(15, 1);
	switch (mpd_status_get_state(status)){
	case MPD_STATE_STOP:
		la_lcdPutChar('X');
		break;
	case MPD_STATE_PLAY:
		la_lcdPutChar('P');
		break;
	case MPD_STATE_PAUSE:
		la_lcdPutChar('=');
		break;
	default:
		la_lcdPutChar('?');
		break;
	}
	mpd_status_free(status);

	mpd_response_finish(conn);
	CHECK_CONNECTION(conn);

	return 0;
}

static int
do_update_played(struct mpd_connection *conn)
{
	struct mpd_status *status;
	struct mpd_song *song;
	const char *value;
	char* tmp;
	char* uri;                     
	int played;
	bool previous;

	uri = NULL;
	previous = false;

	song = mpd_run_current_song(conn);
	if (song == NULL)
	{
		printf("D: do_update_played no current song\n");
		mpd_response_finish(conn);
		song = mpd_run_get_queue_song_pos(conn, 0);
		previous = true;
		
		if(song == NULL)
		{
			printf("D: do_update_played no previous song\n");
			mpd_response_finish(conn);
			CHECK_CONNECTION(conn);
		}
		else
		{
			played = mpd_song_get_duration(song);
		}
	}
	
	
	if(song != NULL)
	{
		if((value = mpd_song_get_uri(song)) != NULL)
		{
			if(strstr(value, "http://") != value)
			{
				uri = strdup(value);
			}
		}

		mpd_song_free(song);
		mpd_response_finish(conn);
	}

	if(!previous)
	{
		status = mpd_run_status(conn);
		if (!status) {
			LOG_ERROR("%s", mpd_connection_get_error_message(conn));
			return -1;
		}

		played = mpd_status_get_elapsed_time(status);
		mpd_status_free(status);
	
		mpd_response_finish(conn);
		CHECK_CONNECTION(conn);
	}


	if(uri)
	{
		tmp = malloc(10);
		snprintf(tmp, 10, "%i", played);

		printf("D: saving sticker played %s = %i\n", uri, played);

		if(!mpd_run_sticker_set(conn, "song", uri, "played", tmp))
		{
			LOG_ERROR("%s", mpd_connection_get_error_message(conn));
			return -1;
		}

		free(tmp);
	}

	return 0;
}

static bool
is_stream_in_queue(struct mpd_connection* conn)
{
	struct mpd_song *song;
	char *value;
	bool ret = false;

	song = mpd_run_current_song(conn);
	if (song != NULL) {

		if((value = (char*)mpd_song_get_uri(song)) == NULL)
		{
			fprintf(stderr, "E: is_stream_in_queue NO URI\n");
		}
		else
		{
			ret = strstr(value, "http://") == value;
		}

		mpd_song_free(song);
	}

	mpd_response_finish(conn);

	CHECK_CONNECTION(conn);

	return ret;
}

typedef struct StringList {
	char* value;
	struct StringList* next;
} StringList;

static StringList*
mk_string_list()
{
	StringList *tmp;
	tmp = calloc(1, sizeof(StringList));
	if(tmp == NULL){
		LOG_ERROR("E: %s", "calloc");
		exit(-1);
	}
	return tmp;
}

typedef struct IntList {
	int value;
	struct IntList* next;
} IntList;

static IntList*
mk_int_list()
{
	IntList *tmp;
	tmp = calloc(1, sizeof(IntList));
	if(tmp == NULL){
		LOG_ERROR("E: %s", "calloc");
		exit(-1);
	}
	return tmp;
}

static char*
get_filename_from_uri(char* uri)
{
	char* value;

	value = strrchr(uri, '/');

	if(value == NULL)
	{
		value = uri;
	}
	else
	{
		value++;
	}

	return value;
}

static char*
la_mpd_song_get_filename(const struct mpd_song* song)
{
	char* value;
	if((value = (char*)mpd_song_get_tag(song, MPD_TAG_TITLE, 0)) == NULL)
	{
		if((value = (char*)mpd_song_get_uri(song)) == NULL)
		{
			value = "<NO URI>";
		}
		else
		{
			value = get_filename_from_uri(value);
		}
	}
	return strdup(value);
}

static char*
format_resume_label(char* uri, int played)
{
	char* filename;
	char* buf;
	size_t len;
	int ret;

	filename = get_filename_from_uri(uri);

	len = strlen(filename)+1+3*(2+1)+1;
	buf = malloc(len);

	if(buf == NULL)
	{
		perror("format_resume_label allocate buf");
		return NULL;
	}

	ret = snprintf(buf, len, "%s %02i:%02i:%02i", filename, played / 3600, (played % 3600) / 60, played % 60);

	if(ret > 0)
	{
		return buf;
	}
	else
	{
		free(buf);
		return NULL;
	}
}


static void
free_list_state()
{
	size_t tmp_list_len;
	char **tmpl, **fns_tmpl;

	tmp_list_len = list_length;
	list_length = 0;
	for(tmpl = list_contents, fns_tmpl = list_uris; tmp_list_len != 0; tmpl++, fns_tmpl++)
	{
		free(*tmpl);
		if(list_uris != NULL)
		{
			free(*fns_tmpl);
		}
		tmp_list_len--;
	}
	free(list_contents);
	free(list_uris);
	list_uris = NULL;
}

static int
fetch_list(struct mpd_connection *conn, char* path)
{
	StringList *buf, *tmp, *cur, *fns, *cur_fns, *tmp_fns;
	char **tmpl;
	int tmp_len;
	struct mpd_entity* entity;
	const struct mpd_directory* dir;
	const struct mpd_song* song;
	char *value, *uri;

	tmp_len = 0;
	cur = NULL;

	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);

	if(path == NULL)
	{
		mpd_send_list_meta(conn, "/");
	}
	else
	{
		printf("D: fetch_list(%s)\n", path);
		mpd_send_list_meta(conn, path);
	}

	CHECK_CONNECTION(conn);

	tmp_len = 0;
	while((entity = mpd_recv_entity(conn)) != NULL)
	{
		if(path == NULL)
		{
			if(mpd_entity_get_type(entity)  ==  MPD_ENTITY_TYPE_DIRECTORY)
			{
				dir = mpd_entity_get_directory(entity);
				value = strdup(mpd_directory_get_path(dir));
				if (cur == NULL)
				{
					buf = tmp = mk_string_list();
					tmp->value = value;
				}
				else
				{
					tmp = mk_string_list();
					tmp->value = value;
					cur->next = tmp;
				}
				cur = tmp;
				tmp_len++;
			}
		}
		else
		{
			if(mpd_entity_get_type(entity)  ==  MPD_ENTITY_TYPE_SONG)
			{
				song = mpd_entity_get_song(entity);
				uri = strdup(mpd_song_get_uri(song));
				value = la_mpd_song_get_filename(song);
				if (cur == NULL)
				{
					buf = tmp = mk_string_list();
					tmp->value = value;

					fns = tmp_fns = mk_string_list();
					tmp_fns->value = uri;
				}
				else
				{
					tmp = mk_string_list();
					tmp->value = value;
					cur->next = tmp;

					tmp_fns = mk_string_list();
					tmp_fns->value = uri;
					cur_fns->next = tmp_fns;
				}
				cur = tmp;
				cur_fns = tmp_fns;
				tmp_len++;
			}
		}

		mpd_entity_free(entity);
	}

	mpd_response_finish(conn);
	CHECK_CONNECTION(conn);

	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}

	list_length = 0;
	free_list_state();

	list_contents = calloc(tmp_len, sizeof(char*));
	cur = buf;
	for(tmpl = list_contents; cur != NULL; tmpl++)
	{
		*tmpl = cur->value;
		tmp = cur;
		cur = cur->next;
		free(tmp);
	}
	list_length = tmp_len;

	if(path != NULL)
	{
		list_uris = calloc(tmp_len, sizeof(char*));
		cur = fns;
		for(tmpl = list_uris; cur != NULL; tmpl++)
		{
			*tmpl = cur->value;
			tmp = cur;
			cur = cur->next;
			free(tmp);
		}
	}

	state_list = 0;
	state_list_rl_offset = 0;
	state_list_path = path;

	return 0;
}

static int
fetch_and_print_list(struct mpd_connection *conn, char* path)
{
	int ret;
	ret = fetch_list(conn, path);
	if(ret) return ret;
	else
	{
		print_list(-1);
		return 0;
	}
}

static int
fetch_and_print_list_radio()
{
	int i;

	free_list_state();

	list_contents = calloc(LIST_RADIOS_LEN, sizeof(char*));
	list_uris = calloc(LIST_RADIOS_LEN, sizeof(char*));
	for(i=0;i<LIST_RADIOS_LEN;i++)
	{
		list_contents[i] = strdup(list_radios[i]);
		list_uris[i] = strdup(list_radios_uris[i]);
	}
	list_length = LIST_RADIOS_LEN;

	state_list = 0;
	state_list_rl_offset = 0;
	print_list(-1);

	return 0;
}


static int
fetch_resume(struct mpd_connection *conn)
{
	StringList *buf, *tmp, *cur, *buf_fns, *cur_fns, *tmp_fns;
	IntList *buf_int, *tmp_int, *cur_int;
	char **tmpl;
	int tmp_len;
	struct mpd_pair* pair;
	const char *sticker_value;
	char *uri, *value;
	int played;
	size_t name_len;
	int* tmpli;
	struct mpd_entity* entity;
	const struct mpd_song* song;

	tmp_len = 0;
	cur_int = NULL;
	cur_fns = NULL;
	cur = NULL;

	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);

	mpd_send_sticker_find(conn, "song", "", "played");
	CHECK_CONNECTION(conn);

	tmp_len = 0;
	while((pair = mpd_recv_pair(conn)) != NULL)
	{
		if(!strcmp(pair->name, "file"))
		{
			uri=strdup(pair->value);
		}
		else
		{
			sticker_value=mpd_parse_sticker(pair->value, &name_len);
			if(sticker_value)
			{
				played=atoi(sticker_value);

				tmp_fns = mk_string_list();
				tmp_fns->value = uri;
				tmp_int = mk_int_list();
				tmp_int->value = played;

				if (cur_fns == NULL)
				{
					buf_fns = tmp_fns;
					buf_int = tmp_int;
				}
				else
				{
					cur_fns->next = tmp_fns;
					cur_int->next = tmp_int;
				}
				cur_fns = tmp_fns;
				cur_int = tmp_int;
				tmp_len++;
			}
			else
			{
				fprintf(stderr, "E: parsing sticker %s\n", pair->value);
				return -1;
			}
		}
		mpd_return_pair(conn, pair);
	}

	mpd_response_finish(conn);
	CHECK_CONNECTION(conn);

	if(tmp_len == 0)
	{
		buf = NULL;
	}
	else
	{
		printf("D: resume length %i\n", tmp_len);
		for(cur_fns=buf_fns, cur_int = buf_int; cur_fns != NULL; cur_fns = cur_fns->next, cur_int = cur_int->next)
		{
			mpd_send_list_meta(conn, cur_fns->value);
			CHECK_CONNECTION(conn);

			while((entity = mpd_recv_entity(conn)) != NULL)
			{
				if(mpd_entity_get_type(entity)  ==  MPD_ENTITY_TYPE_SONG)
				{
					song = mpd_entity_get_song(entity);
					value = la_mpd_song_get_filename(song);

					tmp = mk_string_list();
					tmp->value = format_resume_label(value, cur_int->value);
					printf("D: resume %s\n", tmp->value);

					if (cur == NULL)
					{
						buf = tmp;
					}
					else
					{
						cur->next = tmp;
					}
					cur = tmp;
				}
				mpd_entity_free(entity);
			}

			mpd_response_finish(conn);
			CHECK_CONNECTION(conn);
		}
	}

	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}

	list_length = 0;
	free_list_state();
	free(resume_played);

	list_contents = calloc(tmp_len, sizeof(char*));
	cur = buf;
	for(tmpl = list_contents; cur != NULL; tmpl++)
	{
		*tmpl = cur->value;
		tmp = cur;
		cur = cur->next;
		free(tmp);
	}
	resume_played = calloc(tmp_len, sizeof(int));
	cur_int = buf_int;
	for(tmpli = resume_played; cur_int != NULL; tmpli++)
	{
		*tmpli = cur_int->value;
		tmp_int = cur_int;
		cur_int = cur_int->next;
		free(tmp_int);
	}
	list_uris = calloc(tmp_len, sizeof(char*));
	cur = buf_fns;
	for(tmpl = list_uris; cur != NULL; tmpl++)
	{
		*tmpl = cur->value;
		tmp = cur;
		cur = cur->next;
		free(tmp);
	}

	list_length = tmp_len;

	state_list = 0;
	state_list_rl_offset = 0;
	state_list_path = NULL;

	return 0;
}

static int
fetch_and_print_resume(struct mpd_connection *conn)
{
	int ret;
	ret = fetch_resume(conn);
	if(ret) return ret;
	else
	{
		print_list(-1);
		return 0;
	}
}

static int
print_current_time(unsigned int played, unsigned int total)
{
	int ret;
	int len;
	char* buf;

	la_lcdPosition(0, 1);

	len = 15;
	buf = malloc(len);

	if(buf == NULL)
	{
		perror("print_current_time allocate buf");
		return -1;
	}
	
	if(total > 3600)
	{
		ret = snprintf(buf, len, "%i:%02i:%02i/%i:%02i:%02i ",
					played / 3600, (played % 3600) / 60, played % 60,
					total / 3600, (total % 3600) / 60, total % 60);
	}
	else
	{
		ret = snprintf(buf, len, "%02i:%02i/%02i:%02i    ",
					played / 60, played % 60,
					total /  60, total % 60);		
	}
	
	if(ret > 0)
	{
		la_lcdPuts(buf);
	}

	free(buf);
	return -1;
}

#define JUMP_SECS 60
static int
jump_backward_forward(struct mpd_connection* conn, bool backward)
{
	struct mpd_status *status;
	unsigned int current, total;

	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);
	
	status = mpd_run_status(conn);
	if (!status) {
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}
	
	current = mpd_status_get_elapsed_time(status);
	total  = mpd_status_get_total_time(status);

	mpd_status_free(status);

	mpd_response_finish(conn);
	CHECK_CONNECTION(conn);

	if(backward)
	{
		if(current > JUMP_SECS)
		{
			current = current - JUMP_SECS;
		}
		else
		{
			current = 0;
		}
	}
	else
	{
		if(current + JUMP_SECS < total)
		{
			current = current + JUMP_SECS;
		}
		else
		{
			current = total;
		}
	}
	if(!mpd_run_seek_pos(conn, 0, current))
	{
		LOG_ERROR("jump_backward %s", mpd_connection_get_error_message(conn));
		return -1;
	}

	print_current_time(current, total);
	
	if(do_update_played(conn))
	{
		LOG_ERROR("jump_backward %s", "do_update_played");
		return -1;
	}
	
	ignore_next_idle = true;
	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}

	return 0;
}

static int
jump_backward(struct mpd_connection* conn)
{
	return jump_backward_forward(conn, true);
}

static int
jump_forward(struct mpd_connection* conn)
{
	return jump_backward_forward(conn, false);
}

static void
sigrt_handler(int sig, siginfo_t *si, void *uc)
{
   printf("Caught signal %d with value %d\n", sig, si->si_value.sival_int);

   if(si->si_value.sival_int == LA_SIG_INACTIVE)
   {
   	   inactive_flag |= 1;
   }
   else
   {
   	  sleep_flag |= 1;
   }
   //signal(sig, SIG_IGN);
}

static void
setup_timers()
{
	struct sigevent sevp = {{0}};
	struct sigaction sa = {{0}};
	sigset_t mask;

	// signal
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = sigrt_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGRTMIN, &sa, NULL) == -1)
	{
		LOG_ERROR("sigaction: %s", strerror(errno));
		exit(-1);
	}

	// bloqué en temps normal
	sigemptyset(&mask);
	sigaddset(&mask, SIGRTMIN);
	if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1)
	{
		LOG_ERROR("sigprocmask: %s", strerror(errno));
		exit(-1);
	}

	// timers
	sevp.sigev_notify = SIGEV_SIGNAL;
	sevp.sigev_signo = SIGRTMIN;

	sevp.sigev_value.sival_int = LA_SIG_INACTIVE;
	if(timer_create(CLOCK_MONOTONIC, &sevp, &timer_inactive))
    {
    	LOG_ERROR("timer_create_inactive: %s\n", strerror(errno));
    	exit(-1);
    }

#ifdef CONFIG_SLEEP
	sevp.sigev_value.sival_int = LA_SIG_SLEEP;
	if(timer_create(CLOCK_MONOTONIC, &sevp, &timer_sleep))
    {
    	LOG_ERROR("timer_create_sleep: %s\n", strerror(errno));
    	exit(-1);
    }
#endif
}

static void reset_timers()
{
	struct itimerspec its;

	its.it_value.tv_sec = 10;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;

	printf("D: reset_timers\n");

	if(timer_settime(timer_inactive, 0, &its, NULL))
	{
		LOG_ERROR("timer_settime: %s\n", strerror(errno));
		exit(-1);
	}

#ifdef CONFIG_SLEEP
	its.it_value.tv_sec = 1200;
	if(timer_settime(timer_sleep, 0, &its, NULL))
	{
		LOG_ERROR("timer_settime: %s\n", strerror(errno));
		exit(-1);
	}
#endif

	if(asleep)
	{
		la_ecran_change_state(false);
		asleep = false;
	}
}

#define MAX_EVENTS 10
static void wait_input_async(struct mpd_connection* conn, int mpd_fd, int* control_fds, int control_fds_count)
{
	struct epoll_event ev={0}, events[MAX_EVENTS];
	int nfds, epollfd;
	int n;
	sigset_t mask;
	int ret;

	sigemptyset(&mask);

	setup_timers();

	epollfd = epoll_create1(0);
	if (epollfd == -1)
	{
		perror("epoll_create1");
		return;
	}

	ev.events = EPOLLIN;
	ev.data.fd = mpd_fd;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, mpd_fd, &ev) == -1)
	{
		perror("epoll_ctl: mpd");
		return;
	}

	for(n=0; n<control_fds_count; n++)
	{

		ev.events = EPOLLIN;
		ev.data.fd = control_fds[n];
		if (epoll_ctl(epollfd, EPOLL_CTL_ADD, control_fds[n], &ev) == -1)
		{
			perror("epoll_ctl: controls");
			return;
		}

	}

	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return;
	}

	reset_timers();

	while(true)
	{
		nfds = epoll_pwait(epollfd, events, MAX_EVENTS, -1, &mask);
		if (nfds == -1) {
			if(errno == EINTR)
			{
				if(inactive_flag)
				{
					inactive_flag = 0;
					la_ecran_change_state(true);
					asleep = true;
				}
				else if(sleep_flag)
				{
					sleep_flag = 0;
					if(do_sleep(conn))
					{
						return;
					}
				}
				else
				{
					LOG_ERROR("%s", "int by signal\n");
					return;
				}
				continue;
			}
			else
			{
				perror("epoll_pwait");
				continue;
			}
		}

		for (n = 0; n < nfds; n++)
		{
			if (events[n].data.fd == mpd_fd)
			{
				if(state == LA_STATE_PLAYING)
				{
					if(mpd_recv_idle(conn, false))
					{
						if(ignore_next_idle)
						{
							printf("D: recv_idle IGNORE status\n");
							ignore_next_idle = false;
						}
						else
						{
							printf("D: recv_idle => status\n");
							print_status(conn);
							do_update_played(conn);
							ignore_next_idle = true; // do_update_played triggers idle ?
						}
					}
					if(!mpd_send_idle(conn))
					{
						LOG_ERROR("Unable to put mpd in idle mode%s\n","");
						return;
					}
				}
			}
			else
			{
				ret = la_control_input_one(events[n].data.fd);

				if(ret < 0)
				{
					return;
				}
				else if(ret == 0)
				{
					reset_timers();
				}
			}
		}
	}
}

static int
do_play(struct mpd_connection* conn)
{

	CHECK_CONNECTION(conn);

	mpd_run_play(conn);
	CHECK_CONNECTION(conn);

	state = LA_STATE_PLAYING;

	if(do_update_played(conn))
	{
		return -1;
	}

	printf("D: do_play => status\n");
	print_status(conn);
	ignore_next_idle = true;

	return 0;
}

static int
do_playpause(Control ctrl, struct mpd_connection* conn)
{
	struct mpd_status *status;

	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);


	if(do_update_played(conn))
	{
		LOG_ERROR("do_playpause %s failed\n", "do_update_played");
		return -1;
	}

	status = mpd_run_status(conn);
	if (!status) {
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}
	mpd_response_finish(conn);
	CHECK_CONNECTION(conn);

	switch (mpd_status_get_state(status)){
	case MPD_STATE_STOP:
		mpd_run_play(conn);
		break;
	case MPD_STATE_PLAY:
	case MPD_STATE_PAUSE:
		mpd_run_toggle_pause(conn);
		break;
	default:
		fprintf(stderr, "E: unknown mpd status\n");
	}
	CHECK_CONNECTION(conn);

	mpd_status_free(status);

	if(state == LA_STATE_PLAYING)
	{
		printf("D: do_playpause => status\n");
		print_status(conn);
		ignore_next_idle = true;
	}
	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}
	return 0;
}

static int
do_sleep(struct mpd_connection* conn)
{
	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);
	CHECK_CONNECTION(conn);
	mpd_run_pause(conn, true);
	CHECK_CONNECTION(conn);

	if(do_update_played(conn))
	{
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}

	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}
	return 0;
}

#define MENU_LENGTH 6
static char* menu_contents[MENU_LENGTH] = {
	"Resume...",
	"List...",
	"Volume...",
	"Radio...",
	"Settings...",
	"Off"
};

static void
print_menu(int old_state_menu)
{
	bool need_full_refresh;

	need_full_refresh = (old_state_menu == -1)
						|| (old_state_menu / 2 != state_menu / 2);
	if(need_full_refresh)
	{
		la_lcdClear();
		la_lcdHome();
		la_lcdPutChar('>');
		la_lcdPosition(2, 0);
		la_lcdPuts(menu_contents[state_menu]);
		la_lcdPosition(2, 1);
		la_lcdPuts(menu_contents[(state_menu+1)%MENU_LENGTH]);
	}
	else
	{
		la_lcdPosition(0, old_state_menu%2);
		la_lcdPutChar(' ');
		la_lcdPosition(0, state_menu%2);
		la_lcdPutChar('>');
	}
}

static void
print_list(int old_state_list)
{
	bool need_full_refresh;

	if(old_state_list == -2)
	{
		la_lcdPosition(2, state_list%2);
		la_lcdPuts("                ");
		la_lcdPosition(2, state_list%2);
		la_lcdPuts(list_contents[state_list]+state_list_rl_offset);
	}
	else
	{
		need_full_refresh = (old_state_list == -1)
							|| (old_state_list / 2 != state_list / 2);
		if(need_full_refresh)
		{
			la_lcdClear();
			la_lcdHome();
			la_lcdPutChar('>');
			la_lcdPosition(2, 0);
			la_lcdPuts(list_contents[state_list]+state_list_rl_offset);
			la_lcdPosition(2, 1);
			if(list_length > 1)
			{
				la_lcdPuts(list_contents[(state_list+1)%list_length]);
			}
		}
		else
		{
			la_lcdPosition(0, old_state_list%2);
			la_lcdPutChar(' ');
			la_lcdPosition(0, state_list%2);
			la_lcdPutChar('>');
		}
	}
}


#define ADD_REPLACE_LENGTH 4
static char* add_replace_contents[ADD_REPLACE_LENGTH] = {
	"Replace",
	"Append",
};

static void
print_add_replace()
{
	la_lcdClear();
	la_lcdHome();
	la_lcdPutChar('>');
	la_lcdPosition(2, 0);
	la_lcdPuts(add_replace_contents[state_add_replace]);
	la_lcdPosition(2, 1);
	la_lcdPuts(add_replace_contents[(state_add_replace+1)%ADD_REPLACE_LENGTH]);
}

static void
print_settings()
{
	do_wifi_status();
}

static int
do_list_directory(struct mpd_connection* conn)
{
	state_list_path = strdup(list_contents[state_list]);
	if(state_list_path == NULL)
	{
		LOG_ERROR("%s", "Out of memory");
		return -1;
	}
	else
	{
		state_list_dir_index = state_list;
		return fetch_and_print_list(conn, state_list_path);
	}
}

static int
do_menu(Control ctrl, struct mpd_connection* conn)
{
	switch(state)
	{
	case LA_STATE_MENU:
		state = LA_STATE_PLAYING;
		CHECK_CONNECTION(conn);
		mpd_run_noidle(conn);
		CHECK_CONNECTION(conn);
		printf("D: do_menu => status\n");
		print_status(conn);
		if(!mpd_send_idle(conn))
		{
			LOG_ERROR("Unable to put mpd in idle mode%s\n","");
			return -1;
		}
		break;
	case LA_STATE_LIST:
		if(state_list_path == NULL)
		{
			state = LA_STATE_MENU;
			state_menu = 0;
			print_menu(-1);
		}
		else
		{
			if(fetch_list(conn, NULL) == -1)
			{
				return -1;
			}
			if(state_list_dir_index < list_length)
			{
				state_list = state_list_dir_index;
			}
			print_list(-1);
			return 0;
		}
	default:
		state = LA_STATE_MENU;
		state_menu = 0;
		print_menu(-1);
		break;
	}
	return 0;
}

static int
do_up(Control ctrl, struct mpd_connection* conn)
{
	int old_state_menu, old_state_list;

	switch(state)
	{

	case LA_STATE_MENU:
		old_state_menu = state_menu;
		if(state_menu == 0)
		{
			state_menu = MENU_LENGTH - 1;
		}
		else
		{
			state_menu--;
		}
		print_menu(old_state_menu);
		break;

	case LA_STATE_PLAYING:
		jump_backward(conn);
		break;

	case LA_STATE_LIST:
	case LA_STATE_RADIO:
	case LA_STATE_RESUME:
		old_state_list = state_list;
		if(state_list == 0)
		{
			state_list = list_length - 1;
		}
		else
		{
			state_list--;
		}
		print_list(old_state_list);
		break;

	case LA_STATE_ADD_REPLACE:
		if(state_add_replace == 0)
		{
			state_add_replace = 1;
		}
		else
		{
			state_add_replace = 0;
		}
		print_add_replace(false);
		break;

	case LA_STATE_VOLUME:
		return do_change_volume(conn, 48, true);

	default:
		return -1;
	}
	return 0;
}

static int
do_down(Control ctrl, struct mpd_connection* conn)
{
	int old_state_menu, old_state_list;

	switch(state)
	{
	case LA_STATE_MENU:
		old_state_menu = state_menu;
		state_menu = (state_menu + 1) % MENU_LENGTH;
		print_menu(old_state_menu);
		break;
	case LA_STATE_PLAYING:
		jump_forward(conn);
		break;
	case LA_STATE_LIST:
	case LA_STATE_RADIO:
	case LA_STATE_RESUME:
		old_state_list = state_list;
		state_list = (state_list + 1) % list_length;
		print_list(old_state_list);
		break;
	case LA_STATE_ADD_REPLACE:
		state_add_replace = (state_add_replace + 1) % ADD_REPLACE_LENGTH;
		print_add_replace();
		break;

	case LA_STATE_VOLUME:
		return do_change_volume(conn, 0, true);

	default:
		return -1;
	}
	return 0;
}

static int
do_left(Control ctrl, struct mpd_connection* conn)
{
	size_t len;
	switch(state)
	{
	case LA_STATE_LIST:
	case LA_STATE_RADIO:
	case LA_STATE_RESUME:
		len = strlen(list_contents[state_list]);
		if(state_list_rl_offset > 10)
		{
			state_list_rl_offset -= 10;
		}
		else if(state_list_rl_offset > 0)
		{
			state_list_rl_offset = 0;
		}
		else
		{
			state_list_rl_offset = len - (len % 10);
		}

		print_list(-2);
		break;

	case LA_STATE_VOLUME:
		return do_change_volume(conn, -1, false);

	default:
		return 0;
	}
	return 0;
}

static int
do_right(Control ctrl, struct mpd_connection* conn)
{
	size_t len;

	switch(state)
	{
	case LA_STATE_LIST:
	case LA_STATE_RADIO:
	case LA_STATE_RESUME:
		len = strlen(list_contents[state_list]);
		if(state_list_rl_offset < len - 10)
		{
			state_list_rl_offset += 10;
		}
		else
		{
			state_list_rl_offset = 0;
		}

		print_list(-2);
		break;

	case LA_STATE_VOLUME:
		return do_change_volume(conn, 1, false);

	default:
		return 0;
	}
	return 0;
}

static int
do_ok(Control ctrl, struct mpd_connection* conn)
{
	switch(state)
	{

	case LA_STATE_MENU:
		switch(state_menu)
		{
		case 0:
			state = LA_STATE_RESUME;
			return fetch_and_print_resume(conn);
		case 1:
			state = LA_STATE_LIST;
			return fetch_and_print_list(conn, NULL);
		case 2:
			state = LA_STATE_VOLUME;
			return fetch_and_print_volume(conn);
		case 3:
			state = LA_STATE_RADIO;
			return fetch_and_print_list_radio();
		case 4:
			state  = LA_STATE_SETTINGS;
			state_settings = 0;
			print_settings();
			break;
		case 5:
			return do_shutdown(conn);
		default:
			break;
		}
		break;

	case LA_STATE_LIST:
		if(state_list_path == NULL)
		{
			return do_list_directory(conn);
		}
		else
		{
			state = LA_STATE_ADD_REPLACE;
			state_add_replace = 0;
			print_add_replace();
		}
		break;
	case LA_STATE_RADIO:
		return do_replace_playing_with_selected(conn, true);

	case LA_STATE_ADD_REPLACE:
		return do_replace_playing_with_selected(conn, state_add_replace == 0);

	case LA_STATE_RESUME:
		return do_resume_selected(conn);

	default:
		return -1;
	}
	return 0;
}

static int
do_shutdown(struct mpd_connection* conn)
{
	char* argv[2] = { "/sbin/halt" , NULL};
	pid_t child_pid;
	int child_status;
	pid_t tpid;

	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);
	mpd_run_pause(conn, true);
	CHECK_CONNECTION(conn);

	la_lcdClear();
	la_lcdHome();
	la_lcdPuts("A Bientot...");

	child_pid = fork();
	if(child_pid == 0) {

		execvp(argv[0], argv);

		printf("E: Unknown command: %s\n", argv[0]);
		exit(0);
	}
	else {
		do {
			tpid = wait(&child_status);
		} while(tpid != child_pid);

		return child_status;
	}
	return 0;
}

static int
do_stop(Control ctrl, struct mpd_connection* conn)
{
	struct mpd_status *status;
	CHECK_CONNECTION(conn);
	mpd_run_noidle(conn);

	status = mpd_run_status(conn);
	if (!status) {
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}
	mpd_response_finish(conn);
	CHECK_CONNECTION(conn);

	switch (mpd_status_get_state(status)){
	case MPD_STATE_STOP:
		break;
	case MPD_STATE_PLAY:
	case MPD_STATE_PAUSE:
		mpd_run_stop(conn);
		break;
	default:
		fprintf(stderr, "E: do_stop unknown mpd status\n");
	}
	CHECK_CONNECTION(conn);

	mpd_status_free(status);

	la_lcdHome();
	la_lcdPuts("    MPD STOPPED    ");
	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}
	return 0;
}

static int
do_internet_status()
{
	CURL *curl;
	CURLcode res;
	char errbuf[CURL_ERROR_SIZE] = {0};

	la_lcdPosition(0, 1);
	la_lcdPuts("INTERNET...");
	la_lcdPosition(0, 12);

	curl = curl_easy_init();

	if(curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, list_radios_uris[0]);
		curl_easy_setopt(curl, CURLOPT_HEADER, 1);
		curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
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
			la_lcdPuts("KO");
			return -1;
		}
		la_lcdPuts("OK");
		return 0;
  	}else{
  		fprintf(stderr, "E: couldn't initialize curl\n");
  		la_lcdPuts("KO");
		return -1;
	}
}

static int
do_wifi_status()
{
	char* argv[4] = { "/usr/sbin/ifplugstatus" , "-q", DEFAULT_WLAN_ITF, NULL};
	pid_t child_pid;
	int child_status;
	pid_t tpid;

	la_lcdClear();
	la_lcdHome();
	la_lcdPuts("WIFI...");

	child_pid = fork();
	if(child_pid == 0) {

		execvp(argv[0], argv);

		printf("E: Unknown command: %s\n", argv[0]);
		exit(0);
	}
	else {
		do {
			tpid = wait(&child_status);
		} while(tpid != child_pid);

		if(WIFEXITED(child_status)){
			child_status = WEXITSTATUS(child_status);
			printf("I: ifplugstatus %d\n", child_status);
			la_lcdPosition(12, 0);
			if(child_status == 2){
				la_lcdPuts("OK");
				return do_internet_status();
			}else{
				la_lcdPuts("KO");
			}
		}else{
			fprintf(stderr, "E: ifplugstatus didn't terminate normally\n");
			return -1;
		}
	}
	return 0;
}

int
run()
{
	struct mpd_connection *conn = NULL;
	int mpd_fd;
	int fdControlCount;
	int* fdControls;
	int play_ok = 1; // mettre à 0 pour reprendre
	int i;

	if(la_init_controls(&fdControls, &fdControlCount))
	{
		fprintf(stderr, "E: init controls\n");
		return -1;
	}

	if(la_init_ecran())
	{
		fprintf(stderr, "E: init ecran\n");
		return -1;
	}

	if(connect_to_mpd(&conn)){
		la_exit();
		return -1;
	}

	mpd_fd = mpd_async_get_fd(mpd_connection_get_async(conn));

	printf("D: run => status\n");
	print_status(conn);

	la_on_key(LA_PLAYPAUSE, (Callback)do_playpause, conn);
	la_on_key(LA_MENU, (Callback)do_menu, conn);
	la_on_key(LA_UP, (Callback)do_up, conn);
	la_on_key(LA_DOWN, (Callback)do_down, conn);
	la_on_key(LA_LEFT, (Callback)do_left, conn);
	la_on_key(LA_RIGHT, (Callback)do_right, conn);
	la_on_key(LA_OK, (Callback)do_ok, conn);
	la_on_key(LA_STOP, (Callback)do_stop, conn);
	la_on_key(LA_EXIT, (Callback)do_stop, conn);

	if(is_stream_in_queue(conn))
	{
		for(i=0;  i<10 && ((play_ok = do_wifi_status()) != 0); i++){
			sleep(1);
		}
	}
	if(play_ok == 0)
	{
		do_play(conn);
	}

	wait_input_async(conn, mpd_fd, fdControls, fdControlCount);

	mpd_connection_free(conn);
	la_exit();
	return 0;
}

int
save_pid()
{
	FILE* f;
	f = fopen("/var/run/la.pid", "w");
	pid_t pid;
	int ret;
	if(f)
	{
		pid = getpid();
		fprintf(f, "%d\n", pid);
		ret = fclose(f);
	}
	else
	{
		perror("E: unable to save pid");
		ret = -1;
	}
	return ret;
}

int
run_in_background()
{
	int ret;
	struct timespec before;
	struct timespec after;
	bool keep_running = true;

	ret = daemon(0, 1);
	if(ret)
	{
		perror("E: couldn't daemonize");
		return ret;
	}
	else
	{
		if(!freopen("/dev/null", "r", stdin)
			|| !freopen(DEFAULT_LOG_FILE, "w", stdout)
			|| !freopen(DEFAULT_ERROR_FILE, "a", stderr))
		{
			perror("E: unable to redirect output");
			return -1;
		}
		setlinebuf(stdout);
		setlinebuf(stderr);
		printf("I: I'm a daemon now\n");
		ret = save_pid();
		if(ret)
		{
			return ret;
		}
		else
		{
			while(keep_running)
			{
				if(clock_gettime(CLOCK_MONOTONIC, &before))
				{
					perror("E: unable to get time before run");
					return -1;
				}
				ret = run();
				if(ret)
				{
					sleep(5);
					if(clock_gettime(CLOCK_MONOTONIC, &after))
					{
						perror("E: unable to get time after run");
						return -1;
					}

					if((after.tv_sec - before.tv_sec) < 60)
					{
						keep_running = false;
						fprintf(stderr,
							"E: run() exited too quickly (%lus), giving up\n",
							(after.tv_sec - before.tv_sec));
					}
				}
			}
			return ret;
		}
	}
}

int
usage(int argc, char** argv, int code)
{
	const char* progname;
	if(argc > 0)
	{
		progname = argv[0];
	}
	else
	{
		progname = "la";
	}
	printf(
		"Usage: %s [-b]\n"
		"LecteurAudio: a homemade media player\n"
		"  -b, --background    daemonize\n"
		"  -h, --help          print this help\n", progname);
	return code;
}

int
main(int argc, char ** argv){
	if(argc > 1 &&
		(!strcmp("-h", argv[1]) || !strcmp("--help", argv[1])))
	{
		return usage(argc, argv, 0);
	}
	if(argc == 2 &&
		(!strcmp("-b", argv[1]) || !strcmp("--background", argv[1])))
	{
		return run_in_background();
	}
	if(argc > 1)
	{
		return usage(argc, argv, -1);
	}
	return run();
}