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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include "ecran.h"
#include "controles.h"

#define BRIGHT 1
#define RED 31
#define GREEN 32
#define YELLOW 33
#define BG_BLACK 40
#define COLOR_CODE 0x1B

typedef enum {
	LA_STATE_PLAYING, LA_STATE_MENU, LA_STATE_LIST, LA_STATE_ADD_REPLACE, LA_STATE_VOLUME, LA_STATE_SETTINGS
} LaState;

static void print_list();
static void print_settings();
static int do_shutdown();
static int print_status(struct mpd_connection *conn);

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

int state_add_replace;

int state_settings;

static char log_buffer[16];

#define LOG_INFO(x, ...) {printf("    [info]" x "\n", __VA_ARGS__);}
#define LOG_WARNING(x, ...) \
{\
	fprintf(stderr, "%c[%d;%d;%dm[WARNING](%s:%d) %s : " x "\n", COLOR_CODE, BRIGHT, YELLOW, BG_BLACK, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);\
	printf("%c[%dm", 0x1B, 0);\
}

#define LOG_ERROR(x, ...) \
{\
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
connect_to_mpd(struct mpd_connection **conn)
{
	*conn = mpd_connection_new(NULL, 0, 30000);
	if (*conn == NULL) {
		LOG_ERROR("%s", "Out of memory");
		return -1;
	}

	if (mpd_connection_get_error(*conn) != MPD_ERROR_SUCCESS) {
		LOG_ERROR("%s", mpd_connection_get_error_message(*conn));
		mpd_connection_free(*conn);
		*conn = NULL;
		return -1;
	}
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
 		if(!mpd_run_clear(conn))
 		{
			LOG_ERROR("%s", mpd_connection_get_error_message(conn));
			return -1;
 		}
 	}

	if(!mpd_run_add(conn, file))
	{
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}

	if(!mpd_run_play(conn))
	{
		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
		return -1;
	}

	state = LA_STATE_PLAYING;
	print_status(conn);

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

static int
fetch_and_print_list_artists(struct mpd_connection *conn)
{
	struct mpd_pair *pair;
	StringList *buf, *tmp, *cur;
	char** tmpl;
	int tmp_len,tmp_list_len;
	
	tmp_len = 0;
	cur = NULL;
	
 	CHECK_CONNECTION(conn);
 	mpd_run_noidle(conn);

	mpd_search_db_tags(conn, MPD_TAG_ARTIST);
	mpd_search_commit(conn);
	CHECK_CONNECTION(conn);

	tmp_len = 0;
	while ((pair = mpd_recv_pair_tag(conn, MPD_TAG_ARTIST)) != NULL) {
		if (cur == NULL)
		{
			buf = tmp = mk_string_list();
			tmp->value = strdup(pair->value);
		}
		else
		{
			tmp = mk_string_list();
			tmp->value = strdup(pair->value);
			cur->next = tmp;
		}
		cur = tmp;
		tmp_len++;
		mpd_return_pair(conn, pair);
	}

	mpd_response_finish(conn);
	CHECK_CONNECTION(conn);

	tmp_list_len = list_length;
	list_length = 0;
	for(tmpl = list_contents; tmp_list_len != 0; tmpl++)
	{
		free(*tmpl);
		tmp_list_len--;
	}
	free(list_contents);
	
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
	
	state_list = 0;
	state_list_rl_offset = 0;
	print_list();
	
	return 0;
}

static char*
la_mpd_song_get_filename(const struct mpd_song* song)
{
	char* value;
	char* tmp;
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
	return strdup(value);
}

static int
fetch_and_print_list(struct mpd_connection *conn, char* path)
{
	StringList *buf, *tmp, *cur, *fns, *cur_fns, *tmp_fns;
	char **tmpl, **fns_tmpl;
	int tmp_len,tmp_list_len;
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
 		printf("D: fetch_and_print_list(%s)\n", path);
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
	print_list();
	
	return 0;
}

static int
jump_backward(struct mpd_connection* conn)
{
	LOG_ERROR("TODO: %s","jump_backward");
	return 0;
}

static int
jump_forward(struct mpd_connection* conn)
{
	LOG_ERROR("TODO: %s","jump_forward");
	return 0;
}

#define MAX_EVENTS 10
static void wait_input_async(struct mpd_connection* conn, int mpd_fd, int* control_fds, int control_fds_count)
{
	struct epoll_event ev, events[MAX_EVENTS];
	int nfds, epollfd;
	int n;
	
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
	
	while(true)
	{
		nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			return;
		}
		
		for (n = 0; n < nfds; n++)
		{
			if (events[n].data.fd == mpd_fd)
			{
				if(state == LA_STATE_PLAYING)
				{
					if(mpd_recv_idle(conn, false))
					{
						print_status(conn);
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
				if(la_control_input_one(events[n].data.fd))
				{
					return;
				}
			}
		}
	}
}

static int do_playpause(Control ctrl, struct mpd_connection* conn){
 	CHECK_CONNECTION(conn);
 	mpd_run_noidle(conn);
	mpd_run_toggle_pause(conn);
 	CHECK_CONNECTION(conn);
 	if(state == LA_STATE_PLAYING)
 	{
		print_status(conn);
	}
	if(!mpd_send_idle(conn))
	{
		LOG_ERROR("Unable to put mpd in idle mode%s\n","");
		return -1;
	}
	return 0;
}

#define MENU_LENGTH 4
static char* menu_contents[MENU_LENGTH] = {
	"List...",
	"Volume...",
	"Settings...",
	"Off"
};

static void
print_menu()
{
	la_lcdClear();
	la_lcdHome();
	la_lcdPutChar('>');
	la_lcdPosition(2, 0);
	la_lcdPuts(menu_contents[state_menu]);
	la_lcdPosition(2, 1);
	la_lcdPuts(menu_contents[(state_menu+1)%MENU_LENGTH]);
}

static void
print_list()
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
	LOG_ERROR("E: %s", "print_settings");
}

static int do_list_directory(struct mpd_connection* conn)
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
			print_menu();
		}
		else
		{
			if(fetch_and_print_list(conn, NULL) == -1)
			{
				return -1;
			}
			if(state_list_dir_index < list_length)
			{
				state_list = state_list_dir_index;
				print_list();
				return 0;
			}
		}
	default:
		state = LA_STATE_MENU;
		state_menu = 0;
		print_menu();
		break;
	}
	return 0;
}

