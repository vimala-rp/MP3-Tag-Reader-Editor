#include "header.h"

/* Display the available commands for viewing and editing MP3 tags. */
void help_mode(void)
{
    printf("--------------------------------------------------\n");
    printf("              MP3 TAG READER HELP\n");
    printf("--------------------------------------------------\n");

    /* Command for displaying MP3 metadata. */
    printf("View MP3 details:\n");
    printf("./a.out -v sample.mp3\n\n");

    /* Commands for editing individual ID3 frames. */
    printf("Edit MP3 details:\n");
    printf("./a.out -e -t sample.mp3 \"New Title\"\n");
    printf("./a.out -e -a sample.mp3 \"New Artist\"\n");
    printf("./a.out -e -A sample.mp3 \"New Album\"\n");
    printf("./a.out -e -y sample.mp3 \"2026\"\n");
    printf("./a.out -e -g sample.mp3 \"New Genre\"\n");
    printf("./a.out -e -c sample.mp3 \"New Comment\"\n\n");

    printf("Options:\n");
    printf("-t : Edit title\n");
    printf("-a : Edit artist\n");
    printf("-A : Edit album\n");
    printf("-y : Edit year\n");
    printf("-g : Edit genre\n");
    printf("-c : Edit comment\n");

    printf("--------------------------------------------------\n");
}

