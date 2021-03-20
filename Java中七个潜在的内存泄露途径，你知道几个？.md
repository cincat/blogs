# Java中七个潜在的内存泄露途径，你知道几个？

虽然Java程序员不用像C/C++程序员那样时刻关注内存的使用情况，JVM会帮我们处理好这些，但并不是说有了GC就可以高枕无忧，内存泄露相关的问题一般在测试的时候很难发现，一旦上线流量起来，立刻就是一个线上故障。

## 1. 内存泄露的定义

如果GC无法回收内存中不再使用的对象，则定义为内存有泄露

## 2. 静态变量

不同于一般临时变量的生命周期，类的静态变量的生命周期与类的声明周期一直，而大多数类的生命周期一直持续到整个程序结束，因此，如果设计到大对象或者批量对象，请谨慎使用静态变量

```java
public class StaticTest {
    public static List<Double> list = new ArrayList<>();

    public void populateList() {
        for (int i = 0; i < 10000000; i++) {
            list.add(Math.random());
        }
        Log.info("Debug Point 2");
    }

    public static void main(String[] args) {
        Log.info("Debug Point 1");
        new StaticTest().populateList();
        Log.info("Debug Point 3");
    }
}

```

## 3. 未关闭的资源类

当我们在程序中打开一个新的流或者是新建一个网络连接的时候，JVM都会为这些资源类分配内存做缓存，常见的资源类有网络连接，数据库连接以及IO流。值得注意的是，如果在业务处理中异常，则有可能导致程序不能执行关闭资源类的代码，因此最好按照下面的做法处理资源类

```java
public void handleResource() {
    try {
        // open connection
        // handle business
    } catch (Throwable t) {
        // log stack
    } finally {
        // close connection
    }
}
```

## 4. 未正确实现`equals()`和`hashCode()`

假如有下面的这个类

```java
public class Person {
    public String name;
    
    public Person(String name) {
        this.name = name;
    }
}
```

并且如果在程序中有下面的操作

```java
@Test
public void givenMap_whenEqualsAndHashCodeNotOverridden_thenMemoryLeak() {
    Map<Person, Integer> map = new HashMap<>();
    for(int i=0; i<100; i++) {
        map.put(new Person("jon"), 1);
    }
    Assert.assertFalse(map.size() == 1);
}
```

可以预见，这个单元测试并不能通过，原因是`Person`类没有实现`equals`方法，因此使用`Object`的`equals`方法，直接比较实体对象的地址，所以`map.size() == 100`

如果我们改写`Person`类的代码如下所示：

```java
public class Person {
    public String name;
    
    public Person(String name) {
        this.name = name;
    }
    
    @Override
    public boolean equals(Object o) {
        if (o == this) return true;
        if (!(o instanceof Person)) {
            return false;
        }
        Person person = (Person) o;
        return person.name.equals(name);
    }
    
    @Override
    public int hashCode() {
        int result = 17;
        result = 31 * result + name.hashCode();
        return result;
    }
}
```

则上文中的单元测试就可以顺利通过了，需要注意的是这个场景比较隐蔽，一定要在平时的代码中注意。

## 5. 非静态内部类

要知道，所有的非静态类别类都持有外部类的引用，因此某些情况如果引用内部类可能延长外部类的生命周期，甚至持续到进程结束都不能回收外部类的空间，这类内存溢出一般在Android程序中比较多，只要`MyAsyncTask`处于运行状态`MainActivity`的内存就释放不了，很多时候安卓开发者这样做只是为了在内部类中拿到外部类的属性，殊不知，此时内存已经泄露了。

```java
public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
        new MyAsyncTask().execute();
    }

    private class MyAsyncTask extends AsyncTask {
        @Override
        protected Object doInBackground(Object[] params) {
            return doSomeStuff();
        }
        private Object doSomeStuff() {
            //do something to get result
            return new MyObject();
        }
    }
}
```



## 6. 重写了`finalize()`的类

如果运行下面的这个例子，则最终程序会应为OOM的原因崩溃

