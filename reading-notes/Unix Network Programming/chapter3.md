# 第三章

## 3.2 套接字地址结构

### 3.2.1 IPV4套接字地址结构

```c
struct in_addr {
    in_addr_t s_addr;
}

struct sockaddr_in {
    uint8_t sin_len;
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
}
```

>`s_addr`在历史版本中是一个联合体，允许访问这一个32位的四个字节或者两个十六字节，随着子网划分技术的来临，那个联合已经不再需要了。

### 3.2.2 通用套接字地址结构

```c
struct sockaddr {
    uint8_t sa_len;
    sa_family_t sa_family;
    char sa_data[14];
}
```

### 3.2.4 新的通用套接字地址结构

```c
struct sockaddr_storage {
    uint8_t ss_len;
    sa_family_t ss_family;
    //足够大的内存区域
}
```

> 因为ANSI标准的迟到，又加上C语言中没有重载函数，所以需要一个通用的地址结构来表示各种其他的协议地址。

### 3.7 inet_pton和inet_ntop函数

```c
int inet_pton(int family, const char *strptr, void *addrptr);
int char *int_ntop(int family, const void *addrptr, char *strptr, size_t len);
```

### 3.8 readn和writen函数

```c
ssize_t readn(int fd, void *vptr, size_t n) {
    size_t nleft;
    ssize_t nread;
    char *ptr;
    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0) {
            if (errno == EINTR) nread = 0;
            else return -1;
        }
        else if (nread == 0) break;
        else {
            nleft -= nread;
            ptr += nread;
        }
    }
    return n - nleft;
}

ssize_t writen(int fd, void *vptr, size_t n) {
    size_t nleft;
    ssize_t nwritten;
    const char *ptr;
    ptr = vptr;
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0) {
            if (nwritten < 0 && errno = EINTR) {
                nwritten = 0;
            }
            else return -1;
            nleft -= nwritten;
            ptr += nwritten;
        }
    }
    return n;
}
```