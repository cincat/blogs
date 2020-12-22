# 第十六章 模板与泛型编程

## 16.1 定义模板

### 16.1.1 函数模板

#### 非类型模板参数

```cpp
template <unsigned N, unsigned M>
int compare(const char (&p1)[N], const char (&p2)[M]) {
    return strcmp(p1, p2);
}
```

一个非类型参数可以是一个整型，或者是一个指向对象或者函数类型的指针或引用， 绑定到非类型整形参数的实参必须是一个常量表达式。绑定到指针或者引用非类型的实参必须具有静态的生命周期。

#### 模板编译

当编译器遇到一个模板的定义时， 他并不生成代码，当我们使用模板时， 编译器才会生成代码。模板的头文件通常既包含声明也包括定义。

> NOTE:  
这样做的原因是模板的实例化发生在编译期，而非模板函数的定义查找发生在链接期，晚于编译期。也有方法让模板的声明和定义分别在不同的文件中， 不过这要牺牲模板的灵活性，如下面所示的例子：

```cpp
// Foo.h

// no implementation
template <typename T> struct Foo { ... };

//----------------------------------------
// Foo.cpp

// implementation of Foo's methods

// explicit instantiations
template class Foo<int>;
template class Foo<float>;
// You will only be able to use Foo with int or float
```

### 16.1.2 类模板

与函数模板不同，编译器不能为类模板推断出模板类型参数。  

#### 在类代码内简化模板类名的使用

```cpp
template <typename T>
BlobPtr<T> BlobPtr<T>::operator++(int) {
    BlobPtr ret = *this;//此处BlobPtr等同于BlobPtr<T>
    ++*this;
    return ret;
}
```

#### 通用和特定的模板友好关系

```cpp
//前置声明， 将此模板的一个特定实例声明为友元时要用到
template<typename T> class Pal;
class C {
    friend class Pal<C>;
    //Pal2的所有实例都是C的友元；这种情况下无须前置声明
    template<typename T> friend class Pal2;
};
template <typename T> class C2 {
    frind class Pal<T>;//Pal 的模板声明必须在作用域内
    //Pal2的所有实例都是C2每一个实例的友元，不需要前置声明
    template<typename T> friend class Pal2;
    friend class Pal3;//不需要Pal3的前置声明
}
```

#### 模板类型别名

```cpp
typedef Blob<string> StrBlob;
template<typename T> using twin = std::pair<T, T>;
twin<string> authors;
template<typename T> using partNo = std::pair<T, unsigned>;
```

### 16.1.3 模板参数

#### 模板参数与作用域

```cpp
typedef double A;
template <typename A, typename B> void f(A a, B, b) {
    A tmp = a; // tmp的类型位模板参数A的类型， 而不是double
    double B; // 错误：重声明模板参数B
}

template <typename V, typename V> //错误：非法重用模板参数名V
```

一个模板参数名的可用范围是在其声明之后，至模板声明或定义结束之前。与任何其他名字一样， 模板参数会隐藏外层作用域中声明的相同名字。但是，与大多数其他上下文不同，在模板内部不能重用模板参数名，所以模板参数名在一个特定的模板参数列表中只能出现一次。

#### 使用类的类型成员

假定T是一个模板类型， 当编译器遇到类似`T::mem`这样的代码时，他不会知道`mem`是一个类型成员还是一个`static`数据成员。直到实例化的时候才知道。默认情况下，C++语言假定通过作用域运算符访问的名字不是类型。因此，如果我们希望使用一个模板类型的类型成员，就必须显式的告诉编译器改名字是一个类型。

```cpp
template <typename T>
typename T::value_type top(const T& c) {
    if (!c.empty()) return c.back();
    else return typename T::value_type();
}
```

#### 模板默认实参与类模板

如果一个类模板为其所有的模板参数都提供了默认实参，且我们希望使用这些默认实参，就必须在模板名之后跟一个空尖括号。

### 16.1.4 成员模板

成员模板不能使虚函数，原因如下：

>Member template functions cannot be declared virtual.Current compiler technology experts to be able to determine the size of a class’s virtual function table when the class is parsed.Allowing virtual member template functions woule require knowing all calls to such member functions everywhere in the program ahead of time.This is not feasible,especially for multi-file projects.

#### 类模板的成员模板

当我们在一个类模板外定义一个成员模板时，必须为类模板和成员模板提供模板参数列表。类模板的参数列表在前，后跟成员自己的模板参数列表

### 16.1.5 控制实例化

当两个或者多个独立编译的源文件使用了相同的模板，并且提供了相同的模板参数时，每个文件就会有该模板的一个实例。可以通过显式实例化避免这种开销。

