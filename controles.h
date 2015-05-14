#ifndef CONTROLES_H
#define CONTROLES_H

typedef enum {
	LA_PLAYPAUSE, LA_UP, LA_DOWN, LA_MENU, LA_OK, LA_CONTROL_LENGTH
} Control;

typedef int (*Callback)(Control control, void* param);

int la_init_controls(int** fdControls, int* fdControlCount);
void la_on_key(Control, Callback fn, void* param);
void la_wait_input();
int la_control_input_one();

#endif        //  #ifndef CONTROLES_H
