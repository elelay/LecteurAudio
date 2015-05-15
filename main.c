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
	LA_STATE_PLAYING, LA_STATE_MENU, LA_STATE_LIST, LA_STATE_VOLUME, LA_STATE_SETTINGS
} LaState;

static void print_list();
static void print_settings();
static int do_shutdown();

LaState state;

int state_menu;

char** list_contents = NULL;
int list_length;
int state_list;

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

#define START_TEST(description, method, ...) \
{\
	printf("[Start Test] " description "\n");\
	if (method(__VA_ARGS__) < 0)\
		printf("%c[%d;%d;%dm[End Test: ERROR]\n", COLOR_CODE, BRIGHT, RED, BG_BLACK);\
	else\
		printf("%c[%d;%d;%dm[End Test: OK]\n", COLOR_CODE, BRIGHT, GREEN, BG_BLACK);\
	printf("%c[%dm", 0x1B, 0);\
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
do_replace_playing_with_selected(struct mpd_connection* conn)
{
	const char* file = "";
	
 	CHECK_CONNECTION(conn);
 	mpd_run_noidle(conn);

	if(!mpd_run_add(conn, file))
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
print_status(struct mpd_connection *conn)
{
	struct mpd_status *status;
	struct mpd_song *song;
	char *value;

	la_lcdClear();

	song = mpd_run_current_song(conn);
	if (song != NULL) {

		if((value = (char*)mpd_song_get_tag(song, MPD_TAG_TITLE, 0)) != NULL){
			la_lcdPosition(0,0);
			la_lcdPuts(value);
		}
		if((value = (char*)mpd_song_get_tag(song, MPD_TAG_ARTIST, 0)) != NULL){
			la_lcdPosition(0,1);
			la_lcdPuts(value);
		}

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

// static int
// test_currentsong(struct mpd_connection *conn)
// {
// 	struct mpd_song *song;

// 	song = mpd_run_current_song(conn);
// 	if (song != NULL) {
// 		print_song(song);

// 		mpd_song_free(song);
// 	}

// 	mpd_response_finish(conn);
// 	CHECK_CONNECTION(conn);

// 	return 0;
// }


// static int
// test_list_status_currentsong(struct mpd_connection *conn)
// {
// 	struct mpd_status *status;
// 	const struct mpd_song *song;
// 	struct mpd_entity *entity;

// 	mpd_command_list_begin(conn, true);
// 	mpd_send_status(conn);
// 	mpd_send_current_song(conn);
// 	mpd_command_list_end(conn);

// 	CHECK_CONNECTION(conn);

// 	status = mpd_recv_status(conn);
// 	if (!status) {
// 		LOG_ERROR("%s", mpd_connection_get_error_message(conn));
// 		return -1;
// 	}
// 	if (mpd_status_get_error(status)) {
// 		LOG_WARNING("status error: %s", mpd_status_get_error(status));
// 	}

// 	print_status(status);
// 	mpd_status_free(status);

// 	mpd_response_next(conn);

// 	entity = mpd_recv_entity(conn);
// 	if (entity) {
// 		if (mpd_entity_get_type(entity) != MPD_ENTITY_TYPE_SONG) {
// 			LOG_ERROR("entity doesn't have the expected type (song)i :%d",
// 				  mpd_entity_get_type(entity));
// 			mpd_entity_free(entity);
// 			return -1;
// 		}

// 		song = mpd_entity_get_song(entity);

// 		print_song(song);

// 		mpd_entity_free(entity);
// 	}

// 	mpd_response_finish(conn);
// 	CHECK_CONNECTION(conn);

// 	return 0;
// }

// static int
// test_lsinfo(struct mpd_connection *conn, const char *path)
// {
// 	struct mpd_entity *entity;

// 	mpd_send_list_meta(conn, path);
// 	CHECK_CONNECTION(conn);

// 	while ((entity = mpd_recv_entity(conn)) != NULL) {
// 		const struct mpd_song *song;
// 		const struct mpd_directory *dir;
// 		const struct mpd_playlist *pl;

// 		switch (mpd_entity_get_type(entity)) {
// 		case MPD_ENTITY_TYPE_UNKNOWN:
// 			printf("Unknown type\n");
// 			break;

// 		case MPD_ENTITY_TYPE_SONG:
// 			song = mpd_entity_get_song(entity);
// 			print_song (song);
// 			break;

// 		case MPD_ENTITY_TYPE_DIRECTORY:
// 			dir = mpd_entity_get_directory(entity);
// 			printf("directory: %s\n", mpd_directory_get_path(dir));
// 			break;

// 		case MPD_ENTITY_TYPE_PLAYLIST:
// 			pl = mpd_entity_get_playlist(entity);
// 			LOG_INFO("playlist: %s", mpd_playlist_get_path(pl));
// 			break;
// 		}

// 		mpd_entity_free(entity);
// 	}

// 	mpd_response_finish(conn);
// 	CHECK_CONNECTION(conn);

// 	return 0;
// }

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
fetch_and_print_list(struct mpd_connection *conn)
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
	la_lcdPutChar('0'+state_list);
	la_lcdPosition(2, 0);
	la_lcdPuts(list_contents[state_list]);
	la_lcdPosition(2, 1);
	la_lcdPuts(list_contents[(state_list+1)%list_length]);
}

static void
print_settings()
{
	LOG_ERROR("E: %s", "print_settings");
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
	default:
		return -1;
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
			return fetch_and_print_list(conn);
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
		return do_replace_playing_with_selected(conn);
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
	la_on_key(LA_OK, (Callback)do_ok, conn);
	
	
	wait_input_async(conn, mpd_fd, fdControls, fdControlCount);

	mpd_connection_free(conn);
	la_exit();
	return 0;
}
