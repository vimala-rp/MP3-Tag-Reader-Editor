#include "header.h"
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>

/* Convert a 4-byte synchsafe integer into a normal integer. */
static unsigned long get_tag_size(const unsigned char *bytes)
{
    return ((unsigned long)bytes[0] << 21) |
           ((unsigned long)bytes[1] << 14) |
           ((unsigned long)bytes[2] << 7) |
           (unsigned long)bytes[3];
}

/* ID3v2.3 frame sizes are stored in big-endian format. */
static unsigned long get_frame_size(const unsigned char *bytes)
{
    return ((unsigned long)bytes[0] << 24) |
           ((unsigned long)bytes[1] << 16) |
           ((unsigned long)bytes[2] << 8) |
           (unsigned long)bytes[3];
}

/* Copy single-byte encoded text into a null-terminated string. */
static void copy_text(const unsigned char *data, unsigned long size,
                      char *destination, size_t destination_size)
{
    unsigned long i;
    size_t j = 0;

    if (destination_size == 0)
        return;

    destination[0] = '\0';

    /* Encoding 0 represents ISO-8859-1 text. */
    if (size == 0 || data[0] != 0)
        return;

    for (i = 1; i < size && j < destination_size - 1; i++)
    {
        if (data[i] == '\0')
            break;

        destination[j++] = (char)data[i];
    }

    destination[j] = '\0';
}

