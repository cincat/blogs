## 前言

在前面的文章中我们可以看到`watch`命令对原来的字节码进行了插装，并且我们以此为思路实现了一个简易版的`watch`命令，但真实的`watch`提供的能力远不止统计方法的运行时间，我们最常用他的一个功能还是观察方法的入参返回值等运行时的变量。所有需要插装字节码的命令都继承了`EnhancerCommand`,总共有 5 个命令，分别是`monitor,stack,tt,watch,trace`，本片文章将会展示 arthas 为这么多的命令如何设计一个统一的插装流程的。

## 插装的主流程

在上篇文章中，我们看了`watch`命令的`process`方法：

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

可以看到整个插装的入口是`enhance`这个方法，这个方法在父类`EnhancerCommand`中实现：

```java
protected void enhance(CommandProcess process) {
    Session session = process.session();
    if (!session.tryLock()) {
        String msg = "someone else is enhancing classes, pls. wait.";
        process.appendResult(new EnhancerModel(null, false, msg));
        process.end(-1, msg);
        return;
    }
    EnhancerAffect effect = null;
    int lock = session.getLock();
    try {
        Instrumentation inst = session.getInstrumentation();
        // 获取每个命令特有的AdviceListener，插装的时候会把这个方法注入到目标JVM中
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
            // 如果是trace命令，判断是否需要跳过jdk提供的方法
            skipJDKTrace = ((AbstractTraceAdviceListener) listener).getCommand().isSkipJDKTrace();
        }

        Enhancer enhancer = new Enhancer(listener, listener instanceof InvokeTraceable, skipJDKTrace, getClassNameMatcher(), getMethodNameMatcher());
        // 注册通知监听器
        process.register(listener, enhancer);
        // 开始插装
        effect = enhancer.enhance(inst);

        if (effect.getThrowable() != null) {
            String msg = "error happens when enhancing class: "+effect.getThrowable().getMessage();
            process.appendResult(new EnhancerModel(effect, false, msg));
            process.end(1, msg + ", check arthas log: " + LogUtil.loggingFile());
            return;
        }

        if (effect.cCnt() == 0 || effect.mCnt() == 0) {
            // no class effected
            // might be method code too large
            process.appendResult(new EnhancerModel(effect, false, "No class or method is affected"));
            String msg = "No class or method is affected, try:\n"
                    + "1. sm CLASS_NAME METHOD_NAME to make sure the method you are tracing actually exists (it might be in your parent class).\n"
                    + "2. reset CLASS_NAME and try again, your method body might be too large.\n"
                    + "3. check arthas log: " + LogUtil.loggingFile() + "\n"
                    + "4. visit https://github.com/alibaba/arthas/issues/47 for more details.";
            process.end(-1, msg);
            return;
        }

        // 这里做个补偿,如果在enhance期间,unLock被调用了,则补偿性放弃
        if (session.getLock() == lock) {
            if (process.isForeground()) {
                process.echoTips(Constants.Q_OR_CTRL_C_ABORT_MSG + "\n");
            }
        }

        process.appendResult(new EnhancerModel(effect, true));

        //异步执行，在AdviceListener中结束
    } catch (Throwable e) {
        String msg = "error happens when enhancing class: "+e.getMessage();
        logger.error(msg, e);
        process.appendResult(new EnhancerModel(effect, false, msg));
        process.end(-1, msg);
    } finally {
        if (session.getLock() == lock) {
            // enhance结束后解锁
            process.session().unLock();
        }
    }
}
```

这段代码稍微有点长，里面最重要的是两个地方，一个是调用`getAdviceListenerWithId`获得了一个`AdviceListener`，这个类实现了`befor`,`arterReturning`,`afterThrowing`等方法，会被插装函数注入到目标`JVM`中。另一个是创建了一个`Enhancer`对象，并开始对目标 JVM 的类和方法进行插装。

```java
public synchronized EnhancerAffect enhance(final Instrumentation inst) throws UnmodifiableClassException {
    // 获取需要增强的类集合
    this.matchingClasses = GlobalOptions.isDisableSubClass
            ? SearchUtils.searchClass(inst, classNameMatcher)
            : SearchUtils.searchSubClass(inst, SearchUtils.searchClass(inst, classNameMatcher));

    // 过滤掉无法被增强的类
    filter(matchingClasses);

    logger.info("enhance matched classes: {}", matchingClasses);

    affect.setTransformer(this);

    try {
        ArthasBootstrap.getInstance().getTransformerManager().addTransformer(this, isTracing);

        // 批量增强
        if (GlobalOptions.isBatchReTransform) {
            final int size = matchingClasses.size();
            final Class<?>[] classArray = new Class<?>[size];
            arraycopy(matchingClasses.toArray(), 0, classArray, 0, size);
            if (classArray.length > 0) {
                inst.retransformClasses(classArray);
                logger.info("Success to batch transform classes: " + Arrays.toString(classArray));
            }
        } else {
            // for each 增强
            for (Class<?> clazz : matchingClasses) {
                try {
                    inst.retransformClasses(clazz);
                    logger.info("Success to transform class: " + clazz);
                } catch (Throwable t) {
                    logger.warn("retransform {} failed.", clazz, t);
                    if (t instanceof UnmodifiableClassException) {
                        throw (UnmodifiableClassException) t;
                    } else if (t instanceof RuntimeException) {
                        throw (RuntimeException) t;
                    } else {
                        throw new RuntimeException(t);
                    }
                }
            }
        }
    } catch (Throwable e) {
        logger.error("Enhancer error, matchingClasses: {}", matchingClasses, e);
        affect.setThrowable(e);
    }

    return affect;
}
```

