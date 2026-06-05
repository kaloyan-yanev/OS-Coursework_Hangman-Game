#include "net-lib.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>

void reader_init(line_reader_t *r, int fd)
{
    r->fd = fd;
    r->len = 0;
}

ssize_t reader_fill(line_reader_t *r)
{
    /* If the buffer is completely full without a newline the line is longer
     * than we support; drop it to avoid a deadlock. */
    if (r->len >= sizeof(r->buf))
        r->len = 0;

    ssize_t n = read(r->fd, r->buf + r->len, sizeof(r->buf) - r->len);
    if (n > 0)
        r->len += (size_t) n;
    return n;
}

int reader_getline(line_reader_t *r, char *out, size_t out_size)
{
    for (size_t i = 0; i < r->len; i++)
    {
        if (r->buf[i] == '\n')
        {
            size_t line_len = i;
            if (line_len > 0 && r->buf[line_len - 1] == '\r')
                line_len--;

            size_t copy = line_len;
            if (copy > out_size - 1)
                copy = out_size - 1;
            memcpy(out, r->buf, copy);
            out[copy] = '\0';

            /* Shift the rest of the buffer down. */
            size_t rest = r->len - (i + 1);
            memmove(r->buf, r->buf + i + 1, rest);
            r->len = rest;
            return 1;
        }
    }
    return 0;
}

int reader_getline_blocking(line_reader_t *r, char *out, size_t out_size)
{
    for (;;)
    {
        if (reader_getline(r, out, out_size))
            return 1;

        ssize_t n = reader_fill(r);
        if (n == 0)
        {
            /* EOF: flush any trailing line that had no '\n'. */
            if (r->len > 0)
            {
                size_t copy = r->len;
                if (copy > out_size - 1)
                    copy = out_size - 1;
                memcpy(out, r->buf, copy);
                out[copy] = '\0';
                r->len = 0;
                return 1;
            }
            return 0;
        }
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
    }
}

int write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t) n;
    }
    return 0;
}

int send_str(int fd, const char *s)
{
    return write_all(fd, s, strlen(s));
}
