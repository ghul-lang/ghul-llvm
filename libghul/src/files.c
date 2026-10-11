/*
 * Files and directories, on the real file system: what the core library's
 * file store calls on this target, where .NET reads and writes real files.
 *
 * Each operation that can fail answers, or leaves for file_last_error, one
 * of the codes below, from which the core library throws what .NET throws.
 * Paths arrive as UTF-16 and are used as UTF-8.
 */

#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    FILE_OK = 0,
    FILE_NOT_FOUND = 1,
    DIRECTORY_NOT_FOUND = 2,
    ACCESS_DENIED = 3,
    IO_ERROR = 4,
};

static int32_t last_error;

int32_t ghul_host_file_last_error(void) {
    return last_error;
}

/* `path` as a NUL-terminated UTF-8 string, in memory the caller frees. */
static char *utf8_path(const uint16_t *chars, int32_t length) {
    char *path = malloc((size_t)length * 3 + 4);

    if (!path) {
        libghul_fail("Out of memory.\n");
    }

    uint16_t pending = 0;
    size_t size = libghul_utf8_encode(chars, length, path, &pending);
    path[size] = 0;

    return path;
}

/* Whether the directory `path` would be in exists. */
static int parent_exists(const char *path) {
    const char *slash = strrchr(path, '/');

    if (!slash) {
        return 1;
    }

    if (slash == path) {
        return 1;
    }

    size_t length = (size_t)(slash - path);
    char *parent = malloc(length + 1);

    if (!parent) {
        libghul_fail("Out of memory.\n");
    }

    memcpy(parent, path, length);
    parent[length] = 0;

    struct stat status;
    int exists = stat(parent, &status) == 0 && S_ISDIR(status.st_mode);

    free(parent);
    return exists;
}

/*
 * The code for a failure to reach the file `path`: missing from a
 * directory that exists is a missing file, and anywhere else a missing
 * directory, as .NET tells the two apart.
 */
static int32_t code_for(int error, const char *path) {
    switch (error) {
    case ENOENT:
        return parent_exists(path) ? FILE_NOT_FOUND : DIRECTORY_NOT_FOUND;
    case ENOTDIR:
        return DIRECTORY_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EISDIR:
    case EROFS:
        return ACCESS_DENIED;
    default:
        return IO_ERROR;
    }
}

void *ghul_host_file_current_directory(void) {
    char buffer[PATH_MAX];

    if (!getcwd(buffer, sizeof buffer)) {
        buffer[0] = '/';
        buffer[1] = 0;
    }

    libghul_utf16 text = {0};
    libghul_utf8_decode(buffer, strlen(buffer), &text);

    return libghul_utf16_make_string(&text);
}

int32_t ghul_host_file_kind(const uint16_t *chars, int32_t length) {
    char *path = utf8_path(chars, length);
    struct stat status;
    int32_t kind = 0;

    if (stat(path, &status) == 0) {
        kind = S_ISDIR(status.st_mode) ? 2 : 1;
    }

    free(path);
    return kind;
}

void *ghul_host_file_read(const uint16_t *chars, int32_t length) {
    char *path = utf8_path(chars, length);
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        last_error = code_for(errno, path);
        free(path);
        return NULL;
    }

    struct stat status;

    if (fstat(fd, &status) == 0 && S_ISDIR(status.st_mode)) {
        close(fd);
        free(path);
        last_error = ACCESS_DENIED;
        return NULL;
    }

    size_t capacity = status.st_size > 0 ? (size_t)status.st_size : 4096;
    size_t size = 0;
    uint8_t *bytes = malloc(capacity);

    if (!bytes) {
        libghul_fail("Out of memory.\n");
    }

    for (;;) {
        if (size == capacity) {
            capacity *= 2;
            bytes = realloc(bytes, capacity);

            if (!bytes) {
                libghul_fail("Out of memory.\n");
            }
        }

        ssize_t got = read(fd, bytes + size, capacity - size);

        if (got < 0 && errno == EINTR) {
            continue;
        }

        if (got < 0) {
            last_error = IO_ERROR;
            close(fd);
            free(bytes);
            free(path);
            return NULL;
        }

        if (got == 0) {
            break;
        }

        size += (size_t)got;
    }

    close(fd);
    free(path);

    if (size > INT32_MAX) {
        free(bytes);
        last_error = IO_ERROR;
        return NULL;
    }

    void *result = ghul_make_bytes(bytes, (int32_t)size);

    free(bytes);
    last_error = FILE_OK;

    return result;
}

