
#include "controles.h"
#include "ecran.h"

#include <ncurses.h>
#include <stdbool.h>
#include <unistd.h>

static int _la_control_input_one();


static WINDOW* win;
static Callback callbacks[LA_CONTROL_LENGTH] = {0};
static void* callback_params[LA_CONTROL_LENGTH] = {0};

static
WINDOW *create_newwin(int height, int width, int starty, int startx)
{	WINDOW *local_win;

	local_win = newwin(height, width, starty, startx);
	box(local_win, 0 , 0);		/* 0, 0 gives default characters 
					 * for the vertical and horizontal
					 * lines			*/
	wrefresh(local_win);		/* Show that box 		*/

	return local_win;
}


int la_init_ecran(){
	initscr();
	cbreak();
	keypad(stdscr, TRUE);
	noecho();
	curs_set(0);
	printw("   P : PLAYPAUSE \n"
	 	   "   U : UP        \n"
	 	   "   D : DOWN      \n"
	 	   "   M : MENU      \n"
	 	   "   O : OK        \n"
	 	   "  (Q : QUIT)     \n"
	 	   );
	refresh();
	win = create_newwin(4, 18, 6, 0);
	//mvwprintw(win, 1,1, "HELLO");
	wrefresh(win);
	return 0;
}

void la_exit(){
	wborder(win, ' ', ' ', ' ',' ',' ',' ',' ',' ');
	wrefresh(win);
	delwin(win);
	endwin();
}

void la_lcdHome(){
	wmove(win, 1, 1);
}

void la_lcdClear() {
	wclear(win);
	box(win, 0 , 0);
	wrefresh(win);
}
void la_lcdPosition(int col, int row){
	wmove(win, row+1, col+1);
}
void la_lcdPutChar(uint8_t c){
	int x, y;
	getyx(win, y, x);
	waddch(win, c);
	wmove(win, x,y);
	wrefresh(win);
}
void la_lcdPuts(char* str){
	int x, y, l;
	getyx(win, y, x);
	l = 17 - x;
	waddnstr(win, str, l);
	wmove(win, y,x);
	wrefresh(win);
}

static int emul_fdControls[1] = { STDIN_FILENO };

int la_init_controls(int** fdControls, int* fdControlCount)
{
	
	*fdControls = emul_fdControls;
	*fdControlCount = 1;
	return 0;
}

void la_on_key(Control ctrl, Callback fn, void* param) {
	if(ctrl>=0 && ctrl < LA_CONTROL_LENGTH){
		callbacks[ctrl] = fn;
		callback_params[ctrl] = param;
	}
}

void la_wait_input(){
	int in;
	while(true){
		in = _la_control_input_one();
		if(in == 'Q' || in == 'q' || in == -1){
			return;
		}
	}
}

int
la_control_input_one(int)
{
	int in;
	in = _la_control_input_one();
	if(in == 'Q' || in == 'q')
	{
		return 1;
	}
	else
	{
		return in;
	}
}

static int
_la_control_input_one()
{
	int in;
	Control c;
	in = getch();
	switch(in)
	{
		case 'p':
		case 'P':
		case ' ':
			c = LA_PLAYPAUSE;
			break;
		case 'u':
		case 'U':
		case KEY_UP:
			c = LA_UP;
			break;
		case 'd':
		case 'D':
		case KEY_DOWN:
			c = LA_DOWN;
			break;
		case 'm':
		case 'M':
			c = LA_MENU;
			break;
		case 'o':
		case 'O':
		case 10:
			c = LA_OK;
			break;
		default:
			return in;
	}
	if(callbacks[c] != NULL)
	{
		return callbacks[c](c, callback_params[c]);
	}
	return 0;
}
