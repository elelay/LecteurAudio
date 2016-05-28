#ifndef CONTROLES_H
#define CONTROLES_H

typedef enum {
	LA_PLAYPAUSE, LA_UP, LA_DOWN, LA_LEFT, LA_RIGHT, LA_MENU, LA_OK, LA_STOP, LA_EXIT, LA_CONTROL_LENGTH
} Control;

typedef int (*Callback)(Control control, void* param);

int la_init_controls(int** fdControls, int* fdControlCount);
void la_on_key(Control, Callback fn, void* param);
void la_wait_input();
int la_control_input_one(int fd);
extern char* DEBUG_CONTROLS[LA_CONTROL_LENGTH];

#endif        //  #ifndef CONTROLES_H
