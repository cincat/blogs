JDK16已于北美时间3月16日发布，JDK的这次更新总共带来了17个全新的特性
## 1. 支持类型匹配的`instanceof`
```java
if (obj instanceof String) {
    String s = (String) obj;    // grr...
    ...
}
```
这样的类型转换在JDK16中的写法是：
```java
if (obj instanceof String s) {
    // Let pattern matching do the work!
    // varialble s can be used here
    ...
}
```
如果obj的真实类型是String，则变量s可以在if语句中使用，但是如果obj的类型不是String,则s不能用在后续的变量命名中：
```java
// a is not instance of Point
if (a instanceof Point p) {
   ...
}
if (b instanceof Point p) {         // ERROR - p is in scope
   ...
}
```
## 2. `record`关键字
对于一些POJO类，我们往往需要这样写
```java
class Point {
    private final int x;
    private final int y;

    Point(int x, int y) {
        this.x = x;
        this.y = y;
    }

    int x() { return x; }
    int y() { return y; }

    public boolean equals(Object o) {
        if (!(o instanceof Point)) return false;
        Point other = (Point) o;
        return other.x == x && other.y == y;
    }

    public int hashCode() {
        return Objects.hash(x, y);
    }

    public String toString() {
        return String.format("Point[x=%d, y=%d]", x, y);
    }
}
```
在引入了`record`关键字之后，上面的代码可以简化为：
```java
record Point(int x, int y) { }
```
如果对类的属性初始化的时候有定制逻辑，也是支持的
```java
record Rational(int num, int denom) {
    Rational {
        int gcd = gcd(num, denom);
        num /= gcd;
        denom /= gcd;
    }
}
```
## 3.全并发的ZGC
与CMS中的ParNew和G1类似，ZGC也采用标记-复制算法，不过ZGC对该算法做了重大改进：ZGC在标记、转移和重定位阶段几乎都是并发的，这是ZGC实现停顿时间小于10ms目标的最关键原因。

![img](https://gitee.com/cincat/picgo/raw/master/img/40838f01e4c29cfe5423171f08771ef8156393-20210317221924754.png@1812w_940h_80q)

ZGC只有三个STW阶段：初始标记，再标记，初始转移。其中，初始标记和初始转移分别都只需要扫描所有GC Roots，其处理时间和GC Roots的数量成正比，一般情况耗时非常短；再标记阶段STW时间很短，最多1ms，超过1ms则再次进入并发标记阶段。即，ZGC几乎所有暂停都只依赖于GC Roots集合大小，停顿时间不会随着堆的大小或者活跃对象的大小而增加。与ZGC对比，G1的转移阶段完全STW的，且停顿时间随存活对象的大小增加而增加。
## 4. 可弹性伸缩的元数据区
JDK16对元数据区切分为更小的内存块，并将不再使用的内存快速返还给操作系统，对于频繁加载和卸载类的应用来说这一优化可以产生大量的空闲内存，提升整个JVM的性能
## 5. 支持Unix套接字
在2019 Windows Server和Windows 10提供了对Unix套接字的支持，Unix套接字常用语本地进程之间通信，相比于TCP协议，本地进程使用Unix套接字可以更高效安全的通信。JDK16新增了一个适配Unix套接字的新接口` java.net.UnixDomainSocketAddress`用于支持这一特性
## 6. 新的打包工具`jpackage`

支持将Java程序打包为对应平台的可执行程序
* linux: deb和rpm
* mac: pkg和dmg
* Windows: msi和exe
假如我们在lib目录下有一个jar包组成的应用，并且`main.jar`包含`main`方法，则可以使用下面的语句产生对应平台的可执行程序
```shell
jpackage --name myapp --input lib --main-jar main.jar
```
如果`main.jar`的MANIFEST.MF没有指定`main`函数，则需要在命令行中指定
```shell
jpackage --name myapp --input lib --main-jar main.jar \
  --main-class myapp.Main
```
## 7. 针对`Value-Based`类的编译器`warning`提示

对于基本类型的包装类，JDK16提供了两种新的编译器`warning`提示
* 包装类的构造函数在JDK9已经被废弃，如果在程序中继续使用，则编译器会报`warning`提示
* 如果包装类作为关键字`synchronized`的参数使用，则也会收到编译器的`warning提示`
* 如果接口类作为关键字`synchronized`的参数使用，则会收到`javac`编译器的`warning`提示
举例：
```java
Double d = 20.0;
synchronized (d) { ... } // javac warning & HotSpot warning
Object o = d;
synchronized (o) { ... } // HotSpot warning
```
## 8. 对JDK内部方法提供强制的封装
这个更新目的是为了引导开发者放弃使用JDK内部类转为使标准的API接口，除了例如`sun.misc.Unsafe`这样内部关键的接口之外，其他所有内部元素都提供默认的封装。使用了JDK内部接口的代码再JDK16下编译会失败，JVM参数-–illegal-access能够控制这一行为，要知道从JDK9到JDK15，这个参数默认的值都是`warning`,而现在已经变成了`deny`
## 9. 提供向量计算的API
志强向量计算的API在JDK中是缺失的，常见的二方库有`colt`和`commons-math3`,这些二方库在版本老旧，在易用性上也比较差，此次JDK16引入的向量计算的API针对多数现代CPU使用的`SIMD`指令进行了优化，大幅提升了计算性能
## 10. 对原生代码的调用提供更方便的支持
相比于JNI，提供更方便的方法用于调用原生代码，比如我们想在Java代码中调用`size_t strlen(const char *s);`这个原生C函数，我们只需要这样写：
```java
MethodHandle strlen = CLinker.getInstance().downcallHandle(
        LibraryLookup.ofDefault().lookup("strlen"),
        MethodType.methodType(long.class, MemoryAddress.class),
        FunctionDescriptor.of(C_LONG, C_POINTER)
    );
```
## 11. 提供操作外部内存的能力
JDK16通过`VarHandle`这个类的实例来引用外部内存区域，如果我们想初始化一段外部的内存区域，可以这样写：
```java
VarHandle intHandle = MemoryHandles.varHandle(int.class,
        ByteOrder.nativeOrder());

try (MemorySegment segment = MemorySegment.allocateNative(100)) {
    for (int i = 0; i < 25; i++) {
        intHandle.set(segment, i * 4, i);
    }
}
```
## 12. 提供限制可以继承此类的关键字`sealed`和`permits`
在JDK16中，提供了一种比访问修饰符更精细的控制手段：可以指定可以继承或者实现当前类或者接口的类，这个能力是通过关键字`sealed`和`permits`实现的
```java
public abstract sealed class Shape 
    permits com.example.polar.Circle,
            com.example.quad.Rectangle,
            com.example.quad.simple.Square { ... }
```
比如在上面的这个例子中，类`Shape`只能限定被`Circle`,`Rectangle`和`Square`继承