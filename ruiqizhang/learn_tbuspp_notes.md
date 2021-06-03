# tbuspp学习笔记
## 名词学习
+ RPC (remote procesure call)：远程过程调用，就是本地代码调用在另一台服务器上提供的服务（例如说一个接口）
    的一个过程。该过程涉及到函数名和入参的序列化和传输，以及服务提供方对其进行反序列化和根据函数名寻找
    服务进程id的步骤。
+ 服务：就是一组接口。
+ 服务实例：提供这组接口的一个进程，用(ip:port)标识。
+ 服务名字：对服务的描述，就是gameid.服务名.区服.扩展字段
+ 服务注册：一个服务实例在向外界提供服务之前，需要向服务中心注册以便调用方能发现它。
+ 服务发现：就是调用方找到被调用方的过程。
+ 名字服务：分布式系统中的核心组件之一，它负责为调用者寻找和匹配到合适的被调用者。

## tbuspp是啥
tbuspp就是分布式架构中的提供服务间通信的一个组件，它属于一个系统的基础设施组件层。一个分布式系统每个服务
都有可能依赖于其他服务才能满足用户需求：例如说一个网上购物软件，用户想将某件商品加入到购物车，这个软件必须
询问零售商数据库是否拥有足够货存。
一个service mesh可以对某个服务发起的request进行路由，以找到最佳的服务提供节点。这样的话可以实现某种负载均
衡。这种调用方不关心哪个服务实例提供服务的情况称为无状态路由，如果指定发送到某个服务实例的话称为有状态路由。
当然一个服务开发者也可以自己指定通信规则，但随着系统规模变大，通信方式变得负责，这将变得不可能。
一个service mesh可以看作是一个代理阵列，每个代理伴随着一个服务，request在这个代理网络中进行路由。

## tbuspp运行机制
两个相互之间通信的服务实例会建立两条tcp连接，分别用于两个方向上的传输。tbuspp内部处理事件时使用到了生产者-消费者模式。
tbuspp中的service mesh中的节点为一个tbuspp_agent（也称为sideCar），服务里面的TbusppAPI和Tbuspp_agent通过共
享内存通信。tbuspp_agent会将从网络中收到的消息放入到共享内存里面，服务实例通过TbusppAPI从共享内存中读走消息。tbuspp_agent还会和NS server进行通信来完成注册和上线
其实tbuspp又分为客户端和服务端，每个节点都要运行一个服务端，然后再在上面写客户端。
客户端和服务端之间的通信是通过共享内存的方式：也就是`/dev/shm/`对应的tmpfs

## tbuspp配置
需要选名字服务环境，我就选DEV正式环境吧，对应的TbusppNS域名为devnet.tbusppns.oa.com，http端口为8082，tcp端
口为9092。创建了个游戏：ruiqi_test, gameid为379440991，游戏key（密码）为428a2a1e6bab7b6aa5e1e755dc16a627