```java
public class Finalizer {
    @Override
    protected void finalize() throws Throwable {
    while (true) {
           Thread.yield();
      }
  }

public static void main(String str[]) {
  while (true) {
        for (int i = 0; i < 100000; i++) {
            Finalizer force = new Finalizer();
        }
   }
 }
}

```

JVM对重写了`finalize()`的类的处理稍微不同，首先会针对这个类创建一个java.lang.ref.Finalizer `类，并让`java.lang.ref.Finalizer `持有这个类的引用，在上文中的例子中，因为`Finalizer`类的引用被`java.lang.ref.Finalizer `持有，所以他的实例并不能被Young GC清理，反而会转入到老年代。在老年代中，JVM GC的时候会发现`Finalizer`类只被`java.lang.ref.Finalizer `引用，因此将其标记为可GC状态，并放入到`*java.lang.ref.Finalizer.ReferenceQueue*`这个队列中。当所有的这一切都结束之后，JVM会起一个后台线程去清理`java.lang.ref.Finalizer.ReferenceQueue`中的对象，并且如果队列中有新的对象，也会马上去清理的。这个设计看起来是没什么问题的，但其实有个坑，那就是负责清理`java.lang.ref.Finalizer.ReferenceQueue`的后台线程优先级是比较低的，并且系统没有提供可以调节这个线程优先级的接口或者配置。因此当我们在使用使用重写`finalize()`方法的对象时，千万不要瞬间产生大量的对象，要时刻谨记，JVM对此类对象的处理有特殊逻辑。

## 7. 针对长字符串调用`String.intern()`

如果提前在`src/test/resources/large.txt`中写入大量字符串，并且在Java 1.6及以下的版本运行下面程序，也将得到一个OOM

```java
@Test
public void givenLengthString_whenIntern_thenOutOfMemory()
  throws IOException, InterruptedException {
    String str 
      = new Scanner(new File("src/test/resources/large.txt"), "UTF-8")
      .useDelimiter("\\A").next();
    str.intern();
    
    System.gc(); 
    Thread.sleep(15000);
}
```

原因是在Java 1.6及以下，字符串常量池是处于JVM的`PermGen`区的，并且在程序运行期间不会GC，因此产生了OOM。在Java 1.7以及之后字符串常量池转移到了`HeapSpace`此类问题也就无需再关注了

## 8. `ThreadLocal`的误用

ThreadLocal一定要列在Java内存泄露的榜首，总能在不知不觉中将内存泄露掉，一个常见的例子是：

```java
@Test
public void testThreadLocalMemoryLeaks() {
    ThreadLocal<List<Integer>> localCache = new ThreadLocal<>();
  	List<Integer> cacheInstance = new ArrayList<>(10000);
    localCache.set(cacheInstance);
    localCache = new ThreadLocal<>();
}
```

当`localCache`的值被重置之后`cacheInstance`被`ThreadLocalMap`中的`value`引用，无法被GC，但是其`key`对`ThreadLocal`实例的引用是一个弱引用，本来`ThreadLocal`的实例被`localCache`和`ThreadLocalMap`的`key`同时引用，但是当`localCache`的引用被重置之后，则`ThreadLocal`的实例只有`ThreadLocalMap`的`key`这样一个弱引用了，此时这个实例在GC的时候能够被清理。

![img](https://gitee.com/cincat/picgo/raw/master/img/format,png.jpeg)

其实看过`ThreadLocal`源码的同学会知道，`ThreadLocal`本身对于`key`为`null`的`Entity`有自清理的过程，但是这个过程是依赖于后续对`ThreadLocal`的继续使用，假如上面的这段代码是处于一个秒杀场景下，会有一个瞬间的流量峰值，这个流量峰值也会将集群的内存打到高位(或者运气不好的话直接将集群内存打满导致故障)，后面由于峰值流量已过，对`ThreadLocal`的调用也下降，会使得`ThreadLocal`的自清理能力下降，造成内存泄露。`ThreadLocal`的自清理实现是锦上添花，千万不要指望他雪中送碳。

网络场景中ThreadLocal`造成的内存泄露情况相对更加复杂，大家可以参考Tomcat官方的总结：https://cwiki.apache.org/confluence/display/tomcat/MemoryLeakProtection，后续有机会我们会对这类内存泄露做更细致的分析。