`enhance`这个方法的实现如果看过之前实现`watch`命令那一篇应该不会陌生，在这个方法中最重要的事情就是注册了本类作为转换类，并且调用`Instrumentation`的`retransformClasses`方法对类进行转换。在这个机制下，真正干活的就是`transform`方法了。

```java
@Override
public byte[] transform(final ClassLoader inClassLoader, String className, Class<?> classBeingRedefined,
        ProtectionDomain protectionDomain, byte[] classfileBuffer) throws IllegalClassFormatException {
    try {
        // 检查classloader能否加载到 SpyAPI，如果不能，则放弃增强
        try {
            if (inClassLoader != null) {
                inClassLoader.loadClass(SpyAPI.class.getName());
            }
        } catch (Throwable e) {
            logger.error("the classloader can not load SpyAPI, ignore it. classloader: {}, className: {}",
                    inClassLoader.getClass().getName(), className);
            return null;
        }

        // 这里要再次过滤一次，为啥？因为在transform的过程中，有可能还会再诞生新的类
        // 所以需要将之前需要转换的类集合传递下来，再次进行判断
        if (matchingClasses != null && !matchingClasses.contains(classBeingRedefined)) {
            return null;
        }

        //keep origin class reader for bytecode optimizations, avoiding JVM metaspace OOM.
        ClassNode classNode = new ClassNode(Opcodes.ASM9);
        ClassReader classReader = AsmUtils.toClassNode(classfileBuffer, classNode);
        // remove JSR https://github.com/alibaba/arthas/issues/1304
        classNode = AsmUtils.removeJSRInstructions(classNode);

        // 生成增强字节码
        // 重点1：生成拦截器
        DefaultInterceptorClassParser defaultInterceptorClassParser = new DefaultInterceptorClassParser();

        final List<InterceptorProcessor> interceptorProcessors = new ArrayList<InterceptorProcessor>();

        interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyInterceptor1.class));
        interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyInterceptor2.class));
        interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyInterceptor3.class));

        if (this.isTracing) {
            if (this.skipJDKTrace == false) {
                interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceInterceptor1.class));
                interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceInterceptor2.class));
                interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceInterceptor3.class));
            } else {
                interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceExcludeJDKInterceptor1.class));
                interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceExcludeJDKInterceptor2.class));
                interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceExcludeJDKInterceptor3.class));
            }
        }

        List<MethodNode> matchedMethods = new ArrayList<MethodNode>();
        for (MethodNode methodNode : classNode.methods) {
            if (!isIgnore(methodNode, methodNameMatcher)) {
                matchedMethods.add(methodNode);
            }
        }

        // 用于检查是否已插入了 spy函数，如果已有则不重复处理
        // GroupLocationFilter不是很重要，他的目的只是为了防止重复插装
        GroupLocationFilter groupLocationFilter = new GroupLocationFilter();

        LocationFilter enterFilter = new InvokeContainLocationFilter(Type.getInternalName(SpyAPI.class), "atEnter",
                LocationType.ENTER);
        LocationFilter existFilter = new InvokeContainLocationFilter(Type.getInternalName(SpyAPI.class), "atExit",
                LocationType.EXIT);
        LocationFilter exceptionFilter = new InvokeContainLocationFilter(Type.getInternalName(SpyAPI.class),
                "atExceptionExit", LocationType.EXCEPTION_EXIT);

        groupLocationFilter.addFilter(enterFilter);
        groupLocationFilter.addFilter(existFilter);
        groupLocationFilter.addFilter(exceptionFilter);

        LocationFilter invokeBeforeFilter = new InvokeCheckLocationFilter(Type.getInternalName(SpyAPI.class),
                "atBeforeInvoke", LocationType.INVOKE);
        LocationFilter invokeAfterFilter = new InvokeCheckLocationFilter(Type.getInternalName(SpyAPI.class),
                "atInvokeException", LocationType.INVOKE_COMPLETED);
        LocationFilter invokeExceptionFilter = new InvokeCheckLocationFilter(Type.getInternalName(SpyAPI.class),
                "atInvokeException", LocationType.INVOKE_EXCEPTION_EXIT);
        groupLocationFilter.addFilter(invokeBeforeFilter);
        groupLocationFilter.addFilter(invokeAfterFilter);
        groupLocationFilter.addFilter(invokeExceptionFilter);

        for (MethodNode methodNode : matchedMethods) {
            // 先查找是否有 atBeforeInvoke 函数，如果有，则说明已经有trace了，则直接不再尝试增强，直接插入 listener
            if(AsmUtils.containsMethodInsnNode(methodNode, Type.getInternalName(SpyAPI.class), "atBeforeInvoke")) {
                for (AbstractInsnNode insnNode = methodNode.instructions.getFirst(); insnNode != null; insnNode = insnNode
                        .getNext()) {
                    if (insnNode instanceof MethodInsnNode) {
                        final MethodInsnNode methodInsnNode = (MethodInsnNode) insnNode;
                        if(this.skipJDKTrace) {
                            if(methodInsnNode.owner.startsWith("java/")) {
                                continue;
                            }
                        }
                        // 原始类型的box类型相关的都跳过
                        if(AsmOpUtils.isBoxType(Type.getObjectType(methodInsnNode.owner))) {
                            continue;
                        }
                        AdviceListenerManager.registerTraceAdviceListener(inClassLoader, className,
                                methodInsnNode.owner, methodInsnNode.name, methodInsnNode.desc, listener);
                    }
                }
            }else {
                MethodProcessor methodProcessor = new MethodProcessor(classNode, methodNode, groupLocationFilter);
                for (InterceptorProcessor interceptor : interceptorProcessors) {
                    try {
                        // 重点2：真正的插装工作在这里完成
                        List<Location> locations = interceptor.process(methodProcessor);
                        for (Location location : locations) {
                            if (location instanceof MethodInsnNodeWare) {
                                MethodInsnNodeWare methodInsnNodeWare = (MethodInsnNodeWare) location;
                                MethodInsnNode methodInsnNode = methodInsnNodeWare.methodInsnNode();

                                AdviceListenerManager.registerTraceAdviceListener(inClassLoader, className,
                                        methodInsnNode.owner, methodInsnNode.name, methodInsnNode.desc, listener);
                            }
                        }

                    } catch (Throwable e) {
                        logger.error("enhancer error, class: {}, method: {}, interceptor: {}", classNode.name, methodNode.name, interceptor.getClass().getName(), e);
                    }
                }
            }

            // enter/exist 总是要插入 listener
            AdviceListenerManager.registerAdviceListener(inClassLoader, className, methodNode.name, methodNode.desc,
                    listener);
            affect.addMethodAndCount(inClassLoader, className, methodNode.name, methodNode.desc);
        }

        // https://github.com/alibaba/arthas/issues/1223 , V1_5 的major version是49
        if (AsmUtils.getMajorVersion(classNode.version) < 49) {
            classNode.version = AsmUtils.setMajorVersion(classNode.version, 49);
        }

        byte[] enhanceClassByteArray = AsmUtils.toBytes(classNode, inClassLoader, classReader);

        // 增强成功，记录类
        classBytesCache.put(classBeingRedefined, new Object());

        // dump the class
        dumpClassIfNecessary(className, enhanceClassByteArray, affect);

        // 成功计数
        affect.cCnt(1);

        return enhanceClassByteArray;
    } catch (Throwable t) {
        logger.warn("transform loader[{}]:class[{}] failed.", inClassLoader, className, t);
        affect.setThrowable(t);
    }

    return null;
}
```

