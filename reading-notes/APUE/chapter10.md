# 第十章

## 10.2 信号概念

不存在编号为0的信号，POSIX.1将这种信号值称为空信号

在某个信号出现时，我们可以告诉内核按下列三种方式之一进行处理

1. 忽略此信号，但有两种信号却决不能被忽略，他们是SIGKILL和SIGSTOP。这两种信号不能被忽略的原因是：他们向内核和超级用户提供了使进程终止或停止的可靠方法。另外如果忽略某些由硬件异常产生的信号（如非法内存引用或除以0），则进程的运行行为是未定义的
2. 捕捉信号，注意SIGKILL和SIGSTOP不能被捕捉
3. 执行信号默认动作

## 10.3 函数`signal`

### 1.程序启动

当执行一个程序时，所有信号的状态都是系统默认或者忽略。通常所有的信号都被设置成他们的默认动作，除非调用`exec`的进程忽略该信号。确切的讲，exec函数将原先设置为要捕捉的信号都更改为默认动作，其他信号的状态则不变（一个原先进程要捕捉的信号，当其执行一个新程序以后就不能再捕捉了，因为信号捕捉函数的地址很可能在所执行的新程序文件中已无意义）。具体的例子见p258“一个交互shell如何处理针对后台进程的中断和退出信号”

### 2.进程创建

当一个进程调用`fork`时，其子进程继承父进程的信号处理方式，因为子进程在开始时复制了父进程内存影像，所以信号捕捉函数的地址在子进程中是有意义的

## 10.4 不可靠信号

早期版本中的一个问题是在进程每次接到信号对其进行处理时，随即将信号动作置为默认值，所以早期处理信号的一个经典代码如下：

```cpp
//早期版本不支持void返回类型
int sig_int();
// other codes
signal(SIGINT,sig_int);
// other codes
int sig_int() {
    signal(SIGINT,sig_int);
}
```

这段代码的一个问题是，在信号发生之后到信号处理程序调用signal函数之间有一个时间窗口，在这段时间内，可能发生另一次中断信号。第二个信号会执行默认动作，对于中断信号来说就是终止该进程。  
这些早期版本的另一个问题是：在进程不希望某种信号发生时，它不能关闭该信号。有时希望通知系统“阻止下列信号发生，如果他们确实产生了，请记住他们”，能够显现这种缺陷的是下面的这段代码：

```cpp
int sig_int();
int sig_int_flag;

main() {
    signal(SIGINT, sig_int);
    ...
    while (sig_int_flag == 0)
        pause();
    ...
}
ing sig_int() {
    signal(SINGINT, sigint);
    sig_int_flag = 1;
}
```

如果在测试`sig_int_flag`之后，调用`pause`之前发生信号，则此进程调用pause时可能永久休眠。

## 10.5 中断的系统调用

早期的UNIX系统的一个特性是：如果进程在执行一个低速系统调用而阻塞期间捕捉到一个信号，则该系统调用就被中断不再继续执行，该系统调用返回出错，其`errno`设置为EINTR。

## 10.6 可重入函数

不可重入函数

1. 已知它们使用了静态的数据结构
2. 它们调用`malloc`或者`free`
3. 他们是标准I/O函数，标准I/O库的很多实现都以不可重入的方式使用全局数据结构

由于信号处理函数可能修改errno的值，因此，作为一个通用的规则，当在信号处理函数中调用图10-4中的函数时，因当在调用前保存errno，在调用后恢复errno。

## 10.7 SIGCLD语义

对于SIGCLD早期的处理方式是：

1. 如果进程明确地将信号的配置设为SIG_IGN，则调用进程的子进程不产生僵尸进程，这与其默认动作（SIG_DFL）不同，如果调用进程随后调用一个wait函数，那么它将阻塞知道所有子进程都终止，然后该wait返回-1，并将其errno设为ECHILD。
2. 如果SIGCLD的配置设置为捕捉，则内核立即检查是否有子进程准备好被等待，如果是这样，则调用SIGCLD处理程序。

由于第二点的做法，会在某些平台上产生异常，见p265

## 10.8 可靠信号术语和语义