int32_t ghul_host_file_write(const uint16_t *chars, int32_t length, const uint8_t *bytes,
                             int32_t count, bool append) {
    char *path = utf8_path(chars, length);
    int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC);
    int fd = open(path, flags, 0666);

    if (fd < 0) {
        int32_t code = code_for(errno, path);
        free(path);

        /* Creating a file in a directory that is not there. */
        return code == FILE_NOT_FOUND ? DIRECTORY_NOT_FOUND : code;
    }

    free(path);

    size_t left = count > 0 ? (size_t)count : 0;

    while (left > 0) {
        ssize_t written = write(fd, bytes, left);

        if (written < 0 && errno == EINTR) {
            continue;
        }

        if (written < 0) {
            close(fd);
            return IO_ERROR;
        }

        bytes += written;
        left -= (size_t)written;
    }

    return close(fd) == 0 ? FILE_OK : IO_ERROR;
}

/* A file that is not there is not an error, as .NET's File.Delete has it. */
int32_t ghul_host_file_delete(const uint16_t *chars, int32_t length) {
    char *path = utf8_path(chars, length);
    int32_t code = FILE_OK;

    if (unlink(path) != 0) {
        code = code_for(errno, path);

        if (code == FILE_NOT_FOUND) {
            code = FILE_OK;
        }
    }

    free(path);
    return code;
}

/* Creates every directory along `path` that is not there. */
int32_t ghul_host_directory_create(const uint16_t *chars, int32_t length) {
    if (length == 0) {
        return IO_ERROR;
    }

    char *path = utf8_path(chars, length);
    int32_t code = FILE_OK;

    for (char *slash = path + 1;; slash++) {
        if (*slash == '/' || *slash == 0) {
            char ending = *slash;
            *slash = 0;

            if (mkdir(path, 0777) != 0 && errno != EEXIST) {
                code = code_for(errno, path);
            } else {
                struct stat status;

                if (stat(path, &status) != 0 || !S_ISDIR(status.st_mode)) {
                    code = IO_ERROR;
                }
            }

            *slash = ending;

            if (code != FILE_OK || ending == 0) {
                break;
            }
        }
    }

    free(path);
    return code;
}

static int32_t remove_tree(const char *path) {
    DIR *directory = opendir(path);

    if (!directory) {
        return code_for(errno, path);
    }

    size_t base = strlen(path);
    int32_t code = FILE_OK;
    struct dirent *entry;

    while (code == FILE_OK && (entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }

        char *child = malloc(base + strlen(entry->d_name) + 2);

        if (!child) {
            libghul_fail("Out of memory.\n");
        }

        strcpy(child, path);
        child[base] = '/';
        strcpy(child + base + 1, entry->d_name);

        struct stat status;

        if (lstat(child, &status) == 0 && S_ISDIR(status.st_mode)) {
            code = remove_tree(child);
        } else if (unlink(child) != 0) {
            code = code_for(errno, child);
        }

        free(child);
    }

    closedir(directory);

    if (code == FILE_OK && rmdir(path) != 0) {
        code = code_for(errno, path);
    }

    return code;
}

/*
 * Deletes the directory `path`: with everything in it when `recursive`, and
 * otherwise only when it is empty. A directory that is not there, or one
 * that is not empty, is an error, as .NET's Directory.Delete has it.
 */
