# 第七章

## 7.3 进程终止

### 1.退出函数

```cpp
#include <stdlib.h>
void exit(int status);
void _Exit(int status);
#include <unistd.h>
void _exit(int status)
```

`exit`函数会调用终止处理程序，标准输入输出清理程序，其他两个函数会直接退出。

### 2.函数`atexit`

```cpp
#include <stdlib.h>
int atexit(void (*func)(void));
```

`exit`调用这些函数的次序和他们注册的次序相反，同一个函数如果注册多次，也会被调用多次。内核执行一个程序的唯一方法就是调用`exec`函数，进程自愿终止的唯一方法是显示或隐式的调用三个`exit`函数之一。

## 7.9 环境变量

## 7.10 `setjmp longjmp`

```c
#include <setjmp.h>

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
```

此处`jmp_buf`传的是值，在实现中可能`jmp_buf`可能是一个数组的别名，传值过去会退化成指针，达到了操作全局变量的目的。