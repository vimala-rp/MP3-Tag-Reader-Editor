#include <stdio.h>
#include <string.h>
#include "header.h"

int main(int argc, char *argv[])
{
    char *frame_id = NULL;
    int result;

    if (argc < 2)
    {
        printf("Error: Please provide a command.\n");
        printf("Use --help for instructions.\n");
        return 1;
    }

    /* Display help instructions. */
    if (strcmp(argv[1], "--help") == 0)
    {
        if (argc != 2)
        {
            printf("Usage: ./a.out --help\n");
            return 1;
        }

        help_mode();
    }

    /* Display metadata from the specified MP3 file. */
    else if (strcmp(argv[1], "-v") == 0)
    {
        if (argc != 3)
        {
            printf("Usage: ./a.out -v filename.mp3\n");
            return 1;
        }

        result = view_mode(argv[2]);

        if (result != 0)
            return 1;
    }

    /* Edit the requested ID3 frame. */
    else if (strcmp(argv[1], "-e") == 0)
    {
        if (argc != 5)
        {
            printf("Usage: ./a.out -e option filename.mp3 new_value\n");
            return 1;
        }

        /* Map command-line options to ID3 frame identifiers. */
        if (strcmp(argv[2], "-t") == 0)
            frame_id = "TIT2";       /* Title */

        else if (strcmp(argv[2], "-a") == 0)
            frame_id = "TPE1";       /* Artist */

        else if (strcmp(argv[2], "-A") == 0)
            frame_id = "TALB";       /* Album */

        else if (strcmp(argv[2], "-y") == 0)
            frame_id = "TYER";       /* Year */

        else if (strcmp(argv[2], "-g") == 0)
            frame_id = "TCON";       /* Genre */

        else if (strcmp(argv[2], "-c") == 0)
            frame_id = "COMM";       /* Comment */

        else
        {
            printf("Error: Unknown tag option.\n");
            printf("Use --help for valid options.\n");
            return 1;
        }

        result = edit_frame(frame_id, argv[3], argv[4]);

        if (result != 0)
            return 1;
    }

    else
    {
        printf("Error: Invalid command.\n");
        printf("Use --help for instructions.\n");
        return 1;
    }

    return 0;
}

