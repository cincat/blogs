## 前言

在深入到 Arthas 的原理之前我们先看一个有趣的示例，我们依然使用前面文章中用到的代码示例,

```java
  
 public static void main(String[] args) {  
  for (int i = 0; i < Integer.MAX_VALUE; i++) {  
   System.out.println("times:" + i + " , result:" + testExceptionTrunc());  
  }  
 }  
  
 public static boolean testExceptionTrunc()  {  
  try {  
   // 人工构造异常抛出的场景  
   ((Object)null).getClass();  
  } catch (Exception e) {  
   if (e.getStackTrace().length == 0) {  
    try {  
     // 堆栈消失的时候当前线程休眠5秒，便于观察  
     Thread.sleep(5000);  
    } catch (InterruptedException interruptedException) {  
     // do nothing  
    }  
    return true;  
   }  
  }  
  return false;  
 }  
}  
```

运行这段代码，然后我们使用 Arthas 的`tt`命令记录`testExceptionTrunc`的每次调用的情况

`tt -t com.idealism.demo.DemoApplication testExceptionTrunc  
`

再新开一个窗口，打开 Arthas，使用`dump`命令把正在运行的字节码输出到本地文件后查看此时的字节码
![插装后的字节码](https://gitee.com/cincat/picgo/raw/master/img/插装后的字节码.png)


可以看到，现在正在运行的字节码和我们从源码编译过来的相比多了两行，多的这两行正是 Arthas 插装的代码，Arthas 的一切魔法都从这里开始。

## 实现一个极简的 watch 命令

给运行中的代码插装新的代码片段，这个特性 JVM 从 SE6 就已经开始支持了，所有有关代码插装的 API 都在`java.lang.instrument.Instrumentation`这个包中。有了 JVM 的支持和 Arthas 的启发，我们可以借助代码插装实现一个极简版的`watch`命令，这样的一个小工具有以下特点：

- 可以统计被插装方法的运行时间
- 被插装的代码不感知该工具的存在，该工具动态 attach 到目标类的 JVM 中
- 为了示例简单明了，只实现计时功能，不实现`watch`的其他功能

由于插装代码是一件过于底层且需要对字节码有很高的掌握度，所以我们引入了一个二方包`javassist`来做具体的插装工作，maven 坐标如下：

```java
<dependency>  
   <groupId>org.javassist</groupId>  
   <artifactId>javassist</artifactId>  
   <version>3.21.0-GA</version>  
</dependency>  
```

我们会使用 javassist 最基础的功能，详细的使用教程请参考https://www.baeldung.com/javassist我们的目标类是我们前面文章中一直使用的 Demo 类，代码如下：

```java
public class DemoApplication {  
  
 private static Logger LOGGER = LoggerFactory.getLogger(DemoApplication.class);  
  
 public static void main(String[] args) {  
  for (int i = 0; i < Integer.MAX_VALUE; i++) {  
//   System.out.println("times:" + i + " , result:" + testExceptionTrunc());  
   testExceptionTruncate();  
  }  
 }  
  
 public static void testExceptionTruncate()  {  
  try {  
   // 人工构造异常抛出的场景  
   ((Object)null).getClass();  
  } catch (Exception e) {  
   if (e.getStackTrace().length == 0) {  
    System.out.println("stack miss;");  
    try {  
     // 堆栈消失的时候当前线程休眠5秒，便于观察  
     Thread.sleep(5000);  
    } catch (InterruptedException interruptedException) {  
     // do nothing  
    }  
   }  
  }  
  System.out.println("stack still exist;");  
 }  
}  
```

为了方便插装代码打印日志，我们引入了一个静态的`LOGGER`,并且将`testExceptionTruncate`改为返回`void`类型的返回值，这样的改动让代码插装更加简单。如何让两个运行中的 JVM 建立连接呢，JVM 通过`attach api`支持了这种场景

```java
public static void run(String[] args) {  
  String agentFilePath = "/Users/jnzh/Documents/Idea Project/agent/out/agent.jar";  
  String applicationName = "com.idealism.demo.DemoApplication";  
  
  //iterate all jvms and get the first one that matches our application name  
  Optional<String> jvmProcessOpt = Optional.ofNullable(VirtualMachine.list()  
    .stream()  
    .filter(jvm -> {  
     LOGGER.info("jvm:{}", jvm.displayName());  
     return jvm.displayName().contains(applicationName);  
    })  
    .findFirst().get().id());  
  
  if(!jvmProcessOpt.isPresent()) {  
   LOGGER.error("Target Application not found");  
   return;  
  }  
  File agentFile = new File(agentFilePath);  
  try {  
   String jvmPid = jvmProcessOpt.get();  
   LOGGER.info("Attaching to target JVM with PID: " + jvmPid);  
   VirtualMachine jvm = VirtualMachine.attach(jvmPid);  
   jvm.loadAgent(agentFile.getAbsolutePath());  
   jvm.detach();  
   LOGGER.info("Attached to target JVM and loaded Java agent successfully");  
  } catch (Exception e) {  
   throw new RuntimeException(e);  
  }  
 }  
```

运行这个方法的 JVM 通过名称匹配目标 JVM，然后通过`attach`方法与目标 JVM 取得联系，继而对目标 JVM 发出指令，让其挂载插装`agent`，整个过程如下图所示：

![](https://mmbiz.qpic.cn/mmbiz_svg/Xmnun9Io49QRjtQwaKpnWV7FrG5lDj9dExz5A1pSsHZtmu8sxVVkxlhsMLMVTJWXXxFhEOYG0zWw6qiaASWOzW3owS2nwQAGc/640?wx_fmt=svg)

在我们反复提的 agent 里，我们才真正做代码插装的工作，`attach API`中要求，被目标代码挂载的`agent`包必须实现`agentmain` 且在打包的 MANIFEST.MF 中指定 Agent-Class 属性，完整的 MANIFEST.MF 文件如下所示：

```Manifest-Version: 1.0  
Main-Class: com.idealism.agent.AgentApplication  
Agent-Class: com.idealism.agent.AgentApplication  
Can-Redefine-Classes: true  
Can-Retransform-Classes: true  
```

有个小插曲，用 IDEA 打 JAR 包的时候，指定的 MANIFEST.MF 的路径到`${ProjectName}/src`就可以，默认的需要删掉框中的路径，否则，打出来的 MANIFEST.MF 文件不会生效。
![WX20201110-090506@2x](https://gitee.com/cincat/picgo/raw/master/img/WX20201110-090506@2x.png)
在`agentmain`方法中我们实现了对目标类的插装，目标 JVM 在被`attach`后会自动调用这个方法：

```java
public static void agentmain(String agentArgs, Instrumentation inst) {  
  LOGGER.info("[Agent] In agentmain method");  
  
  String className = "com.idealism.demo.DemoApplication";  
  transformClass(className,inst);  
 }  
```

`transformClass`方法做了一层转发：

```java
private static void transformClass(String className, Instrumentation instrumentation) {  
  Class<?> targetCls = null;  
  ClassLoader targetClassLoader = null;  
  // see if we can get the class using forName  
  try {  
   targetCls = Class.forName(className);  
   targetClassLoader = targetCls.getClassLoader();  
   transform(targetCls, targetClassLoader, instrumentation);  
   return;  
  } catch (Exception ex) {  
   LOGGER.error("Class [{}] not found with Class.forName");  
  }  
  // otherwise iterate all loaded classes and find what we want  
  for(Class<?> clazz: instrumentation.getAllLoadedClasses()) {  
   if(clazz.getName().equals(className)) {  
    targetCls = clazz;  
    targetClassLoader = targetCls.getClassLoader();  
    transform(targetCls, targetClassLoader, instrumentation);  
    return;  
   }  
  }  
  throw new RuntimeException("Failed to find class [" + className + "]");  
 }  
```

最后的流程会调用方法：

```java
private static void transform(Class<?> clazz, ClassLoader classLoader, Instrumentation instrumentation) {  
  TimeWatcherTransformer dt = new TimeWatcherTransformer(clazz.getName(), classLoader);  
  instrumentation.addTransformer(dt, true);  
  try {  
   instrumentation.retransformClasses(clazz);  
  } catch (Exception ex) {  
   throw new RuntimeException("Transform failed for class: [" + clazz.getName() + "]", ex);  
  }  
 }  
```

在这里我们实现了一个`TimeWatcherTransformer`并将代码插装的工作委托给它来做：

```java
public byte[] transform(ClassLoader loader, String className, Class<?> classBeingRedefined,  
                            ProtectionDomain protectionDomain, byte[] classfileBuffer) throws IllegalClassFormatException {  
        byte[] byteCode = classfileBuffer;  
  
        String finalTargetClassName = this.targetClassName.replaceAll("\\.", "/"); //replace . with /  
        if (!className.equals(finalTargetClassName)) {  
            return byteCode;  
        }  
  
        if (className.equals(finalTargetClassName) && loader.equals(targetClassLoader)) {  
            LOGGER.info("[Agent] Transforming class DemoApplication");  
            try {  
                ClassPool cp = ClassPool.getDefault();  
                CtClass cc = cp.get(targetClassName);  
                CtMethod m = cc.getDeclaredMethod(TEST_METHOD);  
                m.addLocalVariable("startTime", CtClass.longType);  
                m.insertBefore("startTime = System.currentTimeMillis();");  
  
                StringBuilder endBlock = new StringBuilder();  
  
                m.addLocalVariable("endTime", CtClass.longType);  
                m.addLocalVariable("opTime", CtClass.longType);  
                endBlock.append("endTime = System.currentTimeMillis();");  
                endBlock.append("opTime = (endTime-startTime)/1000;");  
  
                endBlock.append("LOGGER.info(\"[Application] testExceptionTruncate completed in:\" + opTime + \" seconds!\");");  
  
                m.insertAfter(endBlock.toString());  
  
                byteCode = cc.toBytecode();  
                cc.detach();  
            } catch (NotFoundException | CannotCompileException | IOException e) {  
                LOGGER.error("Exception", e);  
            }  
        }  
        return byteCode;  
    }  
```

有了JVM的支持，我们实现一个简单的watch命令也不难，只需要在目标方法的前后插入时间语句就可以了，目标JVM在attach了我们的agent后会输出本次调用的时间，如下图所示：

![img](https://gitee.com/cincat/picgo/raw/master/img/7e69ff846b355e51f3a9e5563523fa98.png)![点击并拖拽以移动](data:image/gif;base64,R0lGODlhAQABAPABAP///wAAACH5BAEKAAAALAAAAAABAAEAAAICRAEAOw==)

## JVM Attach 机制的实现

在前面的例子里我们之所以可以在一个 JVM 中发送指令让另一个 JVM 加载 Agent，是因为 JVM 通过 Attach 机制提供了一种进程间通信的方式，http://lovestblog.cn/blog/2014/06/18/jvm-attach/\?spm=ata.13261165.0.0.26d52428n8NoAy 详细的讲述了 Attach 机制是如何在 Linux 平台下实现的，结合我们之前的例子，可以把整个过程总结为如下的一张图：

![](https://mmbiz.qpic.cn/mmbiz_svg/Xmnun9Io49QRjtQwaKpnWV7FrG5lDj9dPxgfibrfx4mOwo49Zx2yM0YqGqiaS4ayL69eypVxnHd7W4FZOkTib5wMMiaqjFp3jlvJ/640?wx_fmt=svg)

在 GVM 调用`attach`的时候如果发现没有`java_pid`这个文件，则开始启动`attach`机制，首先会创建一个`attach_pid`的文件，这个文件的主要作用是用来鉴权。然后向`Signal Dispacher`发送`BREAK`信号，之后就一直在轮询等待`java_pid`这个套接字。`Signal Dispacher`中注册的信号处理器`Attach Listener`中首先会校验`attach_pid`这个文件的 uid 是否和当前 uid 一致，鉴权通过后才会创建`attach_pid`建立通信通道。