进程可以选用“阻塞信号递送”，如果为进程产生了一个阻塞信号，而且对该信号的动作是系统默认动作或捕捉该信号，则为该进程将此信号保持为未决状态。直到该进程对此信号解除了阻塞，或者将对此信号的动作更改为忽略

如果在进程解除对某个信号的阻塞之前，这种信号发生了多次，POSIX.1允许系统递送该信号一次或者多次，如果递送了多次，则称这些信号进行了排队，但是除非支持POSIX.1实时扩展，否则大多数UNIX并不对信号排队，而是只递送这种信号一次。

如果多个信号要递送一个进程，POSIX.1并没有规定这些信号的递送顺序，但是POSIX.1基础部分建议：在其他信号之前递送与进程当前状态有关的信号，如SIGSEGV。

## 10.9 函数`kill`和`raise`

```cpp
#include <signal.h>
int kill(pid_t pid, int signo);
int raise(int signo);
```

pid值 | 含义
---|---
pid > 0 | 将信号发送至进程ID为pid的进程
pid == 0 | 将信号发送至与发送进程属于同一进程组的所有进程，且发送进程有权限向这些进程发送信号，这些信号不包括系统进程（内核进程和init）
pid < 0 | 将该信号发送到进程组ID等于pid绝对值的进程组，发送进程要有权限这么做，接受进程不包括系统进程集中的进程
pid == -1 | 将信号发送到进程有权限向他们发送信号的所有进程，如前所示，所有进程不包括系统进程集中的进程

权限规则：超级用户可以将信号发送给任一进程，如果是普通权限的进程，发送者的实际用户ID或者有效用户ID必须等于接受者的实际用于ID或者有效用户ID，如果实现支持POSIX_SAVED_IDS则检查接受这的保存用户ID（而不是有效用户ID）

特例：如果被发送的信号是SIGCONT，则进程可以将它发送给属于同一会话的任一其他进程

POSIX.1将编号0定义为空信号，如果signo参数为0，则kill荏苒执行错误检查，但不发送信号。如果向一个不存在的进程发送空信号，则kill返回-1，errno设置为ESRCH。由于这种测试不是原子操作，被测试的进程ID可能已经被复用，所以这种测试意义不大。

如果调用`kill`为调用进程产生信号，而且该信号是不被阻塞的，那可在`kill`返回之前，`signo`或者某个其他未决的，非阻塞的信号被传送至该进程。

## 10.10 函数`alarm`和`pause`

```cpp
#include <unistd.h>
unsigned int alarm(unsigned int seconds);
```

每个闹钟只能有一个闹钟时间，如果调用alarm时，之前已经为该进程注册的闹钟时间还没超时，则该闹钟的余值作为本次alarm函数调用的值返回，以前注册的闹钟时间被新值代替，如果以前注册的闹钟尚未超时，而本次调用的seconds值为0，则取消以前的闹钟时间，但是其余值仍然作为alarm函数的返回值。

```cpp
#include <unistd.h>
int pause(void);
```

只有执行了一个信号处理程序并从其返回时，pause才返回。

```c
#include <signal.h>
#include <unistd.h>

static void sig_alarm(int signo) {
    // nothing to do, just return to wake up the pause
}

unsigned int sleep1(unsigned int seconds) {
    if (signal(SIGALRM, sig_alarm) != SIG_ERR)
        return seconds;
    alarm(seconds);
    pause();
    return (alarm(0));
}
```

第一个版本的sleep函数有以下三个问题

1. 如果sleep调用之前进程已经设置了闹钟，那么调用sleep之后将会清除原来的闹钟，可用下列方法更正这一点：检查第一次alarm的返回值，如其值小于本次调用alarm的参数值，则只应等到已有的闹钟超时，如果已有的闹钟值晚于本次调用alarm的参数值，则在sleep返回之前，重置此闹钟，使其在之前闹钟设置的时间再次发生超时
2. 该程序修改了SIGALRM信号的配置，如果编写了一个函数供其他函数调用，则在函数被调用时先要保存原配置，在函数返回前再恢复原配置。
3. 第一次调用alarm和pause之间有竞争条件，在一个繁忙的系统中，可能alarm在调用pause之前超时，并调用了信号处理程序，如果发生这种情况，系统永久被挂起

