#pragma once
#include <stdint.h>
#include <sys/types.h>

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define SOL_SOCKET 0xfff // options for socket level
#define SOL_TCP    6     // TCP level

// Option flags per-socket.
#define SO_DEBUG       0x0001 // turn on debugging info recording
#define SO_ACCEPTCONN  0x0002 // socket has had listen()
#define SO_REUSEADDR   0x0004 // allow local address reuse
#define SO_KEEPALIVE   0x0008 // keep connections alive
#define SO_DONTROUTE   0x0010 // just use interface addresses
#define SO_BROADCAST   0x0020 // permit sending of broadcast msgs
#define SO_USELOOPBACK 0x0040 // bypass hardware when possible
#define SO_LINGER      0x0080 // linger on close if data present
#define SO_OOBINLINE   0x0100 // leave received OOB data in line
#define SO_REUSEPORT   0x0200 // allow local address & port reuse
#define SO_DONTLINGER  (int)(~SO_LINGER)

// Additional options, not kept in so_options.
#define SO_SNDBUF      0x1001 // send buffer size
#define SO_RCVBUF      0x1002 // receive buffer size
#define SO_SNDLOWAT    0x1003 // send low-water mark
#define SO_RCVLOWAT    0x1004 // receive low-water mark
#define SO_SNDTIMEO    0x1005 // send timeout
#define SO_RCVTIMEO    0x1006 // receive timeout
#define SO_ERROR       0x1007 // get error status and clear
#define SO_TYPE        0x1008 // get socket type

// send()/recv()/etc flags
// at present, only MSG_PEEK is implemented though.
#define MSG_WAITALL   0x40000000
#define MSG_TRUNC     0x20000000
#define MSG_PEEK      0x10000000
#define MSG_OOB       0x08000000
#define MSG_EOR       0x04000000
#define MSG_DONTROUTE 0x02000000
#define MSG_CTRUNC    0x01000000

#define PF_UNSPEC 0
#define PF_INET   2
#define PF_INET6  10

#define AF_UNSPEC PF_UNSPEC
#define AF_INET   PF_INET
#define AF_INET6  PF_INET6

// shutdown() flags:
#define SHUT_RD   1
#define SHUT_WR   2
#define SHUT_RDWR 3

#ifndef _SA_FAMILY_T_DECLARED
typedef __sa_family_t sa_family_t;
#define _SA_FAMILY_T_DECLARED
#endif

#ifndef _SOCKLEN_T_DECLARED
typedef __socklen_t socklen_t;
#define _SOCKLEN_T_DECLARED
#endif

struct sockaddr {
	sa_family_t sa_family;
	char        sa_data[];
};

struct __attribute__((aligned(4))) sockaddr_storage {
	sa_family_t ss_family;
	char        ss_data[16 - sizeof(sa_family_t)];
};

#ifdef __cplusplus
extern "C" {
#endif

int socket(int domain, int type, int protocol);
int closesocket(int socket); // XX: non standard
int forceclosesocket(int socket); // XX: non standard

int accept(int socket, struct sockaddr* addr, socklen_t* addr_len);
int bind(int socket, const struct sockaddr* addr, socklen_t addr_len);
int connect(int socket, const struct sockaddr* addr, socklen_t addr_len);
int shutdown(int socket, int shutdown_type);
int listen(int socket, int max_connections);

ssize_t recv(int socket, void* data, size_t recvlength, int flags);
ssize_t recvfrom(int socket, void* data, size_t recvlength, int flags, struct sockaddr* addr, socklen_t* addr_len);
ssize_t send(int socket, const void* data, size_t sendlength, int flags);
ssize_t sendto(int socket, const void* data, size_t sendlength, int flags, const struct sockaddr* addr, socklen_t addr_len);

int getpeername(int socket, struct sockaddr* addr, socklen_t* addr_len);
int getsockname(int socket, struct sockaddr* addr, socklen_t* addr_len);
int getsockopt(int socket, int level, int option_name, void* data, socklen_t* data_len);
int setsockopt(int socket, int level, int option_name, const void* data, socklen_t data_len);

#ifdef __cplusplus
};
#endif
