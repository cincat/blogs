# 注意！这个坑Spring Cloud官方也踩过

不要用`ThreadLocal.set(null)`去清理`ThreadLocal`对象，否则会造成内存泄露

关于这一点有同学已经在`Spring Cloud`的github上提了issue

> Using `ThreadLocal.set(null)` to clear a value out causes a memory leak when `ThreadPool`s are used. The problem is this:
>
> - `ThreadLocal.set(null)` will lookup a `Map` associated with the current `Thread`.
> - It sets the key of the map to `this` and a value of `null`
>
> The `Thread` is then contained within a `Thread` pool that outlasts the application. So if the application is un-deployed, the reference to `this` remains within the `Thread`.
>
> At first glance the problem may sound fairly minor. However, in many instances `this` is loaded by the child `ClassLoader` (i.e. the Web Application's `ClassLoader`). One example of how this happens is when using `NamedThreadLocal`.
>
> The implication is that `this.getClass().getClassLoader()` will hold a reference to an un-deployed application's `ClassLoader` which refers to all the classes it loaded. This means there is a rather large leak within the application.
>
> To avoid this, `ThreadLocal.remove()` should be used instead. The difference is that the `Map` entry is moved entirely vs mapping the key to a `null` value.
>
> One example of the issue can be found in `TraceContextHolder.currentSpan`. There appear to be other places in Spring Cloud Sleuth that this should be fixed too.

地址：https://github.com/spring-cloud/spring-cloud-sleuth/issues/27	

`Spring Cloud`官方也将这个Issue认定为BUG并进行了修复

![image-20210313222812460](https://gitee.com/cincat/picgo/raw/master/img/image-20210313222812460.png)

这个issue的大意是Spring Cloud的代码中比如`TraceContextHolder.currentSpan`采用了`ThreadLocal.set(null)`的方式去清理`ThreadLocal`对象，而这样做仅仅会将本线程的`ThreadLocalMap`中的`value`置为`null`但是`ThreadLocalMap`的`key`持有当前`ThreadLocal`的`this`引用，如果当前线程来自一个线程池，而恰恰这个线程池的生命周期比这个服务的生命周期要长（比如服务下线的场景），那会导致`this.getClass().getClassLoader()`加载的已经下线的服务的所有类都得不到释放，导致了内存泄露

## ThreadLocal的设计原理

![ThreadLocal类图1](https://gitee.com/cincat/picgo/raw/master/img/ThreadLocal类图1.png)

从类图上可以看到，ThreadLocal重写了一个定制的HashMap，我们着重关注下`Entry`这个类

```java
static class Entry extends WeakReference<ThreadLocal<?>> {
    /** The value associated with this ThreadLocal. */
    Object value;

    Entry(ThreadLocal<?> k, Object v) {
        super(k);
        value = v;
    }
}
```





