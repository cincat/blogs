# `ON DUPLICATE KEY UPDATE` 我劝你别用

`ON DUPLICATE KEY UPDATE`是MySql对标准sql语法的扩展，其语义是“无则插入，有则更新”，数据是否存在是根据主键或者唯一键(如果指定了唯一索引的话)确定的，它有下面三个最大的缺陷

1. 如果MySql检测到有多条主键冲突，`ON DUPLICATE KEY UPDATE`只会更新其中一条，而不是全部更新
2. 在默认情况下，`ON DUPLICATE KEY UPDATE`会引起主键的非连续自增，如果更新的场景比较多，会导致主键id自增过快，如果主键id到达最大值，后续的插入都会失败
3. 并发调用`ON DUPLICATE KEY UPDATE`会导致死锁

![image-20210110210809891](https://gitee.com/cincat/picgo/raw/master/img/image-20210110210809891.png)

