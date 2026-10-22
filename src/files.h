#ifndef FILES_H
#define FILES_H

#include <stdbool.h>
#include <stdint.h>

/* Saving and opening the code: Ctrl+S asks for a name on the status line,
 * Ctrl+O shows the .str and .txt files of the current folder (the USB drive
 * the program was started from) over the code window. */

/* Where the list and the prompt go on the screen. */
void files_init(uint8_t x, uint8_t y, uint8_t cols, uint8_t rows, uint8_t prompt_y);

void files_save_prompt(void); /* Ctrl+S */
void files_open_list(void);   /* Ctrl+O */

/* While one of them is up, the keys go here instead of to the editor. */
bool files_active(void);
void files_key(uint8_t byte);
void files_end_keys(void);
void files_cancel(void); /* Esc */

/* The name the code was last saved as or opened from. */
const char *files_name(void);

/* Changes since the last save or open; set by the program on each edit. */
extern bool files_modified;

/* The program provides these: a message on the status line, and word that
 * the code was replaced by a file (old positions no longer mean anything). */
void ui_message(const char *msg, uint8_t fg);
void ui_text_replaced(void);

#endif
