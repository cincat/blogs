# 第八章

## 8.3 函数`fork`

```cpp
#include <unistd.h>
pid_t fork(void);
```

> 注意图8.1中的例子

父进程和子进程之间的区别如下

+ fork的返回值不同
+ 进程ID不同
+ 这两个进程的父进程ID不同：子进程的进程ID是创建它的进程的ID，而父进程的父进程ID则不变
+ 子进程的tms_utime,tms_stime,tms_cutime,tms_ustime的值都设置为0
+ 子进程不继承父进程设置的文件锁
+ 子进程的未处理闹钟被清除
+ 子进程未处理的信号集设置为空集

## 8.4 函数`vfork`

`vfork`函数的调用序列和返回值与`fork`相同，但两者的语义不同

+ 在子进程调用`exec`之前，他在父进程的空间中运行
+ `vfork`保证子进程先运行

## 8.5 函数`exit`

+ 如果父进程在子进程之前终止，那子进程的父进程都会变成`init`进程
+ 一个已终止的，父进程尚未对其进行善后处理的进程称为僵尸进程
+ `init`的子进程一旦终止，它就会调用`wait`，取得终止状态
+ 两次`fork`可以避免产生僵尸进程 p.195

## 8.6 函数`wait`和`waitpid`

```cpp
pid_t wait(int *statloc);
pid_t waitpid(pid_t pid, int *statloc, int options);
```

+ 在一个子进程终止前，`wait`使其调用者阻塞，而`waitpid`有一个选项可以不阻塞调用
+ `waitpid`并不等候在其调用后的第一个终止子进程，它有若干选项控制等待的进程

关于终止进程的状态

```cpp
#include "apue.h"
#include <sys/wait.h>

void pr_exit(int status) {
    if (WIFEXITED(status))
        printf("normal termination, exit status = %d\n",
            WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("abornal termination, signal number = %d%s\n",
            WTERMSIG(status),
#ifdef WCOREDUMP
        WCOREDUMP(status) ? " (core file generated" : "");
#else
            "");
#endif
    else if (WIFSTOPPED(status))
        printf("child stopped, signal number = %d\n",
            WSTOPSIG(status));
}
```

对于`waitpid`函数中的`pid`参数的作用解释如下：

pid | 说明
-|-  
pid == -1 | 等待任意子进程，与`wait`等效  
pid > 0   | 等待进程id与pid相等的子进程  
pid == 0  | 等待组id与调用进程组id相等的任意子进程  
pid < -1  | 等待组id等于pid绝对值的任意子进程  

`options`参数进一步控制`waitpid`的操作，取0或者下面图中常量按位或运算的结果

常量 | 说明
----- | -----
WCONTINUED | 如果实现支持作业控制，那么由pid指定的任意子进程在停止后已经继续，但其状态未报告，则返回其状态
NOHANG | 若由pid指定的子进程并不是立即可用的，则`waitpid`不阻塞，此时其返回值为0
WUNTRACED | 若实现支持作业控制，而由pid指定的任意子进程已经停止，并且未报告状态，则返回其状态

## 8.7 函数`waitid`

## 8.8 函数`wait3`和`wait4`

## 8.9 竞争条件

## 8.10 函数`exec`

执行`exec`后，进程ID没有改变，但新程序从调用进程继承了下列属性

+ 进程ID以及父进程ID
+ 实际ID和实际组ID
+ 附属组ID
+ 进程组ID
+ 会话ID
+ 控制终端
+ 闹钟尚余留的时间
+ 当前工作目录
+ 根目录
+ 文件模式创建屏蔽字
+ 文件锁
+ 进程信号屏蔽
+ 未处理的信号
+ 资源限制
+ nice值
+ tms_utime,tms_stime, tms_cutime, tms_cstime

对打开文件的处理与每个描述符的执行时关闭标志值有关，需要注意，这几个函数如果执行成功，就不返回，如果失败就返回-1；

## 8.11 更改用户ID和更改组ID

```cpp
#include <unistd.h>
int setuid(uid_t uid);
int setgid(gid_t gid);
```

1. 若进程具有超级权限（实际用户ID），则`setuid`函数将实际用户ID，有效用户ID以及保存的设置用户ID设置为uid
2. 若进程没有超级权限，但是`uid`等于实际用户ID或保存的设置用户ID，则`setuid`只将有效用户ID设置为uid，不更改实际用户ID和保存的设置组ID
3. 如果上面的两个条件都不满足，则`errno`设置为EPERM，并返回-1

除此之外，还有以下四个函数也跟权限有关：

```cpp
#include <unistd.h>
int setreuid(uid_t ruid, uid_t euid);
int setregid(gid_t rgid, gid_t egid);

int seteuid(uid_t uid);
int setegid(gid_t gid);
```

> p.206页关于at程序的例子是这一节很好的佐证

## 8.13 函数`system`

不含信号处理的版本

```cpp
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>

int system(const char *cmdstring) {
    pid_t pid;
    int status;

    if (cmdstring == NULL)
        return 1;

    if ((pid = fork()) < 0) {
        status = -1;
    }
    else if (pid == 0){
        execl("bin/sh", "sh", "-c", cmdstring, (char*)0);
        _exit(127);
    }
    else {
        while (waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {
                status = -1;
                break;
            }
        }
    }
    return status;
}
```

调用`_exit`是为了防止任一标准I/O缓冲（这些缓冲会在fork中由父进程复制到子进程）在子进程中被冲洗

## 8.17 进程时间