这段代码又是比较长的，里面占据篇幅较长的是三块内容，有两块代码是我们要关注的重点，第一步是注册了一个`InterceptorProcessor`拦截器列表，拦截器的作用就是确定插装代码可以注入到哪些地方，我们后面会着重讲；第二步是注册了一个过滤器的列表，过滤器的逻辑比较简单，就是为了避免重复插装；第三步才是调用各个处理器的`process`进行具体的插装逻辑。

## InterceptorProcessor 是如何生成的

```java
 // 生成增强字节码
DefaultInterceptorClassParser defaultInterceptorClassParser = new DefaultInterceptorClassParser();

final List<InterceptorProcessor> interceptorProcessors = new ArrayList<InterceptorProcessor>();
interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyInterceptor1.class));
interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyInterceptor2.class));
interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyInterceptor3.class));

if (this.isTracing) {
    if (this.skipJDKTrace == false) {
        interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceInterceptor1.class));
        interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceInterceptor2.class));
        interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceInterceptor3.class));
    } else {
        interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceExcludeJDKInterceptor1.class));
        interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceExcludeJDKInterceptor2.class));
        interceptorProcessors.addAll(defaultInterceptorClassParser.parse(SpyTraceExcludeJDKInterceptor3.class));
    }
}
```

这个代码片段对于我们理解 arthas 拦截器是如何工作的是比较重要的，首先我们看下`com.alibaba.bytekit.asm.interceptor.parser.DefaultInterceptorClassParser#parse`的逻辑：

```java
@Override
public List<InterceptorProcessor> parse(Class<?> clazz) {
    final List<InterceptorProcessor> result = new ArrayList<InterceptorProcessor>();

    MethodCallback methodCallback = new MethodCallback() {

        @Override
        public void doWith(Method method) throws IllegalArgumentException, IllegalAccessException {
            for (Annotation onMethodAnnotation : method.getAnnotations()) {
                for (Annotation onAnnotation : onMethodAnnotation.annotationType().getAnnotations()) {
                    if (InterceptorParserHander.class.isAssignableFrom(onAnnotation.annotationType())) {

                        if (!Modifier.isStatic(method.getModifiers())) {
                            throw new IllegalArgumentException("method must be static. method: " + method);
                        }

                        InterceptorParserHander handler = (InterceptorParserHander) onAnnotation;
                        InterceptorProcessorParser interceptorProcessorParser = InstanceUtils
                                .newInstance(handler.parserHander());
                        InterceptorProcessor interceptorProcessor = interceptorProcessorParser.parse(method,
                                onMethodAnnotation);
                        result.add(interceptorProcessor);
                    }
                }
            }
        }

    };
    ReflectionUtils.doWithMethods(clazz, methodCallback);

    return result;
}
```

