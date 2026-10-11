/*
 * What the program learns about its process: its arguments, its
 * environment, the time and random numbers.
 */

#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>

static int argument_count;
static char **arguments;

void $libghul_arguments_init(int argc, char **argv) {
    /* The program's own name is not one of its arguments. */
    argument_count = argc > 0 ? argc - 1 : 0;
    arguments = argc > 0 ? argv + 1 : argv;
}

int32_t $ghul_host_argument_count(void) {
    return argument_count;
}

static void *string_from_utf8(const char *bytes, size_t length) {
    $libghul_utf16 text = {0};

    $libghul_utf8_decode(bytes, length, &text);

    return $libghul_utf16_make_string(&text);
}

void *$ghul_host_argument(int32_t index) {
    if (index < 0 || index >= argument_count) {
        return string_from_utf8("", 0);
    }

    return string_from_utf8(arguments[index], strlen(arguments[index]));
}

/* The environment's variables as they were at start, those with a name. */
static char **variables;
static int variable_count;

void $libghul_environment_init(char **environ) {
    int count = 0;

    while (environ && environ[count]) {
        count++;
    }

    variables = malloc(sizeof(char *) * (size_t)(count + 1));

    if (!variables) {
        $libghul_fail("Out of memory.\n");
    }

    for (int i = 0; i < count; i++) {
        const char *equals = strchr(environ[i], '=');

        if (equals && equals != environ[i]) {
            variables[variable_count++] = environ[i];
        }
    }
}

int32_t $ghul_host_environment_count(void) {
    return variable_count;
}

void *$ghul_host_environment_name(int32_t index) {
    if (index < 0 || index >= variable_count) {
        return string_from_utf8("", 0);
    }

    const char *entry = variables[index];

    return string_from_utf8(entry, (size_t)(strchr(entry, '=') - entry));
}

void *$ghul_host_environment_lookup(const uint16_t *name, int32_t length) {
    char *encoded = malloc((size_t)length * 3 + 4);

    if (!encoded) {
        $libghul_fail("Out of memory.\n");
    }

    uint16_t pending = 0;
    size_t size = $libghul_utf8_encode(name, length, encoded, &pending);

    void *result = NULL;

    for (int i = 0; i < variable_count; i++) {
        const char *entry = variables[i];
        const char *equals = strchr(entry, '=');

        if ((size_t)(equals - entry) == size && memcmp(entry, encoded, size) == 0) {
            result = string_from_utf8(equals + 1, strlen(equals + 1));
            break;
        }
    }

    free(encoded);

    return result;
}

static struct timespec started;

void $libghul_clock_init(void) {
    clock_gettime(CLOCK_MONOTONIC, &started);
}

double $ghul_host_monotonic_milliseconds(void) {
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    return (double)(now.tv_sec - started.tv_sec) * 1000.0 +
           (double)(now.tv_nsec - started.tv_nsec) / 1000000.0;
}

int32_t $ghul_host_random_u32(void) {
    uint32_t value;
    size_t got = 0;

    while (got < sizeof value) {
        ssize_t result = getrandom((char *)&value + got, sizeof value - got, 0);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }

            $libghul_fail("Unable to read random bytes from the operating system.\n");
        }

        got += (size_t)result;
    }

    return (int32_t)value;
}