static int
do_up(Control ctrl, struct mpd_connection* conn)
{
	switch(state)
	{

	case LA_STATE_MENU:
		if(state_menu == 0)
		{
			state_menu = MENU_LENGTH - 1;
		}
		else
		{
			state_menu--;
		}
		print_menu();
		break;

	case LA_STATE_PLAYING:
		jump_backward(conn);
		break;

	case LA_STATE_LIST:
		if(state_list == 0)
		{
			state_list = list_length - 1;
		}
		else
		{
			state_list--;
		}
		print_list();
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
		print_add_replace();
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
	switch(state)
	{
	case LA_STATE_MENU:
		state_menu = (state_menu + 1) % MENU_LENGTH;
		print_menu();
		break;
	case LA_STATE_PLAYING:
		jump_forward(conn);
		break;
	case LA_STATE_LIST:
		state_list = (state_list + 1) % list_length;
		print_list();
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
		len = strlen(list_contents[state_list]);
		if(state_list_rl_offset > 14)
		{
			state_list_rl_offset -= 10;
		}
		else if(state_list_rl_offset > 0)
		{
			state_list_rl_offset--;
		}
		else
		{
			state_list_rl_offset = len - 16;
			if(state_list_rl_offset < 0){
				state_list_rl_offset = 0;
			}
		}

		print_list();
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
		len = strlen(list_contents[state_list]);
		if(state_list_rl_offset < len - 28)
		{
			state_list_rl_offset += 10;
		}
		else if(state_list_rl_offset < len - 14)
		{
			state_list_rl_offset++;
		}
		else
		{
			state_list_rl_offset = 0;
		}

		print_list();
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
			state = LA_STATE_LIST;
			return fetch_and_print_list(conn, NULL);
		case 1:
			state  = LA_STATE_VOLUME;
			return fetch_and_print_volume(conn);
		case 2:
			state  = LA_STATE_SETTINGS;
			state_settings = 0;
			print_settings();
		case 3:
			return do_shutdown();
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

	case LA_STATE_ADD_REPLACE:
		return do_replace_playing_with_selected(conn, state_add_replace == 0);

	default:
		return -1;
	}
	return 0;
}

static int
do_shutdown()
{
	return -1;
}


int
main(int argc, char ** argv)
{
	struct mpd_connection *conn = NULL;
	int mpd_fd;
	int fdControlCount;
	int* fdControls;

	if(la_init_controls(&fdControls, &fdControlCount))
	{
		fprintf(stderr, "E: init controls\n");
		return -1;
	}
	
	la_init_ecran();
	la_lcdHome();
	la_lcdPutChar('H');
	la_lcdPutChar('E');

	if(connect_to_mpd(&conn)){
		la_exit();
		return -1;
	}
	
	mpd_fd = mpd_async_get_fd(mpd_connection_get_async(conn));

	print_status(conn);

	la_on_key(LA_PLAYPAUSE, (Callback)do_playpause, conn);
	la_on_key(LA_MENU, (Callback)do_menu, conn);
	la_on_key(LA_UP, (Callback)do_up, conn);
	la_on_key(LA_DOWN, (Callback)do_down, conn);
	la_on_key(LA_LEFT, (Callback)do_left, conn);
	la_on_key(LA_RIGHT, (Callback)do_right, conn);
	la_on_key(LA_OK, (Callback)do_ok, conn);
	
	
	wait_input_async(conn, mpd_fd, fdControls, fdControlCount);

	mpd_connection_free(conn);
	la_exit();
	return 0;
}
