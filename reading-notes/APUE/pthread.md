# Linux下的多线程

## 11.3 线程标示

```c
#include <pthread.h>
int pthread_equal(pthrea_t tid1, pthread_t tid2);
pthread_t pthread_self(void);
```

`pthread_t`类型不能当做整形数来处理，因为实现的时候可以用一个结构来代表`pthread_t`数据类型。

## 11.4 线程创建

线程创建的时候并不能保证哪个线程先运行：是新创建的线程还是调用线程。新创建的线程继承调用线程的浮点环境和信号屏蔽字，但该线程的挂起信号（pending signals）会被清除。

> 注： p.310打印线程ID的例子中，可靠的获取线程ID的方法是`pthread_self()`

```c
#include <pthread.h>

int pthread_create(pthread_t *restrict tidp,
    const pthread_attr_t *restrict attr,
    void *(start*)(void *), void *restrict arg);
```

### 线程创建属性

```c
#include <pthread.h>
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthrea_attr_t *attr);
```

名称 | 描述 | 接口
-|-|-
detachstate | 线程的分离状态属性 | `pthread_attr_getdetachstate`
guardsize | 线程末尾的警戒缓冲区大小 | `pthread_attr_getguardsize`
stackaddr | 线程的最低地址 | `pthread_attr_getstack`
stacksize | 线程栈的最小长度 | `pthread_attr_getstacksize`

## 11.5 线程的终止

如果线程中的任意进程调用了exit, _Exit或者_exit,那么整个进程就会终止，单个线程有三种方式可以退出：

1. 线程可以简单的从启动例程中退出，返回值是线程的退出码
2. 线程被同一进程中的其他线程取消
3. 线程调用`pthread_exit`

```c
#include <pthread.h>
void pthread_exit(void *rval_ptr);
void pthread_join(pthread_t thread, void **rval_ptr);
```

> 调用`pthread_exit`返回一个指针的时候，确保这个指针在调用者完成调用后依然有效！

### 线程取消

#### 取消行为

```c
#include <pthread.h>
int pthread_cancel(pthread_t tid);
```

`pthread_cancel`调用并不等待线程终止。在默认的情况下，线程在取消请求发出后还是继续运行，直到线程到达某个取消点（见p.362）,如果应用程序长时间不会调用图12-15的函数我们可以自己设置取消点

```c
#include <pthread.h>
void pthread_testcancel(void);
```

#### 取消属性

线程的取消属性并没有包含在`pthread_attr_t`中，他们是可取消状态和可取消类型

```c
#include <pthread.h>
int pthread_setcancelstate(int state, int *oldstate);
```

#### 取消类型

我们在上面描述的取消类型也称为推迟取消，也可以调用接口设置异步取消，此时线程在任意时间取消，不是非得到取消点才取消

```c
#include <pthread.h>
int pthread_setcanceltype(int type, int *oldtype);
```

### 线程清理程序

```c
#include <pthread.h>
void pthread_cleanup_push(void (*rtn)(void *), void *arg);
void pthread_cleanup_pop(int execute);
```

何时调用线程清理程序：

- 调用`pthread_exit`时；
- 相应取消请求时；
- 用非零execute参数调用`pthread_cleanup_pop`时

>线程从启动例程终止返回时不会调用清理函数

## 11.6 线程同步

### 11.6.1 互斥量

```c
#include <pthead.h>
int pthread_mutex_init(pthread_mutex_t *restrict mutex,
    const pthread_mutexattr_t *restrict attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);

int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
```

#### 互斥锁属性

```c
#include <pthread.h>
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
```

##### 进程共享

存在这样一个机制：允许相互独立的多个进程把同一个内存数据块映射到他们各自独立的地址空间中，就像多个线程访问共享数据一样，多个进程访问共享数据通常也需要同步，如果进程共享数据量属性设置为PTHREAD_PROCESS_SHARED,从多个进程彼此之间共享的内存数据块中分配的互斥量就可以用于这些进程的同步，如果进程共享属性值设置为PTHREAD_PROCESS_PRIVATE,就允许pthread线程库提供更有效的实现。

##### 健壮属性

默认值是PTHREAD_MUTEX_STALLED，这意味着持有互斥量的进程终止时不需要采取特别的动作，另一个值是PTHREAD_MUTEX_ROBUST,如果设置这个属性，当锁被另一个进程持有，但进程终止时没有对它进行解锁时，线程调用`pthread_mutex_lock`将会**获取锁**，但是返回值是EOWNNEREEAD而不是0，应用程序从这个特殊的返回值获知，被其他进程持有的锁需要恢复。

