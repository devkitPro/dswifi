#pragma once

#define FIONBIO  1
#define FIONREAD 2

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int fd, int request, ...);

#ifdef __cplusplus
}
#endif
