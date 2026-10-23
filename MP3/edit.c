#include "header.h"
#include <stdlib.h>
#include <stdint.h>

/*
 * Convert the four synchsafe bytes in the ID3 header into an integer.
 * ID3v2.3 stores the overall tag size using 7 bits per byte.
 */
static unsigned long get_tag_size(const unsigned char *bytes)
{
    return ((unsigned long)bytes[0] << 21) |
           ((unsigned long)bytes[1] << 14) |
           ((unsigned long)bytes[2] << 7) |
           (unsigned long)bytes[3];
}

/* Convert a four-byte big-endian frame size into an integer. */
static unsigned long get_frame_size(const unsigned char *bytes)
{
    return ((unsigned long)bytes[0] << 24) |
           ((unsigned long)bytes[1] << 16) |
           ((unsigned long)bytes[2] << 8) |
           (unsigned long)bytes[3];
}

/* Store the updated ID3 tag size in synchsafe format. */
static void put_tag_size(unsigned char *bytes, unsigned long size)
{
    bytes[0] = (size >> 21) & 0x7F;
    bytes[1] = (size >> 14) & 0x7F;
    bytes[2] = (size >> 7) & 0x7F;
    bytes[3] = size & 0x7F;
}

/* Store a frame's data size in four-byte big-endian format. */
static void put_frame_size(unsigned char *bytes, unsigned long size)
{
    bytes[0] = (size >> 24) & 0xFF;
    bytes[1] = (size >> 16) & 0xFF;
    bytes[2] = (size >> 8) & 0xFF;
    bytes[3] = size & 0xFF;
}

/*
 * Strictly validate the MP3 filename extension.
 * Only filenames ending exactly with ".mp3" are accepted.
 */
static int validate_mp3_filename(const char *filename)
{
    size_t length;

    if (filename == NULL || filename[0] == '\0')
    {
        fprintf(stderr, "Error: Filename cannot be empty.\n");
        return 1;
    }

    length = strlen(filename);

    if (length < 4 ||
        strcmp(filename + length - 4, ".mp3") != 0)
    {
        fprintf(stderr,
                "Error: Invalid file extension. "
                "Please provide an .mp3 file.\n");
        return 1;
    }

    return 0;
}

/*
 * Create a backup of the original MP3 before editing.
 * An existing backup is preserved and is never intentionally overwritten.
 *
 * Returns 0 on success and 1 on failure.
 */
static int create_backup(const char *filename)
{
    FILE *source = NULL;
    FILE *backup = NULL;
    char backup_filename[512];
    unsigned char buffer[4096];
    size_t bytes;
    size_t i = 0;
    size_t j = 0;
    const char suffix[] = "_backup.mp3";

    /* Build the backup filename while checking the buffer limit. */
    while (filename[i] != '\0')
    {
        if (i >= sizeof(backup_filename) - sizeof(suffix))
        {
            fprintf(stderr,
                    "Error: Filename is too long for backup.\n");
            return 1;
        }

        backup_filename[i] = filename[i];
        i++;
    }

    while (suffix[j] != '\0')
    {
        backup_filename[i++] = suffix[j++];
    }

    backup_filename[i] = '\0';

    /*
     * If a backup already exists, preserve it and allow editing to continue.
     * The existing backup is not overwritten.
     */
    backup = fopen(backup_filename, "rb");

    if (backup != NULL)
    {
        if (fclose(backup) != 0)
        {
            fprintf(stderr,
                    "Error: Unable to close existing backup file.\n");
            return 1;
        }

        printf("Existing backup preserved: %s\n",
               backup_filename);

        return 0;
    }

    /* Create the first backup when one does not already exist. */
    source = fopen(filename, "rb");

    if (source == NULL)
    {
        fprintf(stderr,
                "Error: Unable to open original MP3 for backup.\n");
        return 1;
    }

    backup = fopen(backup_filename, "wb");

    if (backup == NULL)
    {
        fclose(source);

        fprintf(stderr,
                "Error: Unable to create backup file.\n");
        return 1;
    }

    /* Copy the original MP3 in blocks to avoid loading it all into memory. */
    while ((bytes = fread(buffer, 1, sizeof(buffer), source)) > 0)
    {
        if (fwrite(buffer, 1, bytes, backup) != bytes)
        {
            fclose(source);
            fclose(backup);
            remove(backup_filename);

            fprintf(stderr,
                    "Error: Unable to write backup file.\n");
            return 1;
        }
    }

    if (ferror(source))
    {
        fclose(source);
        fclose(backup);
        remove(backup_filename);

        fprintf(stderr,
                "Error: Unable to read original MP3 for backup.\n");
        return 1;
    }

    if (fclose(source) != 0)
    {
        fclose(backup);
        remove(backup_filename);

        fprintf(stderr,
                "Error: Unable to close original MP3.\n");
        return 1;
    }

    if (fclose(backup) != 0)
    {
        remove(backup_filename);

        fprintf(stderr,
                "Error: Unable to finish writing backup.\n");
        return 1;
    }

    printf("Backup created successfully: %s\n", backup_filename);

    return 0;
}