##### 类型属性

类型 | 描述
---|---
PTHREAD_MUTEX_NORMAL | 标准互斥量，不做特殊的错误检查和死锁检测
PTHREAD_MUTEX_ERRORCHECK | 此互斥量类型提供错误检查
PTHREAD_MUTEX_RECURSIVE | 此互斥量允许同一线程在互斥量解锁之前对该互斥量多次加锁
PTHREAD_MUTEX_DEFAULT | 此互斥量提供默认的特性和行为Linux3.2把这种类型映射为普通的互斥量类型，而FreeBSD8.0则把他映射成错误检查互斥量类型

```c
#include <pthread.h>
int pthread_mutexattr_gettype(const pthread_mutexattr_t *restrict attr, int *restrict type);
int pthread_mutexattr_settype(const pthread_mutexattr_t *restrict attr, int type);
```

>不要在条件变量中使用递归锁

### 11.6.4 读写锁

读写锁也叫共享互斥锁，当读写锁是读模式锁住时，可以说成是共享模式锁住的，当它是写模式锁住的时候，就可以说成是以互斥模式锁住的。与互斥量相比，读写锁在写之前必须初始化，在释放他们底层内存之前必须销毁。

#### 属性

只有进程共享一个属性，属性性质同互斥量

### 11.6.6 条件变量

```c
#include <pthread.h>
int pthread_cond_init(pthread_cond_t *restrict cond, const pthread_condattr_t *restrict attr);
int pthread_cond_destroy(pthread_cond_t *cond);

int pthread_cond_wait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex);
int pthread_cond_timewait(pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex, const struct timespec *restrict tsptr);
```

#### 条件变量属性

进程共享属性以及时钟属性；

### 11.6.7 自旋锁

### 11.6.8 屏障

> 属性只有进程共享属性

## 12.6 线程特定数据

```c
int pthread_once(pthread_once_t *initflag, void (*initfn)(void));
```

`initflag`必须是一个非本地变量（如全局变量或者静态变量），而且必须初始化为PTHREAD_ONCE_INIT.

## 12.8 线程和信号

每个线程都有自己的信号屏蔽字，但是信号处理程序是进程中的所有线程共享的，这意味着线程单个线程可以阻止某些信号，但当某个线程修改了与某个给定信号相关的处理行为以后，所有的线程都必须共享这个处理行为的改变。

进程中的信号是递送到单个线程的，如果一个信号与硬件故障有关，那么该信号一般会发送蛋糕引起该时间的进程中去，而其他信号则被发送到任意一个线程。

```c
#include <pthread.h>
int phread_sigmask(int how, const sigset_t *restrict set,
    sigset_t *restrict oset);

int sigwait(const sigset_t *restrict set, int *restrict signop);
```

set中指定了线程等待的信号集，返回时signop指向的整数包含被递送的信号编号。当一个进程通过`sigaction`建立了信号处理程序，而且一个线程正在`sigwait`调用中等待同一个信号，那么这时将有操作系统实现来决定以何种方式递送信号，操作系统可以让`sigwait`返回，也可以激活信号处理程序，但这两种情况不会同时发生。

> p.365另起一个线程同步处理信号的例子中，新建立的线程继承了现有的信号屏蔽字，信号最终只递送到没有设置相应屏蔽字的线程中。

## 12.9 线程和fork

子进程通过继承整个地址空间的副本，还从父进程那里继承了每个互斥量，读写锁和条件变量的状态，如果父进程包含一个以上的线程，子进程在fork返回后如果不是紧接着马上调用exec的话，就需要清理状态。

在子进程中只有一个线程，它是由父进程中调用fork的线程副本构成的，如果父进程中的线程占有锁，子进程将同样占有这些锁，问题是子进程并不包含占有锁的线程的副本，所以子进程没有办法知道它占有了哪些锁，需要释放那些锁，这时候需要用到另一个接口。

在fork返回和子进程调用其中任一个`exec`函数之前，子进程只能调用异步信号安全函数(避免读写全局变量引起不一致的问题)。

```c
#include <pthread.h>
int pthread_atfork(void (*prepare)(void), void (*parent)(void),
    void (*child)(void));
```

> parent 和 child fork处理程序是以他们注册时得到顺序进行调用的，而repare fork处理程序的调用顺序与他们注册时的顺序相反。