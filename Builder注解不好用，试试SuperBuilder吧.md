# Builder注解不好用，试试SuperBuilder

相信Lombok插件大家一定不会陌生，一个常用的注解是：`@Builer`, 它可以帮我们快速实现一个`builder`模式。以常见的商品模型为例：

```java
@Builder
@AllArgsConstructor
@NoArgsConstructor
@Data
public class ItemDTO {
    /**
     * 商品ID
     */
    private Long itemId;
    /**
     * 商品标题
     */
    private String itemTitle;
    /**
     * 商品原价，单位是分
     */
    private Long price;
    /**
     * 商品优惠价，单位是分
     */
    private Long promotionPrice;
}
```

一行代码就可以构造出一个新的商品：

```java
ItemDTO itemDTO = ItemDTO.builder()
        .itemId(6542744309L)
        .itemTitle("测试请不要拍小番茄500g/盒")
        .price(500L)
        .promotionPrice(325L)
        .build();
System.out.println(itemDTO);
```

这样写不但美观，而且还会省去好多无用的代码。

## `Builder`注解的使用限制

当我们的实体对象有继承的设计的时候，`Builder`注解就没那么好用了，还是以商品实体为例，如果现在商品类都继承自一个`BaseDTO`

```java
@Builder
@NoArgsConstructor
public class BaseDTO {
    /**
     * 业务身份
     */
    private String bizType;
    /**
     * 场景
     */
    private String scene;
}
```

这时候我们再使用`Builder`注解就会发现，在子类中无法通过`builder`方法构造父类中的成员变量