int32_t ghul_host_directory_delete(const uint16_t *chars, int32_t length, bool recursive) {
    char *path = utf8_path(chars, length);
    int32_t code;

    if (recursive) {
        code = remove_tree(path);
    } else {
        code = rmdir(path) == 0 ? FILE_OK : code_for(errno, path);
    }

    if (code == FILE_NOT_FOUND) {
        code = DIRECTORY_NOT_FOUND;
    }

    free(path);
    return code;
}

/*
 * Moves the file or directory `from` to `to`. Nothing may be at `to`
 * already, as .NET's File.Move and Directory.Move have it; a file there is
 * an I/O error rather than being replaced.
 */
int32_t ghul_host_file_rename(const uint16_t *from_chars, int32_t from_length,
                              const uint16_t *to_chars, int32_t to_length) {
    char *from = utf8_path(from_chars, from_length);
    char *to = utf8_path(to_chars, to_length);
    int32_t code = FILE_OK;
    struct stat status;

    if (lstat(to, &status) == 0) {
        code = IO_ERROR;
    } else if (rename(from, to) != 0) {
        int error = errno;
        code = lstat(from, &status) != 0 ? code_for(error, from) : code_for(error, to);

        if (code == FILE_NOT_FOUND && lstat(from, &status) == 0) {
            code = DIRECTORY_NOT_FOUND;
        }
    }

    free(from);
    free(to);
    return code;
}

/* Makes `path` the process's current directory. */
int32_t ghul_host_directory_set_current(const uint16_t *chars, int32_t length) {
    char *path = utf8_path(chars, length);
    int32_t code = chdir(path) == 0 ? FILE_OK : code_for(errno, path);

    free(path);
    return code == FILE_NOT_FOUND ? DIRECTORY_NOT_FOUND : code;
}

/* --- Listing ------------------------------------------------------------ */

static char **entries;
static int32_t entry_count;

static int by_name(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/*
 * Lists the files, or the directories, directly in `path`, keeping the
 * names for directory_entry to answer one at a time. Answers how many there
 * are, or the negated code of a failure. Names are sorted by their UTF-8
 * bytes, so a listing is the same from one run to the next.
 */
int32_t ghul_host_directory_entries(const uint16_t *chars, int32_t length, bool directories) {
    for (int32_t i = 0; i < entry_count; i++) {
        free(entries[i]);
    }

    free(entries);
    entries = NULL;
    entry_count = 0;

    char *path = utf8_path(chars, length);
    DIR *directory = opendir(path);

    if (!directory) {
        int32_t code = code_for(errno, path);
        free(path);
        return -(code == FILE_NOT_FOUND ? DIRECTORY_NOT_FOUND : code);
    }

    size_t base = strlen(path);
    int32_t capacity = 0;
    struct dirent *entry;

    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }

        char *child = malloc(base + strlen(entry->d_name) + 2);

        if (!child) {
            libghul_fail("Out of memory.\n");
        }

        strcpy(child, path);
        child[base] = '/';
        strcpy(child + base + 1, entry->d_name);

        struct stat status;
        int is_directory = stat(child, &status) == 0 && S_ISDIR(status.st_mode);

        free(child);

        if (is_directory != (int)directories) {
            continue;
        }

        if (entry_count == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            entries = realloc(entries, sizeof(char *) * (size_t)capacity);

            if (!entries) {
                libghul_fail("Out of memory.\n");
            }
        }

        entries[entry_count] = strdup(entry->d_name);

        if (!entries[entry_count]) {
            libghul_fail("Out of memory.\n");
        }

        entry_count++;
    }

    closedir(directory);
    free(path);

    qsort(entries, (size_t)entry_count, sizeof(char *), by_name);

    return entry_count;
}

void *ghul_host_directory_entry(int32_t index) {
    libghul_utf16 text = {0};

    if (index >= 0 && index < entry_count) {
        libghul_utf8_decode(entries[index], strlen(entries[index]), &text);
    }

    return libghul_utf16_make_string(&text);
}