/*
 * Write a complete ID3 frame consisting of:
 * frame identifier (4 bytes), frame size (4 bytes),
 * frame flags (2 bytes), and frame data.
 */
static int write_frame(FILE *file,
                       const char *frame_id,
                       const unsigned char *flags,
                       const unsigned char *data,
                       unsigned long size)
{
    unsigned char size_bytes[4];

    put_frame_size(size_bytes, size);

    if (fwrite(frame_id, 1, 4, file) != 4)
        return 1;

    if (fwrite(size_bytes, 1, 4, file) != 4)
        return 1;

    if (fwrite(flags, 1, 2, file) != 2)
        return 1;

    if (fwrite(data, 1, size, file) != size)
        return 1;

    return 0;
}

/*
 * Update the requested ID3v2.3 frame.
 * The original audio data is preserved, and a backup is created
 * before the original file is replaced.
 *
 * Returns 0 on success and 1 on failure.
 */
int edit_frame(const char *frame_id,
               const char *filename,
               const char *new_data)
{
    FILE *source = NULL;
    FILE *temp = NULL;
    FILE *check = NULL;

    unsigned char header[10];
    unsigned char *replacement = NULL;
    unsigned char buffer[4096];

    unsigned long old_tag_size;
    unsigned long new_tag_size = 0;
    unsigned long replacement_size;

    size_t bytes;
    size_t text_length;

    long tag_end;
    long file_size;

    int frame_found = 0;
    int result = 1;
    int temp_created = 0;
    int preserve_temp = 0;

    const char temp_filename[] = "temp.mp3";

    /* Validate arguments and reject unsupported filenames first. */
    if (frame_id == NULL || filename == NULL || new_data == NULL)
    {
        fprintf(stderr, "Error: Invalid editing arguments.\n");
        return 1;
    }

    if (validate_mp3_filename(filename) != 0)
        return 1;

    if (strlen(frame_id) != 4)
    {
        fprintf(stderr, "Error: Invalid frame identifier.\n");
        return 1;
    }

    /*
     * Do not overwrite a temporary file that may contain
     * another operation's data.
     */
    check = fopen(temp_filename, "rb");

    if (check != NULL)
    {
        fclose(check);

        fprintf(stderr,
                "Error: temp.mp3 already exists. "
                "Rename or remove it before editing.\n");

        return 1;
    }

    source = fopen(filename, "rb");

    if (source == NULL)
    {
        fprintf(stderr, "Error: Unable to open MP3 file.\n");
        return 1;
    }

    /* Read and validate the ID3 header before creating the backup. */
    if (fread(header, 1, 10, source) != 10)
    {
        fprintf(stderr, "Error: Unable to read ID3 header.\n");
        goto cleanup;
    }

    if (memcmp(header, "ID3", 3) != 0)
    {
        fprintf(stderr, "Error: ID3 tag not found.\n");
        goto cleanup;
    }

    /* This implementation supports ID3v2.3.0 tags. */
    if (header[3] != 3 || header[4] != 0)
    {
        fprintf(stderr, "Error: Only ID3v2.3.0 is supported.\n");
        goto cleanup;
    }

    /* Reject unsupported unsynchronisation and extended-header flags. */
    if ((header[5] & 0xC0) != 0)
    {
        fprintf(stderr, "Error: Unsupported ID3 header flags.\n");
        goto cleanup;
    }

    /* Each synchsafe size byte must have its highest bit cleared. */
    if (((header[6] | header[7] | header[8] | header[9]) & 0x80) != 0)
    {
        fprintf(stderr, "Error: Invalid ID3 tag size.\n");
        goto cleanup;
    }

    old_tag_size = get_tag_size(&header[6]);

    if (old_tag_size > 0x0FFFFFFFUL)
    {
        fprintf(stderr, "Error: ID3 tag is too large.\n");
        goto cleanup;
    }

    tag_end = 10L + (long)old_tag_size;

    /*
     * Validate the declared tag boundary against the actual file size.
     * This prevents editing a file whose ID3 tag extends beyond its end.
     */
    if (fseek(source, 0, SEEK_END) != 0)
    {
        fprintf(stderr, "Error: Unable to determine MP3 file size.\n");
        goto cleanup;
    }

    file_size = ftell(source);

    if (file_size < 0 || tag_end > file_size)
    {
        fprintf(stderr, "Error: ID3 tag extends beyond the file.\n");
        goto cleanup;
    }

    /* Return to the first frame after validating the file size. */
    if (fseek(source, 10L, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Unable to locate ID3 frames.\n");
        goto cleanup;
    }

    /* Preserve the original file before preparing the updated version. */
    if (create_backup(filename) != 0)
        goto cleanup;

    temp = fopen(temp_filename, "wb+");

    if (temp == NULL)
    {
        fprintf(stderr, "Error: Unable to create temporary file.\n");
        goto cleanup;
    }

    temp_created = 1;

    if (fwrite(header, 1, 10, temp) != 10)
    {
        fprintf(stderr, "Error: Unable to write ID3 header.\n");
        goto cleanup;
    }

    /*
     * Text frames begin with an encoding byte.
     * COMM frames additionally contain a three-byte language field
     * and an empty description terminated by a null byte.
     */
    text_length = strlen(new_data);

    if (strcmp(frame_id, "COMM") == 0)
    {
        if (text_length > 0x0FFFFFFFUL - 5)
        {
            fprintf(stderr, "Error: Replacement text is too large.\n");
            goto cleanup;
        }

        replacement_size = (unsigned long)text_length + 5;
    }
    else
    {
        if (text_length > 0x0FFFFFFFUL - 1)
        {
            fprintf(stderr, "Error: Replacement text is too large.\n");
            goto cleanup;
        }

        replacement_size = (unsigned long)text_length + 1;
    }

    replacement = malloc((size_t)replacement_size);

    if (replacement == NULL)
    {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        goto cleanup;
    }

    if (strcmp(frame_id, "COMM") == 0)
    {
        replacement[0] = 0;       /* ISO-8859-1 encoding */
        replacement[1] = 'e';
        replacement[2] = 'n';
        replacement[3] = 'g';
        replacement[4] = '\0';    /* Empty description */

        memcpy(replacement + 5, new_data, text_length);
    }
    else
    {
        replacement[0] = 0;       /* ISO-8859-1 encoding */

        memcpy(replacement + 1, new_data, text_length);
    }

    /*
     * Process each frame inside the original ID3 tag.
     * Frames that are not selected are copied without changing their data.
     */
    while (1)
    {
        char current_id[5];
        unsigned char frame_header[6];
        unsigned char flags[2];
        unsigned char *old_data = NULL;

        unsigned long current_frame_size;
        long position;
        long remaining;

        /* Check the current file position before processing a frame. */
        position = ftell(source);

        if (position < 0)
        {
            fprintf(stderr, "Error: Unable to locate frame.\n");
            goto cleanup;
        }

        if (position >= tag_end)
            break;

        remaining = tag_end - position;

        if (remaining < 10)
        {
            fprintf(stderr, "Error: Incomplete frame header.\n");
            goto cleanup;
        }

        if (fread(current_id, 1, 4, source) != 4)
        {
            fprintf(stderr, "Error: Unable to read frame ID.\n");
            goto cleanup;
        }

        current_id[4] = '\0';

        /* A zero frame identifier marks the start of tag padding. */
        if (current_id[0] == '\0')
            break;

        if (fread(frame_header, 1, 6, source) != 6)
        {
            fprintf(stderr, "Error: Unable to read frame header.\n");
            goto cleanup;
        }

        current_frame_size = get_frame_size(frame_header);

        flags[0] = frame_header[4];
        flags[1] = frame_header[5];

        if (current_frame_size > (unsigned long)(remaining - 10))
        {
            fprintf(stderr, "Error: Invalid frame size.\n");
            goto cleanup;
        }

        /* Allocate memory for the existing frame data. */
        if (current_frame_size > SIZE_MAX)
        {
            fprintf(stderr, "Error: Frame size is too large.\n");
            goto cleanup;
        }

        old_data = malloc(current_frame_size == 0
                          ? 1
                          : (size_t)current_frame_size);

        if (old_data == NULL)
        {
            fprintf(stderr, "Error: Memory allocation failed.\n");
            goto cleanup;
        }

        if (fread(old_data, 1, (size_t)current_frame_size, source)
            != (size_t)current_frame_size)
        {
            free(old_data);

            fprintf(stderr, "Error: Unable to read frame data.\n");
            goto cleanup;
        }

        if (strcmp(current_id, frame_id) == 0)
        {
            /*
             * Do not rewrite frames whose format flags indicate
             * compression, encryption, or grouping.
             */
            if ((flags[1] & 0xE0) != 0)
            {
                free(old_data);

                fprintf(stderr,
                        "Error: Cannot edit a compressed, encrypted, "
                        "or grouped frame.\n");

                goto cleanup;
            }

            /* The replacement uses an ordinary, uncompressed text frame. */
            flags[0] = 0;
            flags[1] = 0;

            if (write_frame(temp, current_id, flags,
                            replacement, replacement_size) != 0)
            {
                free(old_data);

                fprintf(stderr, "Error: Unable to write updated frame.\n");
                goto cleanup;
            }

            new_tag_size += 10 + replacement_size;
            frame_found = 1;
        }
        else
        {
            if (write_frame(temp, current_id, flags,
                            old_data, current_frame_size) != 0)
            {
                free(old_data);

                fprintf(stderr, "Error: Unable to copy frame.\n");
                goto cleanup;
            }

            new_tag_size += 10 + current_frame_size;
        }

        free(old_data);
    }

    if (ferror(source))
    {
        fprintf(stderr, "Error: Failed to read the MP3 file.\n");
        goto cleanup;
    }

    if (!frame_found)
    {
        fprintf(stderr, "Error: Frame %s was not found.\n", frame_id);
        goto cleanup;
    }

    if (new_tag_size > 0x0FFFFFFFUL)
    {
        fprintf(stderr, "Error: Updated tag is too large.\n");
        goto cleanup;
    }

    /* Copy the original audio data after the ID3 tag. */
    if (fseek(source, tag_end, SEEK_SET) != 0)
    {
        fprintf(stderr, "Error: Unable to locate audio data.\n");
        goto cleanup;
    }

    while ((bytes = fread(buffer, 1, sizeof(buffer), source)) > 0)
    {
        if (fwrite(buffer, 1, bytes, temp) != bytes)
        {
            fprintf(stderr, "Error: Unable to copy audio data.\n");
            goto cleanup;
        }
    }

    if (ferror(source))
    {
        fprintf(stderr, "Error: Failed while reading audio data.\n");
        goto cleanup;
    }

    /* Update the ID3 header to match the size of the rewritten frames. */
    put_tag_size(&header[6], new_tag_size);

    if (fseek(temp, 0, SEEK_SET) != 0 ||
        fwrite(header, 1, 10, temp) != 10)
    {
        fprintf(stderr, "Error: Unable to update ID3 header.\n");
        goto cleanup;
    }

    if (fflush(temp) != 0 || ferror(temp))
    {
        fprintf(stderr, "Error: Unable to finish writing temporary file.\n");
        goto cleanup;
    }

    if (fclose(temp) != 0)
    {
        temp = NULL;

        fprintf(stderr, "Error: Unable to close temporary file.\n");
        goto cleanup;
    }

    temp = NULL;

    if (fclose(source) != 0)
    {
        source = NULL;

        fprintf(stderr, "Error: Unable to close original file.\n");
        goto cleanup;
    }

    source = NULL;

    /*
     * Replace the original only after the temporary file is complete.
     * If replacement fails, preserve temp.mp3 for recovery.
     */
    if (rename(temp_filename, filename) != 0)
    {
        preserve_temp = 1;

        fprintf(stderr,
                "Error: Unable to replace the original MP3. "
                "The updated file remains as temp.mp3.\n");

        goto cleanup;
    }

    printf("\n========================================\n");
    printf("       FRAME UPDATED SUCCESSFULLY\n");
    printf("========================================\n");
    printf("Frame ID : %s\n", frame_id);
    printf("New Data : %s\n", new_data);
    printf("========================================\n");

    result = 0;

cleanup:
    if (source != NULL)
        fclose(source);

    if (temp != NULL)
        fclose(temp);

    free(replacement);

    /*
     * Remove an incomplete temporary file after a failed edit,
     * except when preserving the completed file for recovery.
     */
    if (result != 0 && temp_created && !preserve_temp)
        remove(temp_filename);

    return result;
}

