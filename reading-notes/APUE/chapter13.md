# 第十三章

## 13.3 编程规则

1. 首先调用`unmask`将文件模式屏蔽字设置成一个已知值（通常是0），因为继承而来的文件模式创建屏蔽字可能会被设置成拒绝某些权限。
2. 调用`fork`，然后使父进程exit，作用有二：首先，如果进程从shell启动，这样做会让shell认为这条命令已经执行完毕，其次，这样做保证新的子进程不是一个组长进程，为调用`setsid`做准备（此时可以继续fork保证新的子进程不是会话首进程，阻止其获得控制终端）
3. 调用`setsid`创建一个新的会话
4. 将当前工作目录改为根目录，如果继承而来的当前工作目录挂载在一个文件系统中，那么该文件系统就不能被卸载
5. 关闭不在需要的描述符
6. 打开`/dev/null`使其具有文件描述符1、2和3.

## 13.4 出错记录

`syslog`函数

## 13.5 单实例守护进程

文件锁来实现，实现见p.381

## 13.6 守护进程惯例

单线程多线程重新读取配置文件