# 第十五章

## 15.2 管道

```c
#include <unistd.h>
int pipe(int fd[2]);
```

经由fd返回两个文件描述符，fd[0]为读而打开，fd[1]为写而打开。fd[1]的输出是fd[0]的输入。单个进程中的管道几乎没有任何用处。通常，进程会先调用pipe，接着调用fork，从而创建从父进程到子进程的IPC通道，反之亦然。

利用管道实现进程建的同步，见p.435

> 注意，每个管道都有一个额外的读取进程，这没有关系，因为父进程没有执行对该管道的读操作，所以这不会影响我们。

## 15.3 函数`popen`和`pclose`

> p.437如何实现符合POSIX标准的`popen`和`pclose`

## 15.4 协同进程

> p.444例子中如果修改协同进程使其使用标准输入，则由于是从管道中读入的，所以是全缓冲的，从而引起死锁。

## 15.6 XSI IPC

### 15.6.1 标识符和键

若以IPC_PRIVATE为键，并且指明flag的IPC_CREATE标识位，则创建一个新的IPC结构，如果希望创建一个新的IPC结构，而且要确保没有引用具有同一标识符的一个现有IPC，那么必须在flag中同时指定IPC_CREATE和IPC_EXCL，这样做之后，如果IPC结构已经存在就会造成出错，返回EEXIST。

IPC是在系统范围内起作用的，没有引用计数，如果进程创建了一个消息队列，并且它在队列里放几则消息，然后终止，那么该队列及其内容不会被删除。他们会一直留在系统中直至发生下列动作为止：

- 由某个进程调用`msgrcv`或者`msgctl`读消息或删除消息队列
- 有某个进程执行`ipcrm`命令删除消息队列
- 正在自举的系统删除消息队列

### 优缺点

- 因为这些IPC在文件系统中没有名字，为了访问或者修改他们的属性，不得已系统必须增加十几个新的系统调用
- 因为这些形式的IPC不使用文件描述符，所以不能对他们使用多路转接IO函数。这使得他们很难一次使用一个以上这样的IPC结构。例如，如果没有某种形式的忙等循环，就不能使一个服务器进程等待要放在两个消息队列中的任意一个中的消息。

## 15.7 消息队列

[mmap用作共享内存](https://stackoverflow.com/questions/5656530/how-to-use-shared-memory-with-linux-in-c)