```c
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

static jmp_buf env_alarm;

static void sig_alarm(int signo) {
    longjmp(env_alarm, 1);
}

unsigned int sleep2(unsigned int seconds) {
    if (signal(SIGALRM, sig_alarm) == SIG_ERR) {
        return (seconds);
    }
    if (setjmp(env_alrm) == 0) {
        alarm(seconds);
        pause();
    }
    return alarm(0);
}
```

如果SIGALRM中断了某个其他信号处理程序，则调用longjmp会提早终止该信号处理程序,见p270的例子。

## 10.11 信号集

```cpp
#include <signal.h>
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigdelset(sigset_t *set, int signo);
int sigismember(sigset_t *set, int signo);
```

## 10.12 函数`sigprocmask`

```c
#include <signal.h>

int sigprocmask(int how, const sigset_t *set, const sigset_t *oset);
```

>注：不能阻塞SIGKILL和SIGSTOP信号

how | 说明
-|-
SIG_BLOCK | 该进程的信号屏蔽字是当前信号屏蔽字和指向set的信号集的并集，set包含了希望阻塞的附加信号
SIG_UNBLOCK | 该进程的信号屏蔽字是当前信号和set指向信号集补集的交集，set包含了希望解除的信号
SIG_SETMASK | 该进程的新的信号屏蔽字是set指向的值

## 10.13 函数`sigpending`

```c
#include <signal.h>
int sigpending(sigset_t *set);
```

## 10.14 函数`sigaction`

用`sigaction`函数实现可靠的`signal`函数

```c
Sigfunc *signal(int signo, Sigfunc *func) {
    struct sigaction act, oact;
    act.sa_handler = func;
    act.sa_flags = 0;
    if (signo == SIGALRM) {
#ifdef SA_INTERRUPT
        act.sa_flags |= SA_INTERRUPT;
#endif
    }
    else {
        act.sa_flags |= SA_RESTART;
    }
    if (sigaction(signo, &act, &oact) < 0) {
        return SIG_ERR;
    }
    return (oact.sa_handler);
}
```

>注：不希望重启SIGALRM信号中断的系统调用的原因是：我们希望对I/O操作可以设置时间限制

## 10.15 函数`sigsetjmp`和`siglongjmp`

调用longjmp有一个问题，当捕捉一个信号时，进入信号捕捉函数，此时当前信号被自动的加入到进程的信号屏蔽子中，这阻止了后来的这种信号重哦昂段该信号处理程序，如果用longjmp跳出信号处理程序，无法还原以前的信号屏蔽字，所以才有了这两个函数接口

## 10.16 函数`sigsuspend`

```c
sigset_t newmask, oldmask;
sigemptyset(&newmask);
sigaddset(&newmask, SIGINT);
if (sigprocmask(SIG_BLOCK, &newmask, &oldmask) < 0)
    err_sys("SIG_BLOCK error");
//临界区代码
if (sigprocmask(SIG_SETMASK, &oldmask, NULL) < 0)
    err_sys("SIG_SETMASK error");
pause();
```

如果信号发生在调用pase函数之前，则丢失，因为解除信号屏蔽字和挂起程序不是原子操作，这就是我们为什么需要`sigsuspend`函数的原因。

利用信号实现父子进程间的同步,见p289。

## 10.17 函数`abort`

ISO 规定，调用`abort`将向主机环境递送一个未成功终止的通知，其方法是调用`raise`函数。实现见p292.

## 10.18 函数`system`

posix要求system函数忽略SIGINT和SIGQUIT,阻塞SIGCHLD，实现见p.295
> 要在fork之前就屏蔽SIGCHLD信号，以避免父子进程出现竞争条件，p.296有一个早期实现出现竞争条件的反例

## 10.19 函数`sleep`的实现

第一个版本的`sleep`函数有三个问题，其中最大的问题就是调用`alarm`和调用`pause`之间不是原子操作，会产生竞争条件，后来为了解决这个问题，不得已用了`longjmp`，当然这又产生了与其他信号处理函数交互的问题，好在后来有了`sigsuspend`函数，完美的解决了问题。实现见p.298