#  InnoDB自增原理都搞不清楚，还怎么CRUD？

虽然我们习惯于给主键ID指定`AUTO_INCREMENT`属性，但是`AUTO_INCREMENT`也是可以指定到非主键字段的，唯一的约束就是这个字段上面得加索引，有了索引，就可以通过类似`SELECT MAX(*`ai_col`*)`的语句快速读到这列数据的最大值。

本文要探讨的话题是`MySql`的`InnoDB`引擎处理自增数据列的原理

## MySql 5.1之前的实现

在这个版本之前，用`AUTO_INCREMENT`修饰的数据列确实是严格连续自增的。`MySql`的实现是会针对每个插入语句加一个全表维度的锁，这个锁可以保证每次只有一条插入语句在执行，每插入一行数据，就会生成一个自增数据。

```sql
mysql> CREATE TABLE t1 (
    -> c1 INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY, 
    -> c2 CHAR(1)
    -> ) ENGINE=InnoDB AUTO_INCREMENT=100;
```

假如我们在数据库中新建上面的这张表，接着我们执行插入语句。

```sql
mysql> INSERT INTO t1 (c1,c2) VALUES (NULL,'a'), (NULL,'b'), (NULL,'c'), (NULL,'d');
```

针对这条`MySql`执行的流程为：

> 1. 全表加 `AUTO-INC`锁
>
>    1.1 生成主键ID：101
>
>    1.2 将行(101, 'a')插入表中
>
>    1.3 生成主键ID: 102
>
>    1.4 将行(102, 'b')插入表中
>
>    ...
>
> 2. 释放 `AUTO-INC`锁

`MySql`5.1之前的这种实现方式可以保证`AUTO_INCREMENT`严格自增，但是并发程度也最差，因为`AUTO_INCREMENT`锁是全表加锁直到这条语句结束

## MySql 5.1版本带来的优化

前文中的`insert`语句是比较简单的，所谓简单的`insert`语句指的是插入的的数据行数是可以提前确定的，与之相对的是`Bulk insert`比如`INSERT ... SELECT`这类语句，这类插入语句的插入行数不能提前确定。

在这个版本以及之后，对于简单语句的插入，不再加全表的`AUTO-INC`锁，只会在产生自增列数据的时候加一个轻量级的互斥锁，等自增数据分配好，锁就释放了，因此像上面的例子，在`MySql`5.1之后的执行流程如下

> 1. 加轻量级互斥锁
>
>    1.1 分配自增数据
>
> 2. 释放锁
>
> 3. 将行(101, 'a')插入表中
>
> 4. 将行(102, 'b')插入表中
>
>    ...

可以看到，对于简单的插入语句，并发情况下的临界区变小了，且不再持有全表的锁，提升了并发性能。当然，如果在尝试加锁的过程中遇到有其他事务持有全表的`AUTO-INC`锁，还是要等待全表的`AUTO-INC`锁释放再执行本次插入操作

对于`Bulk insert`的插入语句，仍然避免不了全局的`AUTO-INC`锁，这类语句，他们的执行流程仍然保持和5.1之前版本一致，比如以下表为例

```sql
CREATE TABLE t1 (
  c1 INT(11) NOT NULL AUTO_INCREMENT,
  c2 VARCHAR(10) DEFAULT NULL,
  PRIMARY KEY (c1)
) ENGINE=InnoDB;
```

执行下面两条语句

```sql
Tx1: INSERT INTO t1 (c2) SELECT 1000 rows from another table ...
Tx2: INSERT INTO t1 (c2) VALUES ('xxx');
```

由于在执行Tx1时，`InnoDB`无法知道要插入的具体行数，因此会获取一个全表的锁，每执行一条插入语句就会给自增列赋新的值。因为有全表的锁，所以Tx1这条语句插入的所有行数都是连续自增的，Tx2自增列的值要么小于Tx1自增列的最小值，要么大于Tx1自增列中的最大值，这取决于这两条语句的执行顺序

`InnoDB`采取这样的决策一个重要的原因是主从复制，在`MySql`8.0之前，`MySql`的主从是基于语句复制的。在刚才的例子中，如果Tx1执行的时候没有全表的锁，那有可能在Tx1执行的过程中Tx2也在执行，这就会导致Tx1和Tx2自增列的数据每次执行结果都不相同，也就无法在从库中通过语句回放复制。

## MySql 8.0版本之后的优化

虽然`MySql`5.1版本对简单的插入语句做了优化，避免了全表加锁，但对于`INSERT ... SELECT`这样的复杂插入语句，仍然避免不了全表的`AUTO-INC`锁，主要是基于执行语句的主从复制要能在从库完全回放复制主库，所有的语句执行结果就不能和执行顺序有关。

在`MySql` 8.0以及之后默认的主从复制策略变成了基于数据行实现，在这样的背景下`INSERT ... SELECT`这样的复杂插入语句也不需要全表加锁来生成自增列数据了，所有的插入语句只有在生成自增列数据的时候要求持有一个轻量级的互斥锁，等到自增数据生成好之后释放锁。在这种实现下，所有插入语句的自增列都不能保证连续自增，但是并发性能确实最好的。

## 总结

需要说明的是，如果插入语句所处的事务回滚了，生成的自增列数据是不会回滚的，这种情况下会造成自增列数据非连续增长。

