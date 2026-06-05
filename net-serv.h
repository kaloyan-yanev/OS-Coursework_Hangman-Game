#ifndef __NETUTIL_H__
#define __NETUTIL_H__

#include <stddef.h>
#include <sys/types.h>

/*
 * A small buffered line reader over a file descriptor (socket or pipe).
 *
 * The protocol used by hangman-server / hangman-client is a simple text
 * protocol where every message is a single '\n'-terminated line. Because TCP
 * is a byte stream (a single read() may return several lines, or only part of
 * a line) we need a tiny buffer that re-assembles complete lines for us.
 */
typedef struct line_reader
{
    int fd;
    char buf[4096];
    size_t len; /* number of valid bytes currently in buf */
} line_reader_t;

void reader_init(line_reader_t *r, int fd);

/*
 * Try to read more bytes from the underlying fd into the internal buffer.
 * Returns the number of bytes read (>0), 0 on EOF, or -1 on error.
 * This call may block.
 */
ssize_t reader_fill(line_reader_t *r);

/*
 * Extract one complete line (without the trailing '\n' / '\r') from data
 * already present in the internal buffer. Returns 1 if a line was produced,
 * 0 if no complete line is buffered yet. Never reads from the fd.
 */
int reader_getline(line_reader_t *r, char *out, size_t out_size);

/*
 * Blocking convenience wrapper: keeps filling and extracting until a full
 * line is available. Returns 1 on success, 0 on EOF (with no data), -1 on
 * error. On EOF with a trailing unterminated line, that line is returned.
 */
int reader_getline_blocking(line_reader_t *r, char *out, size_t out_size);

/* Write the whole buffer, handling short writes / EINTR. Returns 0 on
 * success, -1 on error. */
int write_all(int fd, const char *buf, size_t len);

/* Convenience: write a NUL-terminated string (caller includes any '\n'). */
int send_str(int fd, const char *s);

#endif