这个方法的逻辑是比较简单的，对于传进来的类，遍历其所有的方法，如果方法上有`InterceptorParserHander`注解，则调用其`parse`方法，那我们就看下传给`com.alibaba.bytekit.asm.interceptor.parser.DefaultInterceptorClassParser#parse`的几个类：

```java
public class SpyInterceptors {

    public static class SpyInterceptor1 {

        @AtEnter(inline = true)
        public static void atEnter(@Binding.This Object target, @Binding.Class Class<?> clazz,
                @Binding.MethodInfo String methodInfo, @Binding.Args Object[] args) {
            SpyAPI.atEnter(clazz, methodInfo, target, args);
        }
    }
}
```

事实上`SpyInterceptors`是包含比较多的内部类的，大体上分为三个类别，一类是普通的插装，第二类是普通的针对`trace`命令的插装，第三类是过滤了 JDK 方法的`trace`命令的插装，在每个类中有分为了方法调用前插装，方法返回时插装，以及方法异常时的插装三个小类。为了控制篇幅，我们这里只展示了`SpyInterceptor1`这一个类。

`@AtEnter`注解有一个参数`inline`，这个参数控制的是插装的方法需不需要内连到目标方法中，如果点开`@AtEnter`这个注解，会发现它其实就是一个`InterceptorParserHander`的注解：

```java
@Documented
@Retention(RetentionPolicy.RUNTIME)
@java.lang.annotation.Target(ElementType.METHOD)
@InterceptorParserHander(parserHander = EnterInterceptorProcessorParser.class)
public @interface AtEnter {
    boolean inline() default true;

    Class<? extends Throwable> suppress() default None.class;

    Class<?> suppressHandler() default Void.class;

    class EnterInterceptorProcessorParser implements InterceptorProcessorParser {

        @Override
        public InterceptorProcessor parse(Method method, Annotation annotationOnMethod) {

            // LocationMatcher的作用就是避免重复插装
            LocationMatcher locationMatcher = new EnterLocationMatcher();

            AtEnter atEnter = (AtEnter) annotationOnMethod;

            return InterceptorParserUtils.createInterceptorProcessor(method,
                    locationMatcher,
                    atEnter.inline(),
                    atEnter.suppress(),
                    atEnter.suppressHandler());
        }
    }
}
```

在前面的分析中我们可以看到，注册注解器的工作实际上都会转发到`InterceptorParserHander`的`parse`方法，这个方法中做的事情比较简单，会把注解中的参数转发到`createInterceptorProcessor`这个方法中去：

```java
public static InterceptorProcessor createInterceptorProcessor(
        Method method,
        LocationMatcher locationMatcher,
        boolean inline,
        Class<? extends Throwable> suppress,
        Class<?> suppressHandler) {

    InterceptorProcessor interceptorProcessor = new InterceptorProcessor(method.getDeclaringClass().getClassLoader());

    //locationMatcher
    interceptorProcessor.setLocationMatcher(locationMatcher);

    //interceptorMethodConfig
    InterceptorMethodConfig interceptorMethodConfig = new InterceptorMethodConfig();
    interceptorProcessor.setInterceptorMethodConfig(interceptorMethodConfig);
    interceptorMethodConfig.setOwner(Type.getInternalName(method.getDeclaringClass()));
    interceptorMethodConfig.setMethodName(method.getName());
    interceptorMethodConfig.setMethodDesc(Type.getMethodDescriptor(method));

    //inline
    interceptorMethodConfig.setInline(inline);

    //bindings
    List<Binding> bindings = BindingParserUtils.parseBindings(method);
    interceptorMethodConfig.setBindings(bindings);

    //errorHandlerMethodConfig
    InterceptorMethodConfig errorHandlerMethodConfig = ExceptionHandlerUtils
            .errorHandlerMethodConfig(suppress, suppressHandler);
    if (errorHandlerMethodConfig != null) {
        interceptorProcessor.setExceptionHandlerConfig(errorHandlerMethodConfig);
    }

    return interceptorProcessor;
}
```

这个方法也是比较清晰的：

1. 给拦截器注册了一个过滤器，主要作用是防止重复插装
2. 新建了一个`InterceptorMethodConfig`类的实例，该实例包含了待插装方法的详细信息，这里面的`method`变量就是`com.taobao.arthas.core.advisor.SpyInterceptors.SpyInterceptor1#atEnter`方法
3. 接下来是非常重要的一步，为拦截器生成`Binding`对象，关于这个对象我们后面还会详细讲
4. 为拦截器生成异常处理器，原理和处理`Binding`注解类似

```java
public static List<Binding> parseBindings(Method method) {
    // 从 parameter 里解析出来 binding
    List<Binding> bindings = new ArrayList<Binding>();
    Annotation[][] parameterAnnotations = method.getParameterAnnotations();
    for (int parameterIndex = 0; parameterIndex < parameterAnnotations.length; ++parameterIndex) {
        Annotation[] annotationsOnParameter = parameterAnnotations[parameterIndex];
        for (int j = 0; j < annotationsOnParameter.length; ++j) {

            Annotation[] annotationsOnBinding = annotationsOnParameter[j].annotationType().getAnnotations();
            for (Annotation annotationOnBinding : annotationsOnBinding) {
                if (BindingParserHandler.class.isAssignableFrom(annotationOnBinding.annotationType())) {
                    BindingParserHandler bindingParserHandler = (BindingParserHandler) annotationOnBinding;
                    BindingParser bindingParser = InstanceUtils.newInstance(bindingParserHandler.parser());
                    Binding binding = bindingParser.parse(annotationsOnParameter[j]);
                    bindings.add(binding);
                }
            }
        }
    }
    return bindings;
}
```

