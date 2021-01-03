# Arthas原理系列(六)：从源码看trace命令的限制与用法



## 前言

`trace`命令是Artahs很有特色的命令，**对匹配到的method内的 子method 做统计**，并且以树状的形式打印出来，比如代码：

```java
import java.util.concurrent.TimeUnit;

public class Demo {
    public static void main(String[] args) throws InterruptedException {
        Demo demo = new Demo();
        while (true) {
            TimeUnit.SECONDS.sleep(1);
            demo.hello();
        }
    }
    public void hello() {
        System.out.println(this.getClass().getName() + "hello");
    }
}
```

```shell
$ trace Demo hello
Press Q or Ctrl+C to abort.
Affect(class-cnt:1 , method-cnt:1) cost in 108 ms.
`---ts=2019-03-22 18:49:44;thread_name=main;id=1;is_daemon=false;priority=5;TCCL=sun.misc.Launcher$AppClassLoader@4e25154f
    `---[1.477191ms] Demo:hello()
        +---[0.035988ms] java.lang.Object:getClass()
        +---[0.01844ms] java.lang.Class:getName()
        +---[0.018744ms] java.lang.String:valueOf()
        +---[0.023555ms] java.lang.StringBuilder:<init>()
        +---[0.022698ms] java.lang.StringBuilder:append()
        +---[0.014707ms] java.lang.StringBuilder:toString()
        `---[0.091016ms] java.io.PrintStream:println()
```

注：以上例子来源于https://github.com/alibaba/arthas/issues/597

细心的同学可能已经发现了，trace命令只能统计一级子调用，比如`System.out.println`的实现中可能还调用了其他的方法，但是结果中并没有打印出来，如果要观察多级子调用，就要在观察条件中把所有要观察的类和方法都要列一下，比如这样：`trace -E 'Demo\$Hello|Demo\$ClassB|Demo\$ClassC' 'hello|test'`。今天我们就从源码的角度看一下`trace`命令的以下几个特征：

1. 为什么只支持观察一级子调用
2. `E`参数如何保证树状输出
3. 文章写完再总结下

