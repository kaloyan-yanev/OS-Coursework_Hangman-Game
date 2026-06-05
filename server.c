#include "game.h"
#include "net-lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#define MAX_WORD 256

typedef struct player
{
    int fd;
    line_reader_t reader;
    secret_word_t target;     
    char incorrect[27];       
    int incorrect_len;
    bool solved;
} player_t;

static bool word_is_valid(const char *w)
{
    if (w == NULL || w[0] == '\0')
        return false;
    for (size_t i = 0; w[i] != '\0'; i++)
    {
        if (!is_letter(normalize(w[i])))
            return false;
    }
    return true;
}

static void build_masked(const secret_word_t *w, char *out)
{
    for (size_t i = 0; i < w->word_length; i++)
    {
        char c;
        if (secret_word_letter_at(w, i, &c) == SECRET_WORD_LETTER_REVEALED)
            out[i] = c;
        else
            out[i] = '_';
    }
    out[w->word_length] = '\0';
}

static void format_incorrect(const player_t *p, char *out, size_t out_size)
{
    if (p->incorrect_len == 0)
    {
        snprintf(out, out_size, "-");
        return;
    }
    size_t oi = 0;
    for (int i = 0; i < p->incorrect_len && oi + 2 < out_size; i++)
    {
        if (i > 0)
            out[oi++] = ',';
        out[oi++] = p->incorrect[i];
    }
    out[oi] = '\0';
}

static void send_state(player_t *p)
{
    char masked[MAX_WORD + 1];
    char inc[64];
    build_masked(&p->target, masked);
    format_incorrect(p, inc, sizeof(inc));

    char line[MAX_WORD + 128];
    snprintf(line, sizeof(line), "STATE %s %s\n", masked, inc);
    send_str(p->fd, line);
}

static void process_guess(player_t *p, const char *line)
{
    if (p->solved)
        return;

    char guess = 0;
    for (size_t i = 0; line[i] != '\0'; i++)
    {
        if (is_letter(normalize(line[i])))
        {
            guess = line[i];
            break;
        }
    }
    if (guess == 0)
    {
        send_state(p);
        return;
    }

    secret_word_guess_result_t res = secret_word_guess(&p->target, guess);
    if (res == SECRET_WORD_GUESS_INCORRECT && p->incorrect_len < 26)
    {
        p->incorrect[p->incorrect_len++] = normalize(guess);
        p->incorrect[p->incorrect_len] = '\0';
    }

    if (secret_word_is_solved(&p->target))
        p->solved = true;

    send_state(p);
}

static void send_results(player_t players[2])
{
    int inc0 = players[0].incorrect_len;
    int inc1 = players[1].incorrect_len;

    const char *code0, *code1;
    if (inc0 < inc1)      { code0 = "WIN";  code1 = "LOSE"; }
    else if (inc0 > inc1) { code0 = "LOSE"; code1 = "WIN";  }
    else                  { code0 = "TIE";  code1 = "TIE";  }

    char s0[64], s1[64];
    format_incorrect(&players[0], s0, sizeof(s0));
    format_incorrect(&players[1], s1, sizeof(s1));

    char line[256];
    snprintf(line, sizeof(line), "RESULT %s %s %s\n", code0, s0, s1);
    send_str(players[0].fd, line);
    snprintf(line, sizeof(line), "RESULT %s %s %s\n", code1, s1, s0);
    send_str(players[1].fd, line);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    addr.sin_port = htons((uint16_t) port);

    if (bind(listen_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
    {
        perror("bind");
        return 1;
    }
    if (listen(listen_fd, 8) < 0)
    {
        perror("listen");
        return 1;
    }

    printf("Listening on %d...\n", port);
    fflush(stdout);


    player_t players[2];
    char words[2][MAX_WORD];
    int got = 0;

    while (got < 2)
    {
        int cfd = accept(listen_fd, NULL, NULL);
        if (cfd < 0)
        {
            if (errno == EINTR)
                continue;
            perror("accept");
            return 1;
        }

        line_reader_t r;
        reader_init(&r, cfd);

        char wordbuf[MAX_WORD];
        int got_line = reader_getline_blocking(&r, wordbuf, sizeof(wordbuf));
        if (got_line <= 0 || !word_is_valid(wordbuf))
        {
            send_str(cfd, "ERR invalid word\n");
            close(cfd);
            continue;
        }

        players[got].fd = cfd;
        players[got].reader = r;            
        size_t wl = strlen(wordbuf);
        if (wl > MAX_WORD - 1)
            wl = MAX_WORD - 1;
        memcpy(words[got], wordbuf, wl);
        words[got][wl] = '\0';
        got++;
    }

    close(listen_fd);

    if (!secret_word_init_from_c_string(&players[0].target, words[1]) ||
        !secret_word_init_from_c_string(&players[1].target, words[0]))
    {
        fprintf(stderr, "failed to initialise secret words\n");
        return 1;
    }
    for (int i = 0; i < 2; i++)
    {
        players[i].incorrect_len = 0;
        players[i].incorrect[0] = '\0';
        players[i].solved = false;
    }

    send_state(&players[0]);
    send_state(&players[1]);

    bool finished = false;
    while (!finished)
    {
        for (int i = 0; i < 2; i++)
        {
            char line[MAX_WORD];
            while (!players[i].solved &&
                   reader_getline(&players[i].reader, line, sizeof(line)))
            {
                process_guess(&players[i], line);
            }
        }

        if (players[0].solved && players[1].solved)
        {
            send_results(players);
            break;
        }

        struct pollfd pfds[2];
        for (int i = 0; i < 2; i++)
        {
            pfds[i].fd = players[i].fd;
            pfds[i].events = POLLIN;
            pfds[i].revents = 0;
        }

        int pr = poll(pfds, 2, -1);
        if (pr < 0)
        {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        for (int i = 0; i < 2; i++)
        {
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR))
            {
                ssize_t n = reader_fill(&players[i].reader);
                if (n == 0)
                {
                    fprintf(stderr, "client disconnected, aborting game\n");
                    finished = true;
                    break;
                }
                if (n < 0)
                {
                    if (errno == EINTR)
                        continue;
                    perror("read");
                    finished = true;
                    break;
                }

                char line[MAX_WORD];
                while (!players[i].solved &&
                       reader_getline(&players[i].reader, line, sizeof(line)))
                {
                    process_guess(&players[i], line);
                }
            }
        }

        if (players[0].solved && players[1].solved)
        {
            send_results(players);
            finished = true;
        }
    }

    secret_word_free(&players[0].target);
    secret_word_free(&players[1].target);
    close(players[0].fd);
    close(players[1].fd);
    return 0;
}