`Binding`系列的的注解统一在这个方法中处理，原理就是将`BindingParserHandler`注解中的`parser`属性拿出来，然后调用该属性的`parse`方法，生成一个`Binding`对象

## Binding 系列的注解是如何工作的

从这一小节开始，我们的思路要转变一下，得从字节码的角度思考问题，因为我们的方法是运行时插装到目标方法中的，因此目标方法的一些运行时信息也要从运行时的代码形态-字节码获取，下面我们分析一下一些常见的`Binding`注解：

### `ArgsBinding`的工作原理

```java
@Documented
@Retention(RetentionPolicy.RUNTIME)
@java.lang.annotation.Target(ElementType.PARAMETER)
@BindingParserHandler(parser = ArgsBindingParser.class)
public static @interface Args {
    
    boolean optional() default false;

}
public static class ArgsBindingParser implements BindingParser {
    @Override
    public Binding parse(Annotation annotation) {
        return new ArgsBinding();
    }
    
}
```
通过前一节的分析，我们可以看到，最后返回的`Binding`对象是`ArgsBinding`，我们看下他的实现：
```java
public class ArgsBinding extends Binding {

    @Override
    public void pushOntoStack(InsnList instructions, BindingContext bindingContext) {
        AsmOpUtils.loadArgArray(instructions, bindingContext.getMethodProcessor().getMethodNode());
    }

    @Override
    public Type getType(BindingContext bindingContext) {
        return AsmOpUtils.OBJECT_ARRAY_TYPE;
    }

}
```
我们能看到的两个方法都是从`Binding`对象继承而来的，也就是说所有的`Binding`对象都是有这两个方法的，第二个方法比较好理解，标识了类型；第一个方法的作用是通过操作栈和本地变量表，将我们需要的信息保存到一个本地变量中。

```java
public static void loadArgArray(final InsnList instructions, MethodNode methodNode) {
    boolean isStatic = AsmUtils.isStatic(methodNode);
    Type[] argumentTypes = Type.getArgumentTypes(methodNode.desc);
    push(instructions, argumentTypes.length);
    newArray(instructions, OBJECT_TYPE);
    for (int i = 0; i < argumentTypes.length; i++) {
        //将前一步new出来的数组引用拷贝一份到栈顶
        dup(instructions);
        // 将数字i推向栈顶
        push(instructions, i);
        // 将本地变量表中的第i个便联系加载到栈顶
        loadArg(isStatic, instructions, argumentTypes, i);
        // 语法糖
        box(instructions, argumentTypes[i]);
        // 保存数组中的第i个元素
        arrayStore(instructions, OBJECT_TYPE);
    }
}
```

这段代码可以完全理解成就是一段字节码，我们能看到一些熟悉的字节码操作符比如`dup,push`等，唯一不同的就是每个操作符都会有一个`InsnList`的入参，这个入参是一个字节码指令的双向链表，因为我们要插装，所以我们的字节码也要存在这个列表中，然后插入到原本字节码中的某个位置，仅此而已。

这段代码在判断了待插装函数是否是静态的之后是直接`new`了一个`Object`类型的数组。

我们看下JVM Opcode Reference对`anewarray`指令的解释:

> Description: allocate a new array of objects
>
> Stack:
> | before |   after   |
> | :----: | :-------: |
> |  size  | array ref |
> | other  |   other   |

这个指令比较好理解，就是将栈顶的 size取出，作为新建数组的长度，在本地变量表中创建一个数组的引用，然后压栈，对比上面方法中for循环前面的代码，是完全吻合的。

`loadArg`的实现刚开始看起来可能有些不习惯，我们以一个很简单的程序片段举例，观察下它的本地变量表

```java
// Java程序片段
public String hello(String str, long num1, double num2);

// 对应字节码本地变量表
LocalVariableTable:
Start  Length  Slot  Name   Signature
    0     415     0  this   Lcom/example/Sample;
    0     415     1   str   Ljava/lang/String;
    0     415     2  num1   J
    0     415     4  num2   D
    94     321     6  num3   Ljava/lang/Long;
    313     102     7 result   Ljava/lang/String;
```

```java
public static void loadArg(boolean staticAccess, final InsnList instructions, Type[] argumentTypes, int i) {
    final int index = getArgIndex(staticAccess, argumentTypes, i);
    final Type type = argumentTypes[i];
    // 将本地变量表中偏移量为index，长度为`type.size()`的变量加载到栈上
    instructions.add(new VarInsnNode(type.getOpcode(Opcodes.ILOAD), index));
}

static int getArgIndex(boolean staticAccess, final Type[] argumentTypes, final int arg) {
    int index = staticAccess ? 0 : 1;
    for (int i = 0; i < arg; i++) {
        // 根据变量类型，计算第i个变量在本地变量表中的偏移量
        index += argumentTypes[i].getSize();
    }
    return index;
}
```

在详细讲程序流程之前我们先复习下下JVM Opcode Reference对`aastore`指令的解释:

>Description: store value in array[index]
>
>Stack:
> |  before  | after |
> | :------: | :---: |
> |  value   |       |
> |  index   |       |
> | arrayref |       |

