# 第四章

## 4.3 connect函数

connect函数调用将激发TCP三次握手过程，而且仅在连接建立成功或出错的时候才返回，其中出错返回可能有以下几种情况。

1. 若TCP客户没有收到SYN分节的响应，则返回ETIMEOUT错误，举例来说，调用connect函数时， 4.4BSD内核发送一个SYN，若无响应则等待6s后再发送一个，若仍无响应则等待24s再发送一个，若总共等待了75s后仍未收到响应则返回本错误
2. 对客户的SYN响应是RST，则表明服务器主机在我们指定的端口上没有进程在等待与之连接，这是一种硬错误，客户收到RST就马上返回ECONNREFUSED错误
3. 若客户发出的SYN在中间的某个路由器上引发了一个"destination unreachable"ICMP错误，则认为是一个软错误，客户主机保存该消息，并按第一中情况中所述的时间间隔继续发送SYN，若在规定的时间后仍未收到响应，则把保存的消息作为EHOSTUNREACH或者ENETUNREACH错误返回给进程

> 如果connect函数失败，则该套接字不再可用，必须关闭，我们不能对这样的套接字再次调用connect函数，而应该在每次conect调用失败后close当前套接字并重新调用socket。

## 4.4 bind函数

从bind函数返回的常见错误是EADDRINUSE

## 4.5 listen函数

内核为任何一个给定的监听套接字维护两个队列(1)未完成连接队列，SYN分节到达服务器，正在等待三次握手完成(2)已完成连接队列，三次握手已经完成

backlog曾经被定义成是连个队列的长度和，调用是不可以将backlog设置成0，因为不同的实现对此有不同的解释。

如果SYN到达的时候这些队列是满的，TCP就忽略该分节，这么做的原因是这种情况是暂时的，客户端可以在稍后重新发送SYN尝试连接，如果响应一个RST，对于客户端就无法分辨到底是没有进程监听，还是监听队列满了。

## 4.6 close函数

close函数使描述符的引用计数减一，如果该计数为0，则关闭描述符。shutdown直接发送FIN分节。