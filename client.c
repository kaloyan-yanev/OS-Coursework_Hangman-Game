/*
 * hangman-client : a player in a game of "competitive hangman".
 *
 * Usage:   ./hangman-client <host> <port> <opponent-word>
 *
 *   <host> / <port>   address of the hangman-server
 *   <opponent-word>   the word that OUR opponent will have to guess
 *
 * The client connects, submits the opponent's word, then repeatedly shows the
 * word it is itself guessing (masked) together with the wrong letters so far,
 * reads one guessed letter from stdin and sends it to the server. When the
 * word is fully revealed it waits for the final result from the server.
 *
 * The protocol is described in server.c.
 */

#define _GNU_SOURCE

#include "game.h"
#include "net-serv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define LINE_MAX_LEN 1024

/* Print a "a, b, c" list out of the compact "a,b,c" / "-" form. */
static void print_incorrect(const char *inc)
{
    if (strcmp(inc, "-") == 0)
        return;
    for (size_t i = 0; inc[i] != '\0'; i++)
    {
        if (inc[i] == ',')
            printf(", ");
        else
            putchar(inc[i]);
    }
}

static void print_state(const char *masked, const char *inc)
{
    printf("Word: %s\n", masked);
    printf("Incorrect guesses: ");
    print_incorrect(inc);
    printf("\n");
    fflush(stdout);
}

static void print_result(const char *line)
{
    /* line is "RESULT <code> <you> <opp>" (without the RESULT prefix here). */
    char code[16] = "", you[64] = "", opp[64] = "";
    sscanf(line, "%15s %63s %63s", code, you, opp);

    const char *message;
    if (strcmp(code, "WIN") == 0)
        message = "You Win!";
    else if (strcmp(code, "LOSE") == 0)
        message = "You Lose!";
    else
        message = "Tie";

    printf("%s\n", message);
    printf("Your incorrect guesses: ");
    print_incorrect(you);
    printf("\n");
    printf("Opponent's incorrect guesses: ");
    print_incorrect(opp);
    printf("\n");
    fflush(stdout);
}

/* Read a single guessed character from stdin, skipping whitespace. */
static int read_guess_char(void)
{
    int c;
    while ((c = getchar()) != EOF)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        return c;
    }
    return EOF;
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <host> <port> <opponent-word>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    const char *port = argv[2];
    const char *word = argv[3];

    signal(SIGPIPE, SIG_IGN);

    /* Resolve host:port and connect. */
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0)
    {
        fprintf(stderr, "could not resolve %s:%s\n", host, port);
        return 1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
    {
        fprintf(stderr, "could not connect to %s:%s\n", host, port);
        return 1;
    }

    /* Submit the word our opponent will have to guess. */
    char wbuf[LINE_MAX_LEN];
    snprintf(wbuf, sizeof(wbuf), "%s\n", word);
    send_str(fd, wbuf);

    line_reader_t reader;
    reader_init(&reader, fd);

    char line[LINE_MAX_LEN];

    /* ── Guessing loop ────────────────────────────────────────────────── */
    bool solved = false;
    while (!solved)
    {
        int rl = reader_getline_blocking(&reader, line, sizeof(line));
        if (rl <= 0)
        {
            fprintf(stderr, "connection closed by server\n");
            return 1;
        }

        if (strncmp(line, "ERR", 3) == 0)
        {
            const char *msg = line + 3;
            while (*msg == ' ')
                msg++;
            fprintf(stderr, "server rejected word: %s\n", msg);
            return 1;
        }
        else if (strncmp(line, "STATE ", 6) == 0)
        {
            char masked[LINE_MAX_LEN] = "";
            char inc[LINE_MAX_LEN] = "-";
            sscanf(line + 6, "%1023s %1023s", masked, inc);

            print_state(masked, inc);

            if (strchr(masked, '_') == NULL)
            {
                /* Word fully revealed: stop guessing, wait for the result. */
                solved = true;
                break;
            }

            int c = read_guess_char();
            if (c == EOF)
                return 0; /* stdin exhausted */

            char gbuf[4];
            gbuf[0] = (char) c;
            gbuf[1] = '\n';
            gbuf[2] = '\0';
            send_str(fd, gbuf);
        }
        else if (strncmp(line, "RESULT ", 7) == 0)
        {
            print_result(line + 7);
            return 0;
        }
        /* anything else is ignored */
    }

    /* ── Wait for the end-of-game result ──────────────────────────────── */
    for (;;)
    {
        int rl = reader_getline_blocking(&reader, line, sizeof(line));
        if (rl <= 0)
        {
            fprintf(stderr, "connection closed before result\n");
            return 1;
        }
        if (strncmp(line, "RESULT ", 7) == 0)
        {
            print_result(line + 7);
            return 0;
        }
    }
}