```cpp
extern template declaration;
```

#### 实例化定义会实例化所有成员

一个类模板的实例化定义会实例化该模板的所有成员，包括内联的成员函数，当编译器遇到一个实例化定义时，它不了解程序使用了那些成员函数。因此，与处理类模板的普通实例化不同，编译器会实例化该类的所有成员。即使我们不使用某个成员，它也会被初始化。

## 16.2 模板实参推断

### 16.2.1 类型转换与模板类型参数

与往常一样，顶层`const`无论在形参中还是实参中， 都会被忽略。在其他类型转换中，能在调用中应用与函数模板的包括如下两项。

+ `const`转换：可以将一个非`const`对象的引用或指针传递给一个`const`的引用或指针形参。
+ 数组或函数指针转换：如果函数形参不是引用类型，则可以对数组或函数类型的实参应用正常的指针转换。

其他类型转换，如算数转换，派生类向基类的转换以及用户定义的转换都不能应用于函数模板。

### 16.2.2 函数模板的显式实参

#### 指定显示模板实参

```cpp
template<typename T1, typename T2, typename T3>
T1 sum(T2, T3);
auto val = sum<long long>(i, lng);
```

第一个模板实参与第一个模板参数匹配，第二个实参与第二个参数匹配，依次类推，只有最右短的参数的显式模板参数才可以忽略。

```cpp
template<typename T1, typename T2, typename T3>
T3 alternative_sum(T2, T1);
auto val = sum<long, long, int, long>(i, lng);
```

#### 正常的类型转换应用于显式指定的实参

```cpp
long lng;
commpare(lng, 1024);//错误， 无法实例化
compare<long>(lng, 1024);//实例化为compare(long, long)
compare<int>(lng, 1024);//实例化为compare(int, int)
```

#### 尾置返回类型与类型转换（traits）

由于尾置返回出现在参数列表之后，它可以使用函数的参数！

### 16.2.4 函数指针和实参推断

### 16.2.5 模板实参推断和引用

#### 引用折叠和右值引用参数

对于一个给定的类`X`:

+ X& &、X& &&和X&& &都折叠成X&；
+ 类型X&& &&折叠成X&&

#### 编写接受右值引用参数的模板函数

```cpp
template <typename T> void f3(T&& val) {
    T t = val;
    t = fcn(t);
    if (val = t);
}
```

如果我们对一个右值调用f3，例如字面值常量42，T推断出来为int，如果我们以一个左值i调用f3，则T为int&，此时t被绑定到val变量上，if语句始终是判断为真。

如果将一个左值传递给右值引用模板类型参数，推断出来T的类型为左值引用

### 16.2.6 理解`std::move`

move操作会转换对象的控制权，导致原有对象状态合法但确没有意义，因此调用这样的对象是不安全的，尤其当move的操作对象是一个左值的时候，当move的操作对象是一个右值的时候，前面的担心就是多余的。因此我们需要显式的告诉编译器：可以‘窃取’该对象的内容，发生了什么后果我自己承担，这就是std::move的作用。

#### 从一个左值static_cast到一个右值是允许的

### 16.2.7 转发

## 16.3 重载与模板

+ 对于一个调用，其候选函数包括了所有模板实参推断成功的函数模板实例
+ 可行函数（模板与非模板）按类型转换来排序
+ 如果恰有一个函数比其他函数有更好的匹配，选择此函数。但是，如果多个函数提供了同样好的匹配，则：
  + 如果同样好的函数中只有一个是非模板函数，则选择此函数
  + 如果同样好的函数中只有多个模板函数，且其中一个比其他更特例化，则选择此函数
  + 否则， 调用有歧义

例子见书p.615页

## 16.4 可变参数模板

## 16.5 模板特例化

### 定义函数模板特例化

当我们定义一个特例化版本时，函数参数类型必须与一个先前声明的模板中对应的类型匹配

```cpp
template <typename T> int compare(const T&, const T&);
template <size_t N, size_t M>
int compare(const char (&)[N], const char (&)[M]);

template<>
//注意此处p1和p2前的const修饰
int compare(const char * const &p1, const char * const &p2) {
    return strcmp(p1, p2);
}
```

注意特例化本质上你是模板的一个实例，而不是模板的重载，如果我们对字符串常量调用`compare`函数的时候`compare("hi", "mom")`两个模板函数提供同样好的匹配，但是接受字符数组参数的模板更特例化，因此编译器会选择它，如果我们将接受字符指针的版本定义为一个普通函数，那么将有三个可行的函数，而且编译器会选择非模板函数。

### 类模板特例化

### 类模板部分特例化

### 特例化成员