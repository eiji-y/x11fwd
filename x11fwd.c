// Copyright (c) 2022 Eiji Yoshiya. All right reserved. 
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#define	EVENT_SIZE	1024

struct session {
	int	fd;
	struct	session *other;
	int	len;
	char	buf[4096];
};

int epollfd;

void fatal(char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

int get_display_num()
{
	char *display = getenv("DISPLAY");
	if (!display)
		fatal("No X11 DISPLAY variable was set");
	char *num = strchr(display, ':');
	if (!num)
		fatal("Invalid DISPLAY");
	return atoi(num + 1);
}

int create_server_sock(int num)
{
	int ret;
	int s = socket(AF_INET, SOCK_STREAM, 0);
	int on = 1;

	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));

	struct sockaddr_in in_addr;

	in_addr.sin_family = AF_INET;
	in_addr.sin_port = htons(6000 + num);
	in_addr.sin_addr.s_addr = INADDR_ANY;

	ret = bind(s, (struct sockaddr *)&in_addr, sizeof(in_addr));
	if (ret < 0)
		fatal("Bind Error");

	ret = listen(s, 10);
	if (ret < 0)
		fatal("Listen Error");

	return s;
}

void set_events(struct session *s, struct session *t)
{
	struct epoll_event ev;

	ev.events = 0;
	if (s->len < sizeof(s->buf))
		ev.events |= EPOLLIN;
	if (t->len)
		ev.events |= EPOLLOUT;
	ev.data.ptr = s;
	epoll_ctl(epollfd, EPOLL_CTL_MOD, s->fd, &ev);
}

void send_data(struct session *s, struct session *t)
{
	int len = send(t->fd, s->buf, s->len, 0);
	//printf("send = %x\n", len);
	if (len < 0)
		fatal("send");
	if (len > 0) {
		if (len < s->len) {
			memcpy(s->buf, s->buf + len, s->len - len);
		}
		s->len -= len;
	}
	set_events(s, t);
	set_events(t, s);
}

void setnonblocking(int fd)
{
	int flags;
	flags = fcntl(fd, F_GETFL, 0);
	flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, &flags);
}

int main(int argc, char **argv)
{
	if (daemon(1, 0) < 0)
		fatal("daemon");

	epollfd = epoll_create(EVENT_SIZE);
	if (epollfd < 0)
		fatal("epoll_create");

	int ret;
	int display_num = get_display_num();
	int listen_sock = create_server_sock(display_num);

	struct sockaddr_un un_addr;
	un_addr.sun_family = AF_UNIX;
	snprintf(un_addr.sun_path, sizeof un_addr.sun_path, "/tmp/.X11-unix/X%d", display_num);
	//printf("%s\n", un_addr.sun_path);

	struct epoll_event ev, events[EVENT_SIZE];

	ev.events = EPOLLIN;
	ev.data.fd = listen_sock;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) < 0)
		fatal("epoll_ctl");

	for (;;) {
		int nfds;

		nfds = epoll_wait(epollfd, events, sizeof(events)/sizeof(events[0]), -1);
		if (nfds < 0)
			fatal("epoll_wait");

		for (int n = 0; n < nfds; n++) {
			if (events[n].data.fd == listen_sock) {
				int conn = accept(listen_sock, NULL, 0);
				if (conn < 0)
					fatal("accept");
				setnonblocking(conn);

				int sock = socket(AF_UNIX, SOCK_STREAM, 0);
				if (sock < 0)
					fatal("socket");
				ret = connect(sock, (struct sockaddr *)&un_addr, sizeof(un_addr));
				if (ret < 0)
					fatal("connect");
				setnonblocking(sock);

				struct session *s = malloc(sizeof(struct session));
				struct session *t = malloc(sizeof(struct session));
				if (!s || !t)
					fatal("malloc");
				s->fd = conn;
				s->other = t;
				s->len = 0;
				t->fd = sock;
				t->other = s;
				t->len = 0;
				//printf("s, t = %p, %p\n", s, t);

				ev.events = EPOLLIN;
				ev.data.ptr = s;
				if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn, &ev) < 0)
					fatal("epoll_ctl");

				ev.events = EPOLLIN;
				ev.data.ptr = t;
				if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev) < 0)
					fatal("epoll_ctl");
			} else {
				int eof = 0;
				struct session *s = events[n].data.ptr;
				struct session *t = s->other;
				//printf("s, t = %p(%d), %p(%d)\n", s, s->len, t, t->len);

				//printf("events = %x\n", events[n].events);
				if (events[n].events & EPOLLHUP)
					eof = 1;

				if (events[n].events & EPOLLIN) {
					int len = recv(s->fd, s->buf + s->len, sizeof(s->buf) - s->len, 0);
					//printf("recv = %x\n", len);
					if (len > 0) {
						s->len += len;
						send_data(s, t);
					} else if (len == 0){
						eof = 1;
					}
				}
				if (events[n].events & EPOLLOUT) {
					//printf("EPOLLOUT\n");
					send_data(t, s);
				}

				if (eof) {
					struct epoll_event ev;

					if (epoll_ctl(epollfd, EPOLL_CTL_DEL, s->fd, NULL) < 0)
						fatal("epoll_ctl");
					if (epoll_ctl(epollfd, EPOLL_CTL_DEL, t->fd, NULL) < 0)
						fatal("epoll_ctl");
					close(s->fd);
					close(t->fd);
					free(s);
					free(t);
				}
			}
		}
	}

	return 0;
}