以上所述都是各个`MySql`版本的默认实现，`MySql` 5.1引入了一个新的参数 [`innodb_autoinc_lock_mode`](https://dev.mysql.com/doc/refman/8.0/en/innodb-parameters.html#sysvar_innodb_autoinc_lock_mode) 通过修改这个字段的值，可以改变`InnoDB`生成自增列的策略，其值总结如下：

| 值   | 名称                  | 含义                                                         |
| ---- | --------------------- | ------------------------------------------------------------ |
| 0    | traditional lock mode | 每次插入语句执行都会全表加锁至语句结束，5.1版本之前默认实现  |
| 1    | consecutive lock mode | 简单插入不再全表加锁，`INSERT ... SELECT`类的语句才持有全表锁，5.1至8.0默认实现 |
| 2    | interleaved lock mode | `INSERT ... SELECT`类的语句也不会全表加锁，只有生成自增列数据时才加锁，8.0之后默认实现 |

不推荐显式指定自增列数据，因为在5.7以及之前的版本，如果通过`update`语句显式指定一个比`SELECT MAX(*`ai_col`*)`还大的自增列值，后续`insert`语句可能会抛"Duplicate entry"错误，这一点在8.0版本之后也有了改变，如果通过显式的`update`语句显式指定一个比`SELECT MAX(*`ai_col`*)`还大的自增列值，那该值就会被持久化，后续的自增列值都从该值开始生成。

假如有下面这张表

```sql
mysql> CREATE TABLE t1 (
    -> c1 INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY, 
    -> c2 CHAR(1)
    -> ) ENGINE = INNODB AUTO_INCREMENT=100;
```

试想，在我们执行完下面这条语句之后表的内容变成了什么？

```sql
mysql> INSERT INTO t1 (c1,c2) VALUES (1,'a'), (NULL,'b'), (5,'c'), (NULL,'d');
```

**MySql 5.1之前，或者`innodb_autoinc_lock_mode`设置为0**

```sql
mysql> SELECT c1, c2 FROM t1 ORDER BY c2;
+-----+------+
| c1  | c2   |
+-----+------+
|   1 | a    |
| 101 | b    |
|   5 | c    |
| 102 | d    |
+-----+------+
```

在这种模式下，每插入一行数据就会生成一个自增值赋到`c1`这一行，因此`c1`的下一个自增值是103

**MySql 8.0之前，或者`innodb_autoinc_lock_mode`设置为1**

```sql
mysql> SELECT c1, c2 FROM t1 ORDER BY c2;
+-----+------+
| c1  | c2   |
+-----+------+
|   1 | a    |
| 101 | b    |
|   5 | c    |
| 102 | d    |
+-----+------+
```

当前表的数据与前一个场景一致，但是下一个自增值却是105，因为在这个场景下，自增数据是在插入语句执行的最开始一次性生成的

**MySql 8.0之后，或者`innodb_autoinc_lock_mode`设置为2**

```sql
mysql> SELECT c1, c2 FROM t1 ORDER BY c2;
+-----+------+
| c1  | c2   |
+-----+------+
|   1 | a    |
|   x | b    |
|   5 | c    |
|   y | d    |
+-----+------+
```

在这种场景下，因为同时可能有其他的插入语句执行，因此`x`和`y`的值是不确定的，下一个自增值也是未知的。



[Lombok的Builder注解不好用，试试SuperBuilder吧](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247484029&idx=1&sn=776fbe3b251636bc170133680d2a40ca&chksm=c30a532ef47dda38984bbf44bca146e04f01ec9af26d23ac38709d3c0ced1543af17cd478894&scene=21#wechat_redirect)

[Arthas原理系列(五)：watch命令的实现原理](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483998&idx=1&sn=0b6ec3aa6d35d070a575e0e3b6200420&chksm=c30a530df47dda1b7deacf83f3068cff9c5f13d881f667d510d14a14a7e1fbccfee70995ec3a&scene=21#wechat_redirect)

[Arthas原理系列(四)：字节码插装让一切变得有可能](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483941&idx=1&sn=6dc8cff4077a9c1243af4d2807c93b5b&chksm=c30a5376f47dda60a84f187f1e541915a13bf75c838c65af2fecc51f392180f276a736c59d75&scene=21#wechat_redirect)

[Arthas原理系列(三)：服务端启动流程](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483910&idx=1&sn=de2639043291c46b5cd3feefcf9952fd&chksm=c30a5355f47dda43afbe9ca38a41ec2d36f2080a4f0ba3c8bb0e53870f9d605f2cfad0b8e147&scene=21#wechat_redirect)

[Arthas原理系列(二)：总体架构和项目入口](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483839&idx=1&sn=31dc63f9fc05cf51379a4570a17044c8&chksm=c30a50ecf47dd9fa532e6e26c192a8f0e1306e0374dc3eebc1efb7aa23de73da98bf60e1ccbc&scene=21#wechat_redirect)

[Arthas原理系列(一)：实现一个极简的Arthas watch命令](http://mp.weixin.qq.com/s?__biz=Mzk0NjExMjU3Mg==&mid=2247483820&idx=1&sn=e75218b63d950ce061cd8ab57fa56dd9&chksm=c30a50fff47dd9e90fa49c6f00c514d19d57ef09e7576589fa9c9f96d0bbfb55b0561f628ea1&scene=21#wechat_redirect)

