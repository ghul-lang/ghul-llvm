/*
 * Running another program: the command is built up one argument at a time,
 * then run to completion with its standard output and standard error
 * captured, and its standard input shared with this process.
 */

#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* The command being built: the program, then its arguments. */
static char **command;
static int command_count;
static int command_capacity;

/* What the last run captured, and why it could not start if it could not. */
static void *captured_output;
static void *captured_error;
static int32_t last_failure;

static char *utf8_text(const uint16_t *chars, int32_t length) {
    char *text = malloc((size_t)length * 3 + 4);

    if (!text) {
        $libghul_fail("Out of memory.\n");
    }

    uint16_t pending = 0;
    size_t size = $libghul_utf8_encode(chars, length, text, &pending);
    text[size] = 0;

    return text;
}

static void clear_command(void) {
    for (int i = 0; i < command_count; i++) {
        free(command[i]);
    }

    command_count = 0;
}

static void add_to_command(char *word) {
    /* One spare slot for the NULL that ends argv. */
    if (command_count + 2 > command_capacity) {
        command_capacity = command_capacity ? command_capacity * 2 : 8;
        command = realloc(command, sizeof(char *) * (size_t)command_capacity);

        if (!command) {
            $libghul_fail("Out of memory.\n");
        }
    }

    command[command_count++] = word;
    command[command_count] = NULL;
}

void $ghul_host_process_begin(const uint16_t *chars, int32_t length) {
    clear_command();
    add_to_command(utf8_text(chars, length));
}

void $ghul_host_process_add_argument(const uint16_t *chars, int32_t length) {
    add_to_command(utf8_text(chars, length));
}

typedef struct {
    char *bytes;
    size_t length;
    size_t capacity;
} buffer;

/* Reads what is ready on `fd` into `into`; answers 0 at the end of the stream. */
static int read_some(int fd, buffer *into) {
    if (into->length + 4096 > into->capacity) {
        into->capacity = into->capacity ? into->capacity * 2 : 8192;
        into->bytes = realloc(into->bytes, into->capacity);

        if (!into->bytes) {
            $libghul_fail("Out of memory.\n");
        }
    }

    ssize_t got = read(fd, into->bytes + into->length, into->capacity - into->length);

    if (got < 0) {
        return errno == EINTR || errno == EAGAIN;
    }

    into->length += (size_t)got;

    return got > 0;
}

static void *string_from(buffer *bytes) {
    $libghul_utf16 text = {0};

    $libghul_utf8_decode(bytes->bytes ? bytes->bytes : "", bytes->length, &text);
    free(bytes->bytes);

    return $libghul_utf16_make_string(&text);
}

int32_t $ghul_host_process_run(void) {
    buffer out = {0};
    buffer err = {0};

    captured_output = NULL;
    captured_error = NULL;
    last_failure = 0;

    if (command_count == 0) {
        last_failure = 2;
        return -1;
    }

    /* Anything this program buffered goes out before the child's output can. */
    $libghul_flush_output();

    int out_pipe[2];
    int err_pipe[2];

    if (pipe2(out_pipe, O_CLOEXEC) != 0) {
        last_failure = 2;
        return -1;
    }

    if (pipe2(err_pipe, O_CLOEXEC) != 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        last_failure = 2;
        return -1;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], 1);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], 2);

    pid_t child;
    int started = posix_spawnp(&child, command[0], &actions, NULL, command, environ);

    posix_spawn_file_actions_destroy(&actions);
    close(out_pipe[1]);
    close(err_pipe[1]);

    if (started != 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);
        last_failure = started == ENOENT || started == ENOTDIR ? 1 : 2;
        return -1;
    }

    /* Both pipes are drained together, so a child filling one cannot stall. */
    struct pollfd fds[2] = {
        {.fd = out_pipe[0], .events = POLLIN},
        {.fd = err_pipe[0], .events = POLLIN},
    };

    int open_count = 2;

    while (open_count > 0) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }

            break;
        }

        for (int i = 0; i < 2; i++) {
            if (fds[i].fd >= 0 && (fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
                if (!read_some(fds[i].fd, i == 0 ? &out : &err)) {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    open_count--;
                }
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        if (fds[i].fd >= 0) {
            close(fds[i].fd);
        }
    }

    int status = 0;

    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }

    captured_output = string_from(&out);
    captured_error = string_from(&err);

    /* A child ended by a signal reports 128 plus the signal, as .NET does. */
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return WEXITSTATUS(status);
}

int32_t $ghul_host_process_failure(void) {
    return last_failure;
}

void *$ghul_host_process_output(void) {
    return captured_output;
}

void *$ghul_host_process_error(void) {
    return captured_error;
}