## 一个简单的发送服务例子
在写客户端时，需要先定义一些重要的常量：
```
DEFINE_string(conf, "/dev/shm/baseagent/conf/base_agent.conf", "base_agent.conf");
DEFINE_string(game_id, "test_gameid", "game id");
DEFINE_string(game_password, "test_password", "game password");
DEFINE_string(send_server, "example.send_server", "send server name");
DEFINE_string(recv_server, "example.recv_server", "recv server name");
DEFINE_int64(wait_timeout, 1, "wati event timeout, unit is ms");
```
另外得自己定义一个接收系统通知消息的一个类型`RecvSysNotifyMsg`，它继承自`baseagent::NotifyEvtToApp`。里面
必须定义`Notify(cmd, dst, data, code, pext)`，这个接口的参数我的理解是：
cmd: 表示向app通知的消息的类型
dst：表示远端初始化的服务实例
data：表示远端初始化的服务实例对应的用户数据
code：感觉是表示初始化dst这个实例时返回的结果，如果非零的话代表这个实例初始化失败。
pext：这个我就不知道干什么用的了，反正这个例子程序没有用到。
这个`RecvSysNotifyMsg`类型的Notify方法具体看你要处理什么通知了，例如说这个例子
只是处理服务实例上线、下线、startup完成通知：
```
int Notify(
        int cmd,
        const troute::ServerNameAndAddress &dst,
        const troute::ServerNameAndAddress &data,
        int code,
        const std::string *pext)
    {
        if (cmd == tbuspp::Startup_Rsp)
        {
            if (code)   // start instance failed
            {
                exit(-1);
            }
            // start instance succ
            m_is_startup = true;
        }

        if (cmd == tbuspp::Instance_Online)
        {
            InstanceObj* pdata = baseagent::CloneInstance(data);
            std::string uid = pdata->GetServiceNameString() + std::string(":") + pdata->GetUserData();
            if (insts_by_other_process.count(uid) == 0)
            {
                insts_by_other_process[uid] = pdata;
            }
        }

        if (cmd == tbuspp::Instance_Offline)
        {
            InstanceObj* pdata = baseagent::CloneInstance(data);
            std::string uid = pdata->GetServiceNameString() + std::string(":") + pdata->GetUserData();
            std::map<std::string, InstanceObj*>::iterator it = insts_by_other_process.find(uid);
            if (it != insts_by_other_process.end())
            {
                insts_by_other_process.erase(it);
                baseagent::DeleteInstanceObj(it->second);
            }
        }

        return 0;
    }
```
定义了这个类型之后，我们就要创建一个处理系统通知的全局变量`g_notify_handle`

我大概理解了一个服务实例内首发数据的逻辑了：
首先我们要对初始化客户端时对应的`event_handle`进行轮询，也就是:
`int ret = baseagent::PollEvent(event_handle, FLAGS_wait_timeout, &local_instance, &mask, &debugstr);`
一般这个超时`FLAGS_wait_timeout`设为几毫秒，`local_instance`是一个指向
`InstanceObj`的指针，用来存储有事件的服务实例，`mask`用来记录事件掩码，
`debugstr`用来存储发生错误时的信息。
如果`ret == 0`，那么说明成功获得了一个有事件的实例。
随后我们就要用一个`baseagent::IPeekMsg`对象来接受这个有事件发生的实例中的消息了：
首先attach这个有事件发生的实例：`peek_obj->Attach(local_instance);`
然后用`Peek`方法来获取消息包：
`ret = peek_obj->Peek(&index, &msg, &msg_len, &state, &remote_instance);`
其中`index`用来记录这个包是整个消息的第几段，`msg`是一个用来记录消息包的指针，
`msg_len`记录这个消息包的长度，`state`我的理解是记录当前这个包是处于整个消息
的`begin`，`middle`，`end`，抑或是一个完整的消息包`complete`。
这个`remote_instance`是`InstanceObj`指针，用来记录发送消息的服务实例。
接收完了之后我们就可以干业务上的事情了。
当然如果这个消息包并不完全的话，我们还需要调用个`peek_obj->Commit(index);`


先进行客户端初始化：
`TBaseAgentClientInit(event_hangle, gameid, passwd, conf_path)`
这个`event_handle`代表这个客户端的句柄
随后需要创建一个实例：
`CreateInstance(instance_name, user_data)`
然后再注册这个实例：
`RegisterInstance(instance, conflict_strategy)`
然后注册各种notify函数
```
StartupCompleteNotifyFunc(&g_notify_handle);    // 应该是注册完成事件通知函数
InstanceOnlineNotifyFunc(&g_notify_handle);     // 应该是注册服务实例上线事件通知函数
InstanceOfflineNotifyFunc(&g_notify_handle);    // 应该是注册服务实例下线通知函数
```
然后启动这个实例：
`StartupInstance(instance)`
创建一个信息窥视对象：
`static baseagent::IPeekMsg *peek_obj = baseagent::CreatePeekMsgObj();`
随后自旋等待startup完成：
```
while (!g_notify_handle.m_is_startup)
{
    ret = UpdateByPeek(peek_obj,event_handle);
    if (ret)
        usleep(10 * 1000);
}
std::cout << "StartupInstance() success" << std::endl;
```
由于我们的服务需要和另外一个服务进行通信，所以要用名字创建一个接受消息的服务实例：
`ServiceObj* remote_svc = baseagent::CreateService(FLAGS_game_id + "." + FLAGS_recv_server);`
其实`ServiceObj`这个类型是一个`typedef`，对应`troute::ServerNameAndAddress`这个类型，说明
`ServiceObj`是一个服务名字对象。
假如我们只是进行stateless通信的话，那么随后调用`SendMsgToStatelessServer`就可以将消息发送到
该服务的任意一个实例了：
`baseagent::SendMsgToStatelessServer(*send_instance, *remote_svc, msg.c_str(), msg.length(), &remote_inst);`
最后一个`remote_inst`参数可以是个`NULL`。
最后服务完成时需要调用`DeleteInstanceObj`来释放资源