这个指令也比较好理解，将value保存到arrayref[index]，调用这个指令前需要在栈上依次准备好`arrarref,index,value`三个值。有了这些储备知识，我们就可以看下`loadArgArray`这个方法是如何给数组赋值的
1. 将数组长度压栈：
> 此时的堆栈：
>
> | Length |
> | :----: |
> | other  |
>
> 
2. 调用`anewarray`指令创建一个新的数组引用，堆栈：
> 此时的堆栈：
>
> | Arrayref |
> | :------: |
> |  other   |
>
> 
3. 开始`for循环`
+ 将栈顶元复制一份：
> 此时的堆栈：
> | arrayref |
> | :------: |
> | arrayref |
+ 将循环变量i压入栈顶：
> 此时的堆栈：
> |    i     |
> | :------: |
> | arrayref |
> | arrayref |
+ 加载本地变量表中的第i个变量，如何加载见前文对`loadArg`这个函数的分
析：
> 此时的堆栈：
> | localvar |
> | :------: |
> |    i     |
> | arrayref |
> | arrayref |
+ 调用`aastore`指令，将`localvar`存到`arrayref[i]`,并开启下一轮循环
> 此时的堆栈：
>
> | arrayref |
> | :------: |
> |  other   |
>
> 

如此往复就能把方法的参数存到本地变量表中了，这里边还有一个`box`的过程，稍加复杂，限于篇幅不再详述，感兴趣的读者可以去arthas作者的这篇博客详细了解[应用诊断利器Arthas ByteKit 深度解读(2)：本地变量及参数绑定](https://hengyunabc.blog.csdn.net/article/details/107479267)

### ThisBinding的工作原理

有了前面对方法变量的分析，分析其他的`Binding`注解就会比较简单
```java
@Documented
@Retention(RetentionPolicy.RUNTIME)
@java.lang.annotation.Target(ElementType.PARAMETER)
@BindingParserHandler(parser = ThisBindingParser.class)
public static @interface This {

}

public static class ThisBindingParser implements BindingParser {
    @Override
    public Binding parse(Annotation annotation) {
        return new ThisBinding();
    }
    
}
public class ThisBinding extends Binding {

    @Override
    public void pushOntoStack(InsnList instructions, BindingContext bindingContext) {
        bindingContext.getMethodProcessor().loadThis(instructions);
    }

    @Override
    public Type getType(BindingContext bindingContext) {
        return Type.getType(Object.class);
    }

}

public void loadThis(final InsnList instructions) {
    if (isConstructor()) {
        // load this.
        loadVar(instructions, 0);
    } else {
        if (isStatic()) {
            // load null.
            loadNull(instructions);
        } else {
            // load this.
            loadVar(instructions, 0);
        }
    }
}
```
绑定this的这个过程比较简单，如果是一个非静态方法，`this`变量就是本地变量的第一个槽内，如果是静态方法，`this`方法就是一个`null`

类和方法信息都比较简单，一条简单的ldc命令就可以搞定，以类信息为例：
```java
public class ClassBinding extends Binding{

    @Override
    public void pushOntoStack(InsnList instructions, BindingContext bindingContext) {
        String owner = bindingContext.getMethodProcessor().getOwner();
        AsmOpUtils.ldc(instructions, Type.getObjectType(owner));
    }

    @Override
    public Type getType(BindingContext bindingContext) {
        return Type.getType(Class.class);
    }

}
```
故不再赘述

### Binding系列注解总结

用注解的方法实现对JVM运行时信息的拦截和抽取是arthas一个比较有亮点的地方，这部分代码也独立出来了一个单独的框架`bytekit`，本节对arthas对运行时信息的绑定原理进行了分析，让读者能够了解我们在arthas控制台输出的那些运行时信息是怎么来的。

## 拦截器InterceptorProcessor的工作原理
在第一节的分析中，我们可以看到，如果是需要插装的代码，则都会调用拦截器的`process`方法：

```java
public List<Location> process(MethodProcessor methodProcessor) throws Exception {
    List<Location> locations = locationMatcher.match(methodProcessor);

    List<Binding> interceptorBindings = interceptorMethodConfig.getBindings();

    for (Location location : locations) {
        // 有三小段代码，1: 保存当前栈上的值的 , 2: 插入的回调的 ， 3：恢复当前栈的
        InsnList toInsert = new InsnList();
        InsnList stackSaveInsnList = new InsnList();
        InsnList stackLoadInsnList = new InsnList();
        StackSaver stackSaver = null;
        if(location.isStackNeedSave()) {
            stackSaver = location.getStackSaver();
        }
        BindingContext bindingContext = new BindingContext(location, methodProcessor, stackSaver);

        if(stackSaver != null) {
            stackSaver.store(stackSaveInsnList, bindingContext);
            stackSaver.load(stackLoadInsnList, bindingContext);
        }


        Type methodType = Type.getMethodType(interceptorMethodConfig.getMethodDesc());
        Type[] argumentTypes = methodType.getArgumentTypes();
        // 检查回调函数的参数和 binding数一致
        if(interceptorBindings.size() != argumentTypes.length) {
            throw new IllegalArgumentException("interceptorBindings size no equals with interceptorMethod args size.");
        }

        // 把当前栈上的数据保存起来
        int fromStackBindingCount = 0;
        for (Binding binding : interceptorBindings) {
            if(binding.fromStack()) {
                fromStackBindingCount++;
            }
        }
        // 只允许一个binding从栈上保存数据
        if(fromStackBindingCount > 1) {
            throw new IllegalArgumentException("interceptorBindings have more than one from stack Binding.");
        }
        // 组装好要调用的 static 函数的参数
        for(int i = 0 ; i < argumentTypes.length; ++i) {
            Binding binding = interceptorBindings.get(i);
            binding.pushOntoStack(toInsert, bindingContext);
            // 检查 回调函数的参数类型，看是否要box一下 ，检查是否原始类型就可以了。
            // 只有类型不一样时，才需要判断。比如两个都是 long，则不用判断
            Type bindingType = binding.getType(bindingContext);
            if(!bindingType.equals(argumentTypes[i])) {
                if(AsmOpUtils.needBox(bindingType)) {
                    AsmOpUtils.box(toInsert, binding.getType(bindingContext));
                }
            }
        }

        // TODO 要检查 binding 和 回调的函数的参数类型是否一致。回调函数的类型可以是 Object，或者super。但是不允许一些明显的类型问题，比如array转到int
        toInsert.add(new MethodInsnNode(Opcodes.INVOKESTATIC, interceptorMethodConfig.getOwner(), interceptorMethodConfig.getMethodName(),
                interceptorMethodConfig.getMethodDesc(), false));
        if (!methodType.getReturnType().equals(Type.VOID_TYPE)) {
            if (location.canChangeByReturn()) {
                // 当回调函数有返回值时，需要更新到之前保存的栈上
                // TODO 这里应该有 type 的问题？需要检查是否要 box
                Type returnType = methodType.getReturnType();
                Type stackSaverType = stackSaver.getType(bindingContext);
                if (!returnType.equals(stackSaverType)) {
                    AsmOpUtils.unbox(toInsert, stackSaverType);
                }
                stackSaver.store(toInsert, bindingContext);
            } else {
                // 没有使用到回调函数的返回值的话，则需要从栈上清理掉
                int size = methodType.getReturnType().getSize();
                if (size == 1) {
                    AsmOpUtils.pop(toInsert);
                } else if (size == 2) {
                    AsmOpUtils.pop2(toInsert);
                }
            }
        }


        TryCatchBlock errorHandlerTryCatchBlock = null;
        // 生成的代码用try/catch包围起来
        if( exceptionHandlerConfig != null) {
            LabelNode gotoDest = new LabelNode();

            errorHandlerTryCatchBlock = new TryCatchBlock(methodProcessor.getMethodNode(), exceptionHandlerConfig.getSuppress());
            toInsert.insertBefore(toInsert.getFirst(), errorHandlerTryCatchBlock.getStartLabelNode());
            toInsert.add(new JumpInsnNode(Opcodes.GOTO, gotoDest));
            toInsert.add(errorHandlerTryCatchBlock.getEndLabelNode());
//                这里怎么把栈上的数据保存起来？还是强制回调函数的第一个参数是 exception，后面的binding可以随便搞。
            errorHandler(methodProcessor, toInsert);
            toInsert.add(gotoDest);
        }
        stackSaveInsnList.add(toInsert);
        stackSaveInsnList.add(stackLoadInsnList);
        if (location.isWhenComplete()) {
            methodProcessor.getMethodNode().instructions.insert(location.getInsnNode(), stackSaveInsnList);
        }else {
            methodProcessor.getMethodNode().instructions.insertBefore(location.getInsnNode(), stackSaveInsnList);
        }
        if( exceptionHandlerConfig != null) {
            errorHandlerTryCatchBlock.sort();
        }
        // inline callback
        if(interceptorMethodConfig.isInline()) {
            Class<?> forName = classLoader.loadClass(Type.getObjectType(interceptorMethodConfig.getOwner()).getClassName());
            MethodNode toInlineMethodNode = AsmUtils.findMethod(AsmUtils.loadClass(forName).methods, interceptorMethodConfig.getMethodName(), interceptorMethodConfig.getMethodDesc());

            methodProcessor.inline(interceptorMethodConfig.getOwner(), toInlineMethodNode);
        }
        if(exceptionHandlerConfig != null && exceptionHandlerConfig.isInline()) {
            Class<?> forName = classLoader.loadClass(Type.getObjectType(exceptionHandlerConfig.getOwner()).getClassName());
            MethodNode toInlineMethodNode = AsmUtils.findMethod(AsmUtils.loadClass(forName).methods, exceptionHandlerConfig.getMethodName(), exceptionHandlerConfig.getMethodDesc());

            methodProcessor.inline(exceptionHandlerConfig.getOwner(), toInlineMethodNode);
        }
    }
    
    return locations;
}
```
这部分代码比较长，如果我们跳过所有`stackSaver`的代码(后面会讲`stackSaver`)，那逻辑也是清晰的:
1. 调用`Binding`对象的`pushOntoStack`方法完成所有预定义的绑定，`pushOntoStack`方法我们在前文中有详细的说明
2. 调用插装方法
3. 如果注册了异常处理器，将异常处理器也插装到代码中
4. 看注解中指定了是否要内联，如果要内联，则将插装方法和异常处理器都做内联处理

`com.alibaba.bytekit.asm.MethodProcessor#inline`的实现比较长，感兴趣的同学可以看下，这里我们不过多阐述。

因为这个流程没有考虑`StackSaver`，所以相对简单，但是`StackSaver`的原理对于我们完整的理解整个拦截器的工作原理是必不可少的。

### `StackSaver`是如何工作的

`Location`的子类中实现`getStackSaver`方法的并不多，我们以`ExitLocation`为例：
```java
public StackSaver getStackSaver() {
    StackSaver stackSaver = new StackSaver() {

        @Override
        public void store(InsnList instructions, BindingContext bindingContext) {
            Type returnType = bindingContext.getMethodProcessor().getReturnType();
            if(!returnType.equals(Type.VOID_TYPE)) {
                LocalVariableNode returnVariableNode = bindingContext.getMethodProcessor().initReturnVariableNode();
                AsmOpUtils.storeVar(instructions, returnType, returnVariableNode.index);
            }
        }

        @Override
        public void load(InsnList instructions, BindingContext bindingContext) {
            Type returnType = bindingContext.getMethodProcessor().getReturnType();
            if(!returnType.equals(Type.VOID_TYPE)) {
                LocalVariableNode returnVariableNode = bindingContext.getMethodProcessor().initReturnVariableNode();
                AsmOpUtils.loadVar(instructions, returnType, returnVariableNode.index);
            }
        }

        @Override
        public Type getType(BindingContext bindingContext) {
            return bindingContext.getMethodProcessor().getReturnType();
        }
        
    };
    return stackSaver;
}

// 会在本地变量表中新增一个变量
public LocalVariableNode initReturnVariableNode() {
    if (returnVariableNode == null) {
        returnVariableNode = this.addInterceptorLocalVariable(returnVariableName, returnType.getDescriptor());
    }
    return returnVariableNode;
}
```
在调用`store`方法的时候会先在本地变量表新增一个变量，然后将栈顶的变量保存到这个新增的变量中，然后在调用`load`之后又会将这个本地变量中的值加载到栈顶，我们再回到拦截器的实现，在这个实现中关于`stackSaver`的流程我专门摘出来并做了详细的注释，希望能帮助大家更好的理解这个过程。
```java
// ...
// 如果子类实现了getStackSaver，则做两件事
if(stackSaver != null) {
    // 1. 将栈顶元素保存到本地变量表，实际上是把这条指令存到stackSaveInsnList
    stackSaver.store(stackSaveInsnList, bindingContext);
    // 2. 从本地变量表加载变量到栈顶，也是把指令存到stackLoadInsnList上，需要注意的是，这是两个不同的列表
    stackSaver.load(stackLoadInsnList, bindingContext);
}
// ...
// 以ExistLocation为例，原先函数的返回值可以被插装函数替换
// 因为前文已经调用插装函数，JVM指令执行到这里是插装函数刚调用完毕的状态
// 此时栈顶就是插装函数的返回值，用store指令保存栈顶元素就可以覆盖之前保存的
// 原方法的返回值，以达到替换的目的
if (location.canChangeByReturn()) {
    // 当回调函数有返回值时，需要更新到之前保存的栈上
    // TODO 这里应该有 type 的问题？需要检查是否要 box
    Type returnType = methodType.getReturnType();
    Type stackSaverType = stackSaver.getType(bindingContext);
    if (!returnType.equals(stackSaverType)) {
        AsmOpUtils.unbox(toInsert, stackSaverType);
    }
    stackSaver.store(toInsert, bindingContext);
} else {
    // 没有使用到回调函数的返回值的话，则需要从栈上清理掉
    int size = methodType.getReturnType().getSize();
    if (size == 1) {
        AsmOpUtils.pop(toInsert);
    } else if (size == 2) {
        AsmOpUtils.pop2(toInsert);
    }
}
// ...
stackSaveInsnList.add(toInsert);
// toInsert中的最后一个指令是store，会清除栈顶的元素
// 但是根据JVM规范，函数调用完成后栈顶的元素一定是返回值
// 因此再调用load指令把函数的返回值加载到栈上
stackSaveInsnList.add(stackLoadInsnList);
```

### 异常处理器的插装

```java
// 生成的代码用try/catch包围起来
if( exceptionHandlerConfig != null) {
    LabelNode gotoDest = new LabelNode();
    errorHandlerTryCatchBlock = new TryCatchBlock(methodProcessor.getMethodNode(), exceptionHandlerConfig.getSuppress());
    toInsert.insertBefore(toInsert.getFirst(), errorHandlerTryCatchBlock.getStartLabelNode());
    toInsert.add(new JumpInsnNode(Opcodes.GOTO, gotoDest));
    toInsert.add(errorHandlerTryCatchBlock.getEndLabelNode());
    errorHandler(methodProcessor, toInsert);
    toInsert.add(gotoDest);
}
```
异常处理器的插装比较简单，核心思路就是将我们刚才插入的代码用一个try-catch包起来，类似于下面这段伪指令：
```java
startLabel
instrument instruction
goto dest
endLabel
errorHandler
dest
```

## 总结

代码插装的设计是arthas的核心逻辑，也是他的核心资产，所以这部分内容比较多，也比较复杂，读本篇文章时思路一定要转换过来，从字节码的角度去思考java代码。本文力图用最简单的例子帮助大家理解每条插装语句的用意，可能并不能涵盖arthas插装的所有场景，读者可结合本文中的例子和arthas的源码进行更深入的探索。

下篇文章将以本文的字节码插装为基础，讲述arthas具体的命令是如何实现的，预计包含`watch,trace,tt,monitor,stack`五条指令，敬请期待。