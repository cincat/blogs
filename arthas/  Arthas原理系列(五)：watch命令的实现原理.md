## 前言

通过前面几篇文章的介绍，我们可以看到Arthas是如何通过插装来获取运行时信息的，从这篇文章开始，我们开始看Arthas里面的命令具体是如何实现的，涉及到的命令有`watch`, `trace`, `monitor`, `stack`， `time tunnel`, 这几条命令相应的`Command`类都继承了`EnhancerCommand`, 因此他们的实现离不开代码插装。

本文首先会介绍命令实现的通用流程，以便后续文章的开展，然后会着重看一下上面5条命令中最简单的一条`watch`是如何实现的。

## `AdviceListener`是如何工作的

从上篇文章的分析中我们可以看到，`Arthas`会在所有的待插装的代码的特定位置插装一个函数，相关的代码片段如下：

```java
// TODO 要检查 binding 和 回调的函数的参数类型是否一致。回调函数的类型可以是 Object，或者super。但是不允许一些明显的类型问题，比如array转到int

toInsert.add(new MethodInsnNode(Opcodes.INVOKESTATIC, interceptorMethodConfig.getOwner(), interceptorMethodConfig.getMethodName(),
        interceptorMethodConfig.getMethodDesc(), false));
```

这里的`interceptorMethodConfig`会在拦截器中设置插装函数(拦截器的工作原理见上一篇文章，[Arthas原理系列(四)：字节码插装让一切变得有可能](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483941&idx=1&sn=6dc8cff4077a9c1243af4d2807c93b5b&chksm=c30a5376f47dda60a84f187f1e541915a13bf75c838c65af2fecc51f392180f276a736c59d75&scene=21#wechat_redirect)，以`AtEnter`方法为例：

```java
@AtEnter(inline = true)
public static void atEnter(@Binding.This Object target, @Binding.Class Class<?> clazz,
        @Binding.MethodInfo String methodInfo, @Binding.Args Object[] args) {
    SpyAPI.atEnter(clazz, methodInfo, target, args);
}
```

将会在目标方法的第一行前面插入`atEnter`这个方法，实际的执行将会转发到`SpyAPI.atEnter`中，我们接下来看下`SpyAPI.atEnter`中会具体做些什么工作。

`SpyAPI`是一个接口，这个接口的实例化在`Enhancer`初始化的时候就已经完成了

```java
private static SpyImpl spyImpl = new SpyImpl();

static {
    SpyAPI.setSpy(spyImpl);
}
```

所以，我们直接看`SpyImpl`的实现就可以了：

```java
@Override
public void atEnter(Class<?> clazz, String methodInfo, Object target, Object[] args) {
    ClassLoader classLoader = clazz.getClassLoader();

    String[] info = splitMethodInfo(methodInfo);
    String methodName = info[0];
    String methodDesc = info[1];
    // TODO listener 只用查一次，放到 thread local里保存起来就可以了！
    List<AdviceListener> listeners = AdviceListenerManager.queryAdviceListeners(classLoader, clazz.getName(),
            methodName, methodDesc);
    if (listeners != null) {
        for (AdviceListener adviceListener : listeners) {
            try {
                if (skipAdviceListener(adviceListener)) {
                    continue;
                }
                adviceListener.before(clazz, methodName, methodDesc, target, args);
            } catch (Throwable e) {
                logger.error("class: {}, methodInfo: {}", clazz.getName(), methodInfo, e);
            }
        }
    }

}
```

`atEnter`的入参是被插装方法的所有信息，Artahs如何获取这些动态信息的，在上一篇文章[Arthas原理系列(四)：字节码插装让一切变得有可能](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483941&idx=1&sn=6dc8cff4077a9c1243af4d2807c93b5b&chksm=c30a5376f47dda60a84f187f1e541915a13bf75c838c65af2fecc51f392180f276a736c59d75&scene=21#wechat_redirect)中做了详细分析。这段方法比较简单，核心的思路是获取一个`AdviceListener`的列表，然后调用其`before`方法，直到这里，我们的主角`AdviceListener`才正式登场，我们看下`AdviceListener`的列表是如何获取的：

```java
public static List<AdviceListener> queryAdviceListeners(ClassLoader classLoader, String className,
        String methodName, String methodDesc) {
    classLoader = wrap(classLoader);
    className = className.replace('/', '.');
    ClassLoaderAdviceListenerManager manager = adviceListenerMap.get(classLoader);

    if (manager != null) {
        return manager.queryAdviceListeners(className, methodName, methodDesc);
    }

    return null;
}
```

可见查询全都转发到了`ClassLoaderAdviceListenerManager#queryAdviceListeners`，我们再深入看下去：

```java
public List<AdviceListener> queryAdviceListeners(String className, String methodName, String methodDesc) {
    className = className.replace('/', '.');
    String key = key(className, methodName, methodDesc);

    List<AdviceListener> listeners = map.get(key);

    return listeners;
}
```

可以卡看到这个方法就是封装了一个`map`，没有其他的逻辑，那这个`map`中的值是何时被初始化进去的呢？还是这个类，我们稍微上下翻一下：

```java
public static void registerAdviceListener(ClassLoader classLoader, String className, String methodName,
        String methodDesc, AdviceListener listener) {
    classLoader = wrap(classLoader);
    className = className.replace('/', '.');

    ClassLoaderAdviceListenerManager manager = adviceListenerMap.get(classLoader);

    if (manager == null) {
        manager = new ClassLoaderAdviceListenerManager();
        adviceListenerMap.put(classLoader, manager);
    }
    manager.registerAdviceListener(className, methodName, methodDesc, listener);
}
```

会发现在这个方法中将入参中的`listener`初始化到上文看到的`map`中，` manager.registerAdviceListener`中还有一点简单的逻辑，这里就不在详述。问题的关键在于`listener`是从外部传入的，我们再看下调用`registerAdviceListener`的上下文：

```java
// enter/exist 总是要插入 listener
AdviceListenerManager.registerAdviceListener(inClassLoader, className, methodNode.name, methodNode.desc,
        listener);
affect.addMethodAndCount(inClassLoader, className, methodNode.name, methodNode.desc);
```

正是在`Enhancer`的的`transform`方法中，这里的`listener`在`Enhancer`初始化的时候传值进来的：

```java
public Enhancer(AdviceListener listener, boolean isTracing, boolean skipJDKTrace, Matcher classNameMatcher,
        Matcher methodNameMatcher) {
    this.listener = listener;
    this.isTracing = isTracing;
    this.skipJDKTrace = skipJDKTrace;
    this.classNameMatcher = classNameMatcher;
    this.methodNameMatcher = methodNameMatcher;
    this.affect = new EnhancerAffect();
    affect.setListenerId(listener.id());
}
```

而`Enhancer`的初始化只在一个地方，那就是`EnhancerCommand#enhance`中

```java
// 从CommandProcess对象中获取AdviceListener实例
AdviceListener listener = getAdviceListenerWithId(process);
if (listener == null) {
    logger.error("advice listener is null");
    String msg = "advice listener is null, check arthas log";
    process.appendResult(new EnhancerModel(effect, false, msg));
    process.end(-1, msg);
    return;
}
boolean skipJDKTrace = false;
if(listener instanceof AbstractTraceAdviceListener) {
    skipJDKTrace = ((AbstractTraceAdviceListener) listener).getCommand().isSkipJDKTrace();
}
// 初始化Enhancer
Enhancer enhancer = new Enhancer(listener, listener instanceof InvokeTraceable, skipJDKTrace, getClassNameMatcher(), getMethodNameMatcher());
```

```java
AdviceListener getAdviceListenerWithId(CommandProcess process) {
    if (listenerId != 0) {
        AdviceListener listener = AdviceWeaver.listener(listenerId);
        if (listener != null) {
            return listener;
        }
    }
    return getAdviceListener(process);
}
```

`getAdviceListenerWithId`的实现比较简单，用`map`做了一层缓存，然后实际获取`AdviceListener`的过程都在`getAdviceListener`中， 而`getAdviceListener`却是一个抽象方法，具体的实现由下面的的这几个类提供，这几个方法正好就是我们要分析的几个需要插装才能实现的命令了![image-20201221231705076](https://gitee.com/cincat/picgo/raw/master/img/image-20201221231705076.png)

我们以本篇要讲的`watch`命令为例：

```java
@Override
protected AdviceListener getAdviceListener(CommandProcess process) {
    return new WatchAdviceListener(this, process, GlobalOptions.verbose || this.verbose);
}
```

`getAdviceListener`会返回一个`WatchAdviceListener`的实例，这个类实现了`before`, `afterReturning`，`afterThrowing`等方法，这些方法会按照他们的名字所示分别插装到目标方法的对应位置上。

## `watch`命令的实现

通过前面的文章[Arthas原理系列(三)：服务端启动流程](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483910&idx=1&sn=de2639043291c46b5cd3feefcf9952fd&chksm=c30a5355f47dda43afbe9ca38a41ec2d36f2080a4f0ba3c8bb0e53870f9d605f2cfad0b8e147&scene=21#wechat_redirect)我们可以看到，命令的执行最终都会调用到`Enhancer`的`process`方法中：

```java
@Override
public void process(final CommandProcess process) {
    // ctrl-C support
    process.interruptHandler(new CommandInterruptHandler(process));
    // q exit support
    process.stdinHandler(new QExitHandler(process));

    // start to enhance
    enhance(process);
}
```

`enhance`通过`SpyAPI`调用了不同的命令的`AdviceListener`，从而实现不同命令不同的插装逻辑，我们看下watch命令的实现：

```java
@Override
public void before(ClassLoader loader, Class<?> clazz, ArthasMethod method, Object target, Object[] args)
        throws Throwable {
    // 开始计算本次方法调用耗时
    threadLocalWatch.start();
    if (command.isBefore()) {
        watching(Advice.newForBefore(loader, clazz, method, target, args));
    }
}
```

`before`会插装到方法执行的起始位置，首先在方法执行前会启动一个本地的`loacalWatch`用于计时，如果命令指定了`-b`参数，将会调用`watching`命令直接输出结果：

```java
private void watching(Advice advice) {
    try {
        // 本次调用的耗时
        double cost = threadLocalWatch.costInMillis();
        boolean conditionResult = isConditionMet(command.getConditionExpress(), advice, cost);
        if (this.isVerbose()) {
            process.write("Condition express: " + command.getConditionExpress() + " , result: " + conditionResult + "\n");
        }
        if (conditionResult) {
            // 根据OGNL表达式计算需要输出的表达式
            Object value = getExpressionResult(command.getExpress(), advice, cost);

            WatchModel model = new WatchModel();
            model.setTs(new Date());
            model.setCost(cost);
            model.setValue(value);
            model.setExpand(command.getExpand());
            model.setSizeLimit(command.getSizeLimit());

            process.appendResult(model);
            process.times().incrementAndGet();
            if (isLimitExceeded(command.getNumberOfLimit(), process.times().get())) {
                abortProcess(process, command.getNumberOfLimit());
            }
        }
    } catch (Throwable e) {
        logger.warn("watch failed.", e);
        process.end(-1, "watch failed, condition is: " + command.getConditionExpress() + ", express is: "
                      + command.getExpress() + ", " + e.getMessage() + ", visit " + LogUtil.loggingFile()
                      + " for more details.");
    }
}
```

`watching`命令的执行逻辑也不复杂，主要完成以下几个工作：

1. 通过之前设置的`threadLocalWatch`获取本次调用的耗时
2. 根据`OGNL`表达式计算要输出到客户端的表达式，比如："{params,returnObj}"，将会输出该方法的入参和返回值，有关OGNL表达式的语法，请看文章[OGNL语法规范](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483720&idx=1&sn=226660602264822691cd9678b9217872&chksm=c30a501bf47dd90d5e75c2a506851dcc56e8acd47e8a3878e035eb98f58fc6ee04c42269f1fc&scene=21#wechat_redirect)
3. 新建一个`WatchModel`的实例，然后将方法执行的耗时和第2步获取到的表达式初始化到`WatchModel`实例中，`WatchModel`是Arthas返回给客户端的统一的结果
4. 查看观察次数是否已经超过命令设置的上限，如果是，则直接终止。从代码中看，默认的观察次数上线是100，可以通过`-n`参数修改。

```java
@Override
public void afterReturning(ClassLoader loader, Class<?> clazz, ArthasMethod method, Object target, Object[] args, Object returnObject) throws Throwable {
    Advice advice = Advice.newForAfterRetuning(loader, clazz, method, target, args, returnObject);
    if (command.isSuccess()) {
        watching(advice);
    }

    finishing(advice);
}

@Override
public void afterThrowing(ClassLoader loader, Class<?> clazz, ArthasMethod method, Object target, Object[] args,Throwable throwable) {
    Advice advice = Advice.newForAfterThrowing(loader, clazz, method, target, args, throwable);
    if (command.isException()) {
        watching(advice);
    }

    finishing(advice);
}
```

+ `command.isSuccess())`对应参数`s`，代表在在**方法返回之后**观察

+ `command.isException()`对应参数`e`, 代表在**方法异常之后**观察

+ `finishing(advice)`对应参数`f`,代表在**方法结束之后**(正常返回和异常返回)观察

+ `command.isBefore()`对应参数`b`, 代表在在**方法调用之前**观察

## 小结

我们在这篇文章总先是详细看了`AdviceListener`的实现过程，理解了它的工作原理就可以理解`Arthas`是如何将各种不同的命令的插装都统一在统一个框架中的，并且这个类的原理也是其他所有需要插装的基础，所以花费了比较多的笔墨进行分析。随后看了`watch`命令的实现，这是需要插装的命令中最简单的一个命令，在上一篇文章[Arthas原理系列(四)：字节码插装让一切变得有可能](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483941&idx=1&sn=6dc8cff4077a9c1243af4d2807c93b5b&chksm=c30a5376f47dda60a84f187f1e541915a13bf75c838c65af2fecc51f392180f276a736c59d75&scene=21#wechat_redirect)中我们详细分析了`Arthas`如何在运行时拿到方法的入参，返回值等信息的，`watch`命令在其基础上只加了一个计时的功能，因此逻辑是比较简单的

