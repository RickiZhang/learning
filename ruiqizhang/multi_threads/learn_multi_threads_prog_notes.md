# Linux多线程服务器编程 （阅读笔记）

## shared_ptr和weak_ptr的再学习

在我最初接触`shared_ptr`和`weak_ptr`两种智能指针的时候，我被告知它们两者最重要的用途是自动地`delete`掉动态分配的资源，以及避免由于`shared_ptr`循环引用导致的资源泄漏的问题。从如此朴素的角度来看待这两个标准库神器，导致我一度认为`shared_ptr`就是为了避免程序员因为记性不好导致没有调用`delete`释放资源而创造出来的一样东西，为了解决一些附带而来地小毛病而配套地创造出`weak_ptr`。毕竟，最新版的C++ Primer也只是说明了这些用途和例子而已。

这种错误的观点持续到我今天读这本书的第一章的时候，陈硕从多线程场景下对象析构所面临的诸多问题导出了`shared_ptr`和`weak_ptr`一种非常关键的用途。考虑observer设计模式下的场景：

```c++
class Observer {
public:
   	virtual ~Observer();
    virtual update() = 0;
};

class Observable {
public:
    void register_(Observer *obsr);
    void unregister(Observer *obsr);
    
    void notifyObservers()
    {
        MutexLockGuard lock(&mutex); 
        for (Observer *obsr : observers_)
        {
            obsr->update();
        }
    }
private:
    MutexLock mutex;
    std::vector<Observer *> observers_;
};

class Foo : public Observer {
public:
    void observe(Observable *obs)
    {
        obs->register_(this);
        observable = obs;
    }
    ~Foo()
    {
       	// do stuffs...
        observable->unregister(this);
    }
private:
	Observable *observable; 
};


```

上面的代码在多线程情境下面临着三个主要问题：

+   当Observable对象调用某个Observer对象的update方法时，该Observer对象正处于析构怎么办？调用一个析构到一半的对象的非静态方法是很危险的一件事情。
+   同样的，当一个Observer对象在析构函数中执行到`unregister`那一句时，怎么判断`observable`指向一个仍然存活的Observable对象？
+   在析构一个Observable对象的之前，怎么保证没有任何线程正在或试图执行该对象的成员函数？

很遗憾，上面代码不能解决这里面的任何问题。其实第一和最后一个问题在一般的多线程编程场景下就是如何正确地把控对象的析构时间。而`shared_ptr`生来就是为了解决这类问题，它的引用计数机制保证了只有在对象没有被任何线程引用时，才会去析构该对象。其实`shared_ptr`相当于引入了一层proxy机制：让资源生命周期管理这种脏活交给代理（标准库）。

有了上面的想法，我们可能会将Observable和Observer中保存的指针成员全改成`shared_ptr`。然而这样是不行的，会导致shared_ptr循环引用的问题。因此在Observable中我们将`observers`的类型重新声明为`std::vector<weak_ptr<Observer>>`，在调用任何Observer对象的成员函数之前，首先尝试将对应的`weak_ptr`转换为一个`shared_ptr`，如果`observer`没有过期，那么这个`shared_ptr`必然是有效的，而且在后续的操作也无需担心对象可能会被其它线程析构的问题。如果所指向的对象已经过期了，那么转换的结果必然是一个空的`shared_ptr`。通过判断两种情况我们就能解决第二个问题。

```c++
class Observable {
public:
    void register_(std::weak_ptr<Observer> obsr);
    void unregister(std::weak_ptr<Observer> obsr);
    
    void notifyObservers()
    {
        MutexLockGuard lock(&mutex); 
        auto it = observers_.begin();
        while (it != observers_.end())
        {
            std::shared_ptr<Observer> obj(obsr.lock());
            if (obj)
            {
            	obsr->update();
            	++it;
            }
            else
                it = observers_.erase(it);
        }
    }
private:
    MutexLock mutex;
    std::vector<std::weak_ptr<Observer>> observers_;
};

class Foo : public Observer {
public:
    void observe(std::weak_ptr<Observable> obs)
    {
        std::shared_ptr<Observable> spObs(obs.lock());
        if (spObs)
        {
        	obs->register_(this);
        	observable = obs;
        }
    }
    ~Foo()
    {
       	// do stuffs...
        {
        std::shared_ptr<Observable> spObs(observable.lock());
        if (spObs)
        {
        	spObs->unregister(this);
        }
        }
    }
private:
	std::weak_ptr<Observable> observable; 
};
```

Bravo！问题1、2、3都不会再发生了。不过这样的设计事实上还是会导致其它问题。

实际上，`shared_ptr`和`weak_ptr`不仅能解决共享对象在多线程下析构时的竞争问题，还能提供一种高效的访问同步机制，在频繁读偶尔写的场景下性能尤为出色。有人说read-write lock不就是为这个场景而生的同步机制吗，但是read-write lock的性能往往不高，这个我后面补上。（**锁并不是多线程性能降低的原因，锁争用才是**）

```c++
```