![image-20210103161952259](https://gitee.com/cincat/picgo/raw/master/img/image-20210103161952259.png)

给`BaseDTO`上加上`Builder`注解也不会有任何效果。事实上，`Builder`注解只管承接注解的这个类，而不会管他的父类或者子类。如果真的是这样的话，遇到有继承的类，只好又打回原形，写一堆的`setter`方法了。

## 试试`SuperBuilder`吧

这个问题在`lombok`v1.18.2版本之前其实很难办，但是在这个版本官方引入了一个新的注解`@SuperBuilder`，无法`build`父类的问题迎刃而解

> The `@SuperBuilder` annotation produces complex builder APIs for your classes. In contrast to [`@Builder`](https://projectlombok.org/features/Builder), `@SuperBuilder` also works with fields from superclasses. However, it only works for types. Most importantly, it requires that *all superclasses* also have the `@SuperBuilder` annotation.

按照官方文档的说法，为了能够使用`build`方法，只需要在子类和父类上都加`@SuperBuilder`注解，我们试一下

![image-20210103164125460](https://gitee.com/cincat/picgo/raw/master/img/image-20210103164125460.png)

果然现在就可以在子类的实例中build`父类的成员变量了

## `Lombok`的原理

`Lombok`自动生成代码的实现也是依赖于JVM开放的扩展点，使其可以在编译的时候修改抽象语法树，从而影响最终生成的字节码

![](https://gitee.com/cincat/picgo/raw/master/img/lombok原理.gif)

图片来源地址：http://notatube.blogspot.com/2010/12/project-lombok-creating-custom.html

## 为什么`Builder`不能处理父类的成员变量

我们可以翻一下`Lombok`的源码，`Lombok`对所有的注解都有两套实现，`javac`和`eclipse`，由于我们的运行环境是`Idea`所以我们选择`javac`的实现，`javac`版本的实现在`lombok.javac.handlers.HandleBuilder#handle`这个方法中

```java
JavacNode parent = annotationNode.up();
if (parent.get() instanceof JCClassDecl) {
   job.parentType = parent;
   JCClassDecl td = (JCClassDecl) parent.get();
   
   ListBuffer<JavacNode> allFields = new ListBuffer<JavacNode>();
   boolean valuePresent = (hasAnnotation(lombok.Value.class, parent) || hasAnnotation("lombok.experimental.Value", parent));
  // 取出所有的成员变量
   for (JavacNode fieldNode : HandleConstructor.findAllFields(parent, true)) {
      JCVariableDecl fd = (JCVariableDecl) fieldNode.get();
      JavacNode isDefault = findAnnotation(Builder.Default.class, fieldNode, false);
      boolean isFinal = (fd.mods.flags & Flags.FINAL) != 0 || (valuePresent && !hasAnnotation(NonFinal.class, fieldNode));
      // 巴拉巴拉，省略掉
}
```

这里的`annotationNode`就是`Builder`注解，站在抽象语法树的角度，调用`up`方法得到的就是被注解修饰的类，也就是需要生成`builder`方法的类。

通过查看源代码，`@Builder`注解是可以修饰类，构造函数和方法的，为了简单起见，上面的代码只截取了`@Builder`修饰类这一种情况，这段代码关键的地方就在于调用`HandleConstructor.findAllFields`方法获得类中所有的成员变量：

```java
public static List<JavacNode> findAllFields(JavacNode typeNode, boolean evenFinalInitialized) {
   ListBuffer<JavacNode> fields = new ListBuffer<JavacNode>();
  // 从抽象语法树出发，遍历类的所有的成员变量
   for (JavacNode child : typeNode.down()) {
      if (child.getKind() != Kind.FIELD) continue;
      JCVariableDecl fieldDecl = (JCVariableDecl) child.get();
      //Skip fields that start with $
      if (fieldDecl.name.toString().startsWith("$")) continue;
      long fieldFlags = fieldDecl.mods.flags;
      //Skip static fields.
      if ((fieldFlags & Flags.STATIC) != 0) continue;
      //Skip initialized final fields
      boolean isFinal = (fieldFlags & Flags.FINAL) != 0;
      if (evenFinalInitialized || !isFinal || fieldDecl.init == null) fields.append(child);
   }
   return fields.toList();
}
```

这段代码比较简单，就是对类中的成员变量做了过滤，比如说，静态变量就不能被`@Builder`方法构造。有一个有意思的点，尽管`$`可以合法的出现在`java`的变量命名中，但是`Lombok`对这种变量做了过滤，因此变量名以`$`开始的也不能被`@Builder`构造，经过我们的验证确实是这样的。

如果我们用`JDT AstView`看一下`ItemDTO`的抽象语法树结构，发现`Java`的抽象语法树设计的确是每个类只包含显式声明的变量而不包括父类的成员变量（该插件支持点击语法树节点可以和源文件联动，且数量只有4个和`ItemDTO`声明的成员变量数量一致）

![image-20210104000114524](https://gitee.com/cincat/picgo/raw/master/img/image-20210104000114524.png)

因为`findAllFields`方法是从当前类的抽象语法树出发去找所有的成员变量，所以就只能找到当前类的成员变量，而访问不到父类的成员变量

一个镜像的问题就是，既然`@Builder`注解不能构造父类的成员变量，那`@SuperBuilder`是怎么做到的呢？翻一下`@SuperBuilder`的源码，核心逻辑在`lombok.javac.handlers.HandleSuperBuilder#handle`

```java
// 巴拉巴拉省略
JCClassDecl td = (JCClassDecl) parent.get();
// 获取继承的父类的抽象语法树
JCTree extendsClause = Javac.getExtendsClause(td);
JCExpression superclassBuilderClass = null;
if (extendsClause instanceof JCTypeApply) {
   // Remember the type arguments, because we need them for the extends clause of our abstract builder class.
   superclassTypeParams = ((JCTypeApply) extendsClause).getTypeArguments();
   // A class name with a generics type, e.g., "Superclass<A>".
   extendsClause = ((JCTypeApply) extendsClause).getType();
}
if (extendsClause instanceof JCFieldAccess) {
   Name superclassName = ((JCFieldAccess) extendsClause).getIdentifier();
   String superclassBuilderClassName = superclassName.toString() + "Builder";
   superclassBuilderClass = parent.getTreeMaker().Select((JCFieldAccess) extendsClause, parent.toName(superclassBuilderClassName));
} else if (extendsClause != null) {
   String superclassBuilderClassName = extendsClause.toString() + "Builder";
   superclassBuilderClass = chainDots(parent, extendsClause.toString(), superclassBuilderClassName);
}
// 巴拉巴拉省略
```

可以看到，这里拿到了继承的父类的抽象语法树，并在后面的逻辑中进行了处理，这里不再赘述