/* Read and display metadata from an ID3v2.3.0 MP3 file. */
int view_mode(const char *filename)
{
    FILE *fp = NULL;
    unsigned char header[10];

    char title[100] = "";
    char artist[100] = "";
    char album[100] = "";
    char year[20] = "";
    char genre[100] = "";
    char comment[200] = "";

    int result = 1;
    unsigned long tag_size;
    long tag_end;
    long file_size;

    if (filename == NULL || filename[0] == '\0')
    {
        fprintf(stderr, "Error: Invalid filename.\n");
        return 1;
    }

    fp = fopen(filename, "rb");

    if (fp == NULL)
    {
        fprintf(stderr, "Error: Unable to open MP3 file '%s'.\n", filename);
        return 1;
    }

    /* Read the complete 10-byte ID3 header. */
    if (fread(header, 1, sizeof(header), fp) != sizeof(header))
    {
        fprintf(stderr, "Error: Unable to read ID3 header.\n");
        goto cleanup;
    }

    if (memcmp(header, "ID3", 3) != 0)
    {
        fprintf(stderr, "Error: ID3 header not found.\n");
        goto cleanup;
    }

    /* This implementation supports ID3v2.3.0 only. */
    if (header[3] != 3 || header[4] != 0)
    {
        fprintf(stderr, "Error: Only ID3v2.3.0 is supported.\n");
        goto cleanup;
    }

    /* Reject unsupported extended-header and unsynchronisation flags. */
    if ((header[5] & 0xC0) != 0)
    {
        fprintf(stderr, "Error: Unsupported ID3 header flags.\n");
        goto cleanup;
    }

    /* Validate the four synchsafe tag-size bytes. */
    if (((header[6] | header[7] | header[8] | header[9]) & 0x80) != 0)
    {
        fprintf(stderr, "Error: Invalid ID3 tag size.\n");
        goto cleanup;
    }

    tag_size = get_tag_size(&header[6]);

    /* Prevent the tag-end calculation from overflowing. */
    if (tag_size > (unsigned long)(LONG_MAX - 10L))
    {
        fprintf(stderr, "Error: ID3 tag size is too large.\n");
        goto cleanup;
    }

    tag_end = 10L + (long)tag_size;

    /* Check that the declared tag fits inside the actual file. */
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Error: Unable to determine MP3 file size.\n");
        goto cleanup;
    }

    file_size = ftell(fp);

    if (file_size < 0 || tag_end > file_size)
    {
        fprintf(stderr, "Error: ID3 tag extends beyond the file.\n");
        goto cleanup;
    }

    /*
     * Skip the 10-byte ID3 header.
     * The first frame begins at byte offset 10.
     */
    if (fseek(fp, 10L, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Unable to locate ID3 frames.\n");
        goto cleanup;
    }

    printf("\n");
    printf("-----------------------------------------------\n");
    printf("              MP3 TAG INFORMATION\n");
    printf("-----------------------------------------------\n");
    printf("Major Version : %u\n", header[3]);
    printf("Revision      : %u\n", header[4]);
    printf("Tag Size      : %lu bytes\n", tag_size);

    /*
     * Each ID3v2.3 frame has a 10-byte header followed by its data.
     * Stop when padding is reached or the tag boundary is reached.
     */
    while (1)
    {
        char frame_id[5];
        unsigned char frame_header[6];
        unsigned char *data = NULL;

        unsigned long frame_size;
        size_t data_size;
        long current_position = ftell(fp);
        long remaining;

        if (current_position < 0)
        {
            fprintf(stderr, "Error: Unable to determine frame position.\n");
            goto cleanup;
        }

        if (current_position >= tag_end)
            break;

        remaining = tag_end - current_position;

        if (remaining < 10)
        {
            fprintf(stderr, "Error: Incomplete ID3 frame header.\n");
            goto cleanup;
        }

        /* Read the 4-byte frame identifier, such as TIT2 or TPE1. */
        if (fread(frame_id, 1, 4, fp) != 4)
        {
            fprintf(stderr, "Error: Unable to read frame ID.\n");
            goto cleanup;
        }

        frame_id[4] = '\0';

        /* A zero byte indicates padding, where no more frames follow. */
        if (frame_id[0] == '\0')
            break;

        /*
         * Read the remaining 6 bytes of the frame header:
         * 4 bytes for frame size and 2 bytes for frame flags.
         */
        if (fread(frame_header, 1, 6, fp) != 6)
        {
            fprintf(stderr, "Error: Unable to read frame header.\n");
            goto cleanup;
        }

        /* The first 4 bytes contain the frame size. */
        frame_size = get_frame_size(frame_header);

        if (frame_size > (unsigned long)(remaining - 10))
        {
            fprintf(stderr, "Error: Frame exceeds the ID3 tag boundary.\n");
            goto cleanup;
        }

        /* Ensure the frame size can safely be converted to size_t. */
        if (frame_size > SIZE_MAX)
        {
            fprintf(stderr, "Error: Frame size is too large.\n");
            goto cleanup;
        }

        data_size = (size_t)frame_size;

        /* Allocate memory for the complete frame data. */
        data = malloc(data_size == 0 ? 1 : data_size);

        if (data == NULL)
        {
            fprintf(stderr, "Error: Memory allocation failed.\n");
            goto cleanup;
        }

        /* Read the complete frame data, including its encoding byte. */
        if (fread(data, 1, data_size, fp) != data_size)
        {
            free(data);
            fprintf(stderr, "Error: Unable to read frame data.\n");
            goto cleanup;
        }

        /* Extract the supported metadata fields. */
        if (strcmp(frame_id, "TIT2") == 0)
        {
            copy_text(data, frame_size, title, sizeof(title));
        }
        else if (strcmp(frame_id, "TPE1") == 0)
        {
            copy_text(data, frame_size, artist, sizeof(artist));
        }
        else if (strcmp(frame_id, "TALB") == 0)
        {
            copy_text(data, frame_size, album, sizeof(album));
        }
        else if (strcmp(frame_id, "TYER") == 0)
        {
            copy_text(data, frame_size, year, sizeof(year));
        }
        else if (strcmp(frame_id, "TCON") == 0)
        {
            copy_text(data, frame_size, genre, sizeof(genre));
        }
        else if (strcmp(frame_id, "COMM") == 0 && frame_size >= 4)
        {
            /*
             * COMM contains an encoding byte, a 3-byte language,
             * a description, and the actual comment.
             * This handles encoding 0 with an empty description.
             */
            if (data[0] == 0)
            {
                unsigned long i = 4;
                size_t j = 0;

                while (i < frame_size && data[i] != '\0')
                    i++;

                if (i < frame_size)
                {
                    i++;

                    while (i < frame_size && j < sizeof(comment) - 1)
                    {
                        comment[j++] = (char)data[i++];
                    }

                    comment[j] = '\0';
                }
            }
        }

        free(data);
    }

    if (ferror(fp))
    {
        fprintf(stderr, "Error: Failed while reading MP3 metadata.\n");
        goto cleanup;
    }

    printf("\n");
    printf("-----------------------------------------------\n");
    printf("                  MP3 DETAILS\n");
    printf("-----------------------------------------------\n");
    printf("TITLE   : %s\n", title);
    printf("ARTIST  : %s\n", artist);
    printf("ALBUM   : %s\n", album);
    printf("YEAR    : %s\n", year);
    printf("GENRE   : %s\n", genre);
    printf("COMMENT : %s\n", comment);
    printf("-----------------------------------------------\n");

    result = 0;

cleanup:
    fclose(fp);
    return result;
}

