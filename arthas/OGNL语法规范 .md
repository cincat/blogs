## 1 什么是OGNL

OGNL\(Object-Graph Navigation Language\)是一种表达式语言\(EL\)，简单来说就是一种简化了的Java属性的取值语言，在传统的Struts框架，以及MyBatis中都有大量使用，开源Java诊断利器Arthas也使用它做表达式过滤，简单但不失灵活的设计衍生出来了好多高阶玩法。

## 2 基础语法简介

OGNL生来就是为了简化Java属性的取值，比如想根据名称name引用当前上下文环境中的对象，则直接键入即可，如果想要引用当前上下文环境中对象text的属性title，则键入text.title即可。如果想引用对象的非值属性，OGNL也是支持的：

| 属性类型 | 举例                                      |
| :------- | :---------------------------------------- |
| 属性名称 | 比如上文中的name                          |
| 属性方法 | hashcode\(\)返回当前对象的哈希值          |
| 数组索引 | arrays\[0\]返回返回arrays数组的第一个元素 |

OGNL表达式是可以前后连接起来的，比如`name.toCharArray()[0].numericValue.toString()`

## 3 表达式

### 3.1 常量

OGNL表达式规定有以下几种常量

1.  以单引号或双引号括起来的字符串
2.  以单引号括起来的字符
3.  数字常量，除了Java语法中的常量外，OGNL还定义了以B或者b结尾的BigDecimals以及以H或者h结尾的BigIntegers\(为了避免和BigDecimals的后缀搞混，OGNL特意取了Huge这个单词的首字母作为BigIntegers的后缀\)
4.  布尔常量：true和false
5.  null常量

### 3.2 灵活的\[\]操作符

OGNL过\[\]来支持对象应用的高阶用法  

- **区别重载方法**

通过\[\]引用入参，比如某个对象存在以下四个重载方法：

```java
public PropertyType[] getPropertyName();  
public void setPropertyName(PropertyType[] anArray);  
public PropertyType getPropertyName(int index);  
public void setPropertyName(int index, PropertyType value);  
````

则`someProperty[2]`等价于Java代码的`getPropertyName(2)`

 -    **动态计算**

`name.length`

就等价于

`name['length']`

也等价于

`name['len' + 'th']`

`[]`的引入可以让引用对象属性更加灵活

### 3.3 方法调用

举例：

`method( ensureLoaded(), name )`

- 有两个地方需要注意，OGNL是运行时调用，因此没有任何静态类型的信息可以参考，所以如果解析到有多个匹配的方法，则任选其中一个方法调用
- 常量null可以匹配所有的非原始类型的对象

### 3.4 复杂链式表达式

如果在`.`后面链接一个括号表达式，则当前表达式计算的结果会传入括号中，比如：

`headline.parent.(ensureLoaded(), name)`

等价于

`headline.parent.ensureLoaded(), headline.parent.name`

### 3.5 变量引用

临时变量在OGNL是在变量名前加`#`来表示的，虽然是临时变量，但是他们都是全局可见的。此外，表达式计算的每一步结果都存在变量`this`中，比如下面这个例子：

`listeners.size().(#this > 100? 2*#this : 20+#this)`

表达的含义就是如果listeners数量大于100则返回2倍的值，否则返回数量加20

### 3.6 括号表达式

括号的作用Java语法中一致，会始终被当成一个语法单元进行解析，当然也会改变运算符的先后顺序，比如在下面的方法调用中，会首先解析括号`(ensureLoaded(), name)`的值（逗号表达式的含义同Java语法规则），然后才会执行方法调用

`method( (ensureLoaded(), name) )`

## 4 集合操作

### 4.1 新建列表Lists

`name in { null,"Untitled" }`

这条语句判断name是否等于null或者"Untitled"

### 4.2 新建原生数组

 -    **创建数组并初始化**

`new int[] { 1, 2, 3 }`

 -    **创建数组并指定长度**

`new int[5]`

### 4.3 新建Maps

 -    **新建普通Map**

`#{ "foo" : "foo value", "bar" : "bar value" }`

 -    **新建特定类型Map**

`#@java.util.LinkedHashMap@{ "foo" : "foo value", "bar" : "bar value" }`

### 4.4 集合的投影

OGNL把对针对集合上的每个元素调用同一个方法并返回新的集合的行为称之为“投影”，类似于Java Stream中的map比如：

`listeners.{delegate}`

将这个集合每个元素的`delegate`属性组成了新的集合并返回

`objects.{ #this instanceof String ? #this : #this.toString()}`

返回`objects`中元素的String形式

### 4.5 查找集合元素

 -    **查找所有匹配的元素**

`listeners.{? #this instanceof ActionListener}`

 -    **查找第一个匹配的元素**

`objects.{^ #this instanceof String }`

 -    **查找最后一个匹配的元素**

`objects.{$ #this instanceof String }`

### 4.6 集合的虚拟属性

在Java的语法中，想要观察集合的状态必须要调用相应的集合方法，比如说`size()`或者是`length()`等，OGNL定义了一些特定的集合属性，含义与相应的集合方法完全等价

- **Collections**

  - `size`集合的大小
  - `isEmpty`集合为空时返回true

- **List**

  - `iterator`返回List的Iterator

- **Map**

  - `keys`返回Map的所有key值
  - `values`返回Map的所有value值

- **Set**

  - `iterator`返回Set的Iterator

## 5 类的静态方法

### 5.1 构造函数

非java.lang包下的所有类的构造函数都要用类的全限定名称

`new java.util.ArrayList()`

### 5.2 静态方法

`@class@method(args)`

如果省略class则默认引用的类是java.lang.Math除此之外，还可以通过类的实例引用静态方法

### 5.3 静态属性

`@class@field `

## 6 伪lambda表达式

### 6.1 表达式值计算

`#fact(30H)  
`

如果括号运算符前面没有`.`运算符，则OGNL会将括号中的结果传到括号前的表达式中作为起始值，但是需要注意和方法调用的区别：

`fact(30H)  
`

这样写会调用当前上下文的fact方法，并传入参数30H，如果想强制进行表达式计算，可以像下面这样：

`(fact)(30H)  
`

### 6.2 伪lambda表达式

因为OGNL的变量都是全局可见的，所以只能实现一个简化的lambda表达式，例如：

`#fact = :[#this<=1? 1 : #this*#fact(#this-1)], #fact(30H)  
`

## 7 后记

本文对常见的OGNL语法进行了总结梳理，更加详细语法规则请查阅官方文档：http://commons.apache.org/proper/commons-ognl/language-guide.html  