## 另一个例子：一个简单的接受服务

## 写一个ping-pong server？
一个ping-pong server应该有一个`--is_ping`的命令行参数，这个参数表明该进程主动先向另一个
ping-pong server发送消息。在第一个消息被发出之后，两个ping-pong servers将接收到的消息序列号
加一后回传给发送方，知道序列号超过某个阈值。

我注意到tbuspp中有一个轮询函数`baseagent::PollEvent`，这个方法可以用来监听所有本地服务实例
上的事件。这个方法有两个重载，一个是监听的服务数目只有一个，另外一个监听的服务数目有多个。由于本地只有
一个服务实例，因此只用第一个版本：
```
int PollEvent(unsigned long long notify_handle, int msec_timeout,
    OUT const InstanceObj** ppdst_instance,
    OUT int* pevent_mask, OUT std::string* perr_info)
```
其中第一个参数就是`TBaseAgentClientInit`中初始化的句柄，第二个是轮询超时时间，我这里设一个很长的时
间，例如说60s, `ppdst_instance`是返回参数，当`ret == 0`时它指向有事件发生的服务实例名字对象。
`pevent_mask`用来记录事件的掩码， `perr_info`当`ret`非零时可以查看错误信息。

因此我程序中的main loop应该长这样：
```
while (true)
{
    int mask;
    const InstanceObj *local_instance = NULL;
    std::string debugstr;
    int ret = baseagent::PollEvent(event_handle, timeout_ms, &local_instance, &mask, &debugstr);
    // no message recv
    if (ret)
    {
        std::cout << "PollEvent() failed, ret = " << ret << "," << baseagent::GetErrMsg(ret) 
                    << ", debugstr=" << debugstr << std::endl;
        break;
    }
    // 将baseagent::IPeekMsg对象绑定到local_instance上
    peek_obj->Attach(local_instance);

    const InstanceObj *remote_instance = NULL;
    char* msg = g_tmp_buffer;
    unsigned int msg_len;

    // 我这里先假设收到的消息都是很短的，不需要在一个循环中多次读取
    ret = peek_obj->ReadMsg(msg,&msg_len,&remote_instance);
    // 要么没有数据，要么读取失败
    if (ret)
    {
        std::cout << "ReadMsg() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        break;
    }
    // 将收到的数据打回到发送方
    PongBack(msg,msg_len,local_instance,remote_instance);
}
```

这样的话发消息的逻辑因该是在`PongBack`这个方法上的：
```
int PongBack(const char* msg,unsigned int msg_len,const InstanceObj* local,const InstanceObj* remote)
{
    if (msg_len == 0 || !remote || !local)
        return -1;
    // show ping message
    std::cout << "Get ping from:" << baseagent::GetInstanceStr(*remote) << "'s msg:" << std::string(msg,msg_len) << std::endl;
    // pong back 
    // 1. get the sequence number
    // 2. rewrite the msg
    // 这里最后一个参数其实可以用来保存实际路由道到的服务实例，下次发消息的时候就不用路由了。但这里
    // 为了简化程序我就不指定了。
    ret = baseagent::SendMsgToStatelessServer(*send_instance, *remote_svc, msg.c_str(), msg.length(), NULL);
    if (ret)
    {
        std::cout << "Send msg failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        return -1;
    }
    return 0;
}

```
