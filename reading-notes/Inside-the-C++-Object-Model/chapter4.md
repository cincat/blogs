# 第四章

## 虚拟成员函数

需要调整this指针的三种情况

1. 通过一个指向第二个基类的指针， 调用派生类虚函数
2. 通过派生类指针， 调用第二个基类中继承而来的虚函数
3. 单一虚继承与非虚拟的单一继承的内存布局不同， 所以也会引发this指针的调整

---

## 指向成员函数的指针

若成员函数是一个非虚函数， 那么这个指针就是函数在内存中的地址， 而如果该成员函数是一个虚函数， 那么这个指针的值是一个整数， 代表该虚函数在虚函数表中的索引。编译器的实现就需要把这两种完全不同的数值统一起来。

---

## 内联函数

```cpp
inline int min(int i, int j) {
    return i < j ? i : j;
}

inline int bar() {
    int minval;
    int val1 = 1024;
    int val2 = 2048;
    minval = min(val1, val2); // (1)
    minval = min(1024, 2048); // (2)
    minval = min(foo(), bar() + 1); //(3)
    return minval;
}
```

上面的三个调用会依次扩展为：  
`minval = val1 < val2 ? val1 : val2;`  
`minval = 1024;`  
以及

```cpp
int t1;
int t2;

minval = (t1 = foo()), (t2 = bar() + 1),
t1 < t2 ? t1 : t2;
```

而对于有局部变量的内联函数， 扩展后该局部变量会被维持， 所以内联函数中的局部变量加上有副作用的参数， 可能导致大量临时性对象的产生， 特别是当一个单一表达式被扩展多次的时候：  
`minval = min(val1, val2) + min(foo(), foo() + 1)`
可能被拓展成：

```cpp
int __min_lv_minval_00;
int __min_lv_minval_01;

int t1;
int t2;

minval = ((__min_lv_minval__00 =
    val1 < val2 ? val1 : val2),
    __min_lv_minval__00)
     +
     ((__min_lv_minval__01 = (t1 = foo()),
     (t2 = foo() + 1),
     t1 < t2 ? t1 : t2),
     __min_lv_minval__01);
```