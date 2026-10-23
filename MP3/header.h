#ifndef HEADER_H
#define HEADER_H

#include <stdio.h>
#include <string.h>

/* Display the available commands and usage information. */
void help_mode(void);

/* Display metadata from the specified MP3 file.
 * Returns 0 on success and nonzero on failure.
 */
int view_mode(const char *filename);

/* Edit the specified ID3 frame in the MP3 file.
 * Returns 0 on success and nonzero on failure.
 */
int edit_frame(const char *frame_id,
               const char *filename,
               const char *new_data);

#endif

