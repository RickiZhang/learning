# tbuspp学习笔记

## tbuspp是啥

tbuspp就是微服务架构中的提供服务间通信功能的一个组件，它的好处之一是我们在写服务的时候可以专注于业务逻辑，而不用关注消息如何路由到目标服务实例。例如说我当前的服务`serv1`需要使用另一个服务`serv2`才能完成功能，我们只需要`serv2`的名字`dst_name`，通过在`serv1`中调用`SendMsgToStatelessServer(scr_name, dst_name, msg)`来完成服务间通信。tbuspp包含服务端和客户端，服务端（也是tbuspp名字通信网格中的SideCar）提供消息的底层路由功能，客户端（也是一个服务实例）通过调用tbuspp API来实现服务间通信。一个服务器只有一个服务端进程，但是可以有多个客户端实例，每个实例可以提供相同的服务，也可以提供不同的服务。

<img src="C:\Users\ruiqizhang\Documents\Typora\tbuspp_framework1.png" alt="tbuspp_framework1" style="zoom:67%;" />

<center>Fig. 1 Tbuspp整体架构图</center>

## tbuspp运行机制

一个微服务系统中各个服务器节点都运行着一个tbuspp服务端进程，这个进程称为一个tbuspp_agent，系统中所有这样的节点形成一个网格结构，服务间消息就在这个网格中进行路由。本地的服务实例（也就是tbuspp客户端）调用tbuspp API时，会通过共享内存的机制（在也就是挂载在`/dev/shm/`的`tmpfs`）和tbuspp_agent进行通信。tbuspp_agent会将从网络中收到的消息放入到共享内存里面，服务实例通过tbuspp API从共享内存中读走消息。在一个服务实例上线之前，tbuspp_agent还会向名字注册该服务（的名字），以便其它服务实例能找到它。

从名字服务的架构图我们可以看到，每个tbuspp_agent通过tcp请求向名字服务器查询名字、注册名字、上线和下线服务实例等等。名字服务用一个主线程处理所有IO，并使用线程池的方式异步地处理每个请求。在线的服务实例需要定期（5s）向名字服务发送心跳信号，名字服务才会认为该服务实例仍然在线，否则超过三次未发送心跳信号的话名字服务会自动认为该服务实例下线。

<img src="C:\Users\ruiqizhang\Documents\Typora\nameserver_framwork1.png" alt="nameserver_framwork1" style="zoom:67%;" />

<center>Fig. 2 名字服务架构图</center>

**## 名词解释**

\+ RPC (remote procesure call)：远程过程调用，就是本地代码调用在另一台服务器上提供的服务（例如说一个接口）

  的一个过程。该过程涉及到函数名和入参的序列化和传输，以及服务提供方对其进行反序列化和根据函数名寻找

  服务进程id的步骤。

\+ 服务：就是实现某种功能的一组接口。

\+ 服务实例：提供这组接口的一个进程，用(ip:port)标识。

\+ 服务名字：对服务的描述，在tbuspp里面就是gameid.区服.服务名.用户字段

\+ 服务注册：一个服务实例在向外界提供服务之前，需要向服务中心注册以便调用方能发现它。

\+ 服务发现：就是调用方找到被调用方的过程。

\+ 名字服务：分布式系统中的核心组件之一，它负责为调用者寻找和匹配到合适的被调用者。



## tbuspp配置

在用tbuspp写服务之前，需要清楚我们的游戏是属于哪个名字服务环境的（有DEV正式环境、IDC正式环境、IDC测试环境），这个名字服务环境对应一个TbusppNS的域名。例如说我选择的服务环境域名为devnet.tbusppns.oa.com，http端口为8082（这个端口主要用于与TbusppOMS通信），tcp端口为9092（这个端口主要用于TbusppSDK与名字服务器进行通信）。

那么对应tbuspp_agent的配置为（在`/dev/shm/baseagent/conf/base_agent.conf`这个文件夹）：

```
conf_nameserver {

  host: "devnet.tbusppns.oa.com"

  port: 9092

}

// other configurations...
```

我创建了个游戏叫ruiqi_test，这个游戏的gameid为379440991，游戏key为428a2a1e6bab7b6aa5e1e755dc16a627。

这个gameid和key在客户端初始化时需要用到。

## 一个简单的例子：echo server

这里我通过写一个基于tbuspp的echo服务来熟悉tbuspp API的使用。这里省略具体的细节，只记录使用tbuspp的核心步骤，具体代码在echo_server.cpp里面。

首先是一些配置常量的定义：

```c++
std::string conf = "/dev/shm/baseagent/conf/base_agent.conf";

std::string game_id = "379440991";

std::string game_password = "428a2a1e6bab7b6aa5e1e755dc16a627";

std::string name = "test.echo_server";

std::string user = "ricki";
```

首先是初始当前的tbuspp客户端，他会初始化一个事件句柄`event_handle`，我们通过这个句柄来监听网络中的消息到达事件：

```c++
int ret = baseagent::TBaseAgentClientInit(&event_handle, game_id.c_str(), game_password.c_str(), NULL, conf.c_str());
```

然后就是为我们当前这个服务创建一个名字实例，并向名字服务中心注册我们这个服务：

```c++
InstanceObj *serv_instance = baseagent::CreateInstance(game_id + "." + name, user);

ret = baseagent::RegisterInstance(*recv_instance， baseagent::ENUM_OVERWRITE_WHEN_REG_CONFLICT);
```

随后就是启动（上线）我们这个服务，因为在从启动到上线需要经历一定时间，我们还需要注册上线完成事件：

```c++
RecvSysNotifyMsg notify_obj;

baseagent::StartupCompleteNotifyFunc(&notify_obj);

ret = baseagent::StartupInstance(*recv_instance);
```

这里的`RecvSysNotifyMsg`是一个继承于`baseagent::NotifyEvtToApp`的类，里面实现了虚函数`Notidy`：

```c++
class RecvSysNotifyMsg : public baseagent::NotifyEvtToApp
{
public:
    RecvSysNotifyMsg()
    {
        m_is_startup = false;
    }

    int Notify(
        int cmd,
        const troute::ServerNameAndAddress &dst,
        const troute::ServerNameAndAddress &data,
        int code,
        const std::string *pext)
    {
        if (cmd == tbuspp::Startup_Rsp)
        {
            if (code)
            {	// fail to startup
                exit(-1);
            }
			// starup succeed
            m_is_startup = true;
        }
        return 0;
    }

public:
    bool m_is_startup;
};
```

这个Notify可以理解为一个回调函数：当客户端通过`baseagent::xxxNotifyFunc(&notify_obj)`向tbuspp_agent注册了`xxx`事件的通知，那么当有从名字服务来的`xxx`事件消息时，tbuspp_agent会通过调用`notify_obj`的`Notify()`方法来通知客户端事件的到来，客户端就在`Notify`函数内处理相应的事件。例如说这里我只想监听本服务实例上线事件的通知，那么我们只需要在`Notify`函数内处理`cmd == tbuspp:Startup_Rsp`的情况就可以了。严谨来说我们应该还需要判断`data`里面是否确实是当前服务实例上线的通知，但这里我们就略去了。

我们需要通过自旋等待上线完成：

```c++
while (!notify_obj.m_is_startup)
{

  if (ReadMsg(event_handle, NULL, 0) <= 0)
      usleep(1000 * 10);
}
```

这里会有些奇怪：因为明明是自旋等待，为什么还要有个`ReadMsg`去读取`event_handle`中的消息？tbuspp_agent不是会在有消息来的时候自动调用`notify_obj.Notify()`来修改`m_is_startup`这个标志吗？

我们先来看看`ReadMsg`中的逻辑：

```c++
int ReadMsg(unsigned long long event_handle, char *buf, unsigned int maxlen, const InstanceObj **dst = NULL)
{
	// ...
    int ret = baseagent::PollEvent(event_handle, timeout_ms, &local_instance, &mask, &debugstr);

	// ...
    ret = peek_obj->Peek(&index, &msg, &msg_len, &state, &remote_instance);

	// ...
    peek_obj->Commit(index);
    // ...
}
```

里面有三个关键的步骤：先轮询`event_handle`看是否有消息到来，然后通过`Peek`来读取tbuspp_agent收到的消息，最后处理完这个消息后把这个消息给`commit`掉。现在再来回到刚才那个问题：为什么我们在等待上线完成时需要不断去读取当前客户端收到的消息，这两者看似没有什么关联。但其实是有关联的，如果你把这个读取消息的过程去掉的话会发现程序就死循环在这个上线完成等待过程了。因为客户端需要通过调用`Peek`来驱动tbuspp_agent来收取数据，从而回调`Notify`函数。

完成了注册和上线之后我们就可以开始监听消息了：基本就是一个死循环不断地拉去收到的消息，并将该消息原封不动地发回去：

```c++
while (true)
{
    now = time(NULL);
    if (now - last_heart_time >= 5)
    {
        baseagent::SendHeartbeat(*recv_instance);
        last_heart_time = now;
    }
    // read the message from network
    int msg_len = ReadMsg(event_handle, g_tmp_buffer, sizeof(g_tmp_buffer), &remote_inst);
    if (msg_len <= 0)
    {
        continue;
    }

    std::cout << "recv msg << " << std::string(g_tmp_buffer, msg_len) << std::endl;
    Echo(g_tmp_buffer, msg_len, recv_instance, remote_inst);
}
```

在这个循环里面我们还要注意定期向名字服务发送心跳信息。`ReadMsg`将消息读入到`g_tmp_buffer`中，顺带把发送方的名字给记录到`remote_inst`。最后通过`Echo`函数将消息发送回去：

```c++
void Echo(const char *msg, 
            unsigned int msg_len,
            const InstanceObj *src,
            const ServiceObj *dst)
{
    if (msg_len == 0 || !src || !dst)
        return;
    
    // echo back
    std::string res_msg = "echo: " + std::string(msg, msg_len);
    baseagent::SendMsgToStateful(*src, *dst, 
                                res_msg.c_str(), 
                                res_msg.length(), NULL);
}
```

这里我们调用了`SendMsgToStateful`来发送消息，这是因为我们希望echo到发消息给我们的服务实例，而不是提供相同服务的另一个实例。与此相对的还有一个`SendMsgToStatelessServer`方法，这个方法中只需要指定接收方的名字，也就是说接收方只是停供该名字指定服务的任意一个实例。

## 配套一个ping_server，daemonize

我们还需要写一个ping_server来测试echo_server是否正确运行。这个ping_server从初始化到上线的过程和echo_server是完全一样的，不一样的是我们在死循环中实现的逻辑：

```c++
for (int i = 0; i < FLAGS_number_ping; ++i)
{
    now = time(NULL);
    if (now - last_heart_time >= 5)
    {
        baseagent::SendHeartbeat(*send_instance);
        last_heart_time = now;
    }
    static char idstr[32];
    snprintf(idstr, sizeof(idstr), "%lld  ", msg_id++);
    std::string msg = "ping: " + std::string(idstr);
    if (!remote_inst) 
        ret = baseagent::SendMsgToStatelessServer(*send_instance, *recv_service, msg.c_str(), msg.length(), &remote_inst);
    else 
        ret = baseagent::SendMsgToStateful(*send_instance, *remote_inst, msg.c_str(), msg.length(), NULL);
    if (ret)
    {
        std::cout << "Send msg failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        continue;
    }
    std::cout << "send msg >> " << msg << std::endl;
    // recv response msg
    int msg_len = ReadMsg(event_handle, buffer, sizeof(buffer));
    if (msg_len == -1)
    {
        std::cout << "Fail to receive responsed message." << std::endl;
        break;
    }
    else if (msg_len == 0)
    {
        continue;
    }
    else
    {
        std::cout << "recv response << " << std::string(buffer, msg_len) << std::endl;
    }
}
```

我们首次发送的时候是不指定服务实例的，而仅仅制指定了一个服务名字，因此只能用`SendMsgToStatelessServer`发送消息。但是在第一次发送了之后我们就能知道接受服务实例的名字，因此下次发送给echo_server的时候就用`SendMsgToStateful`。

最后，我们还需要将echo_server给daemonize。因为一般来说一个服务都是以daemon的形式运行的，因此我们需要在echo_server初始化之前加一个实现daemonize的函数：

```c++
static void skeleton_deamon(const char *new_dir, const char *logfile_name)
{
    if (!new_dir || !logfile_name)
        exit(EXIT_FAILURE);

    pid_t pid;

    /* 先让子进程脱离父进程以在背景运行 */
    pid = fork();
    if (pid < 0)
        exit(EXIT_FAILURE);
    if (pid > 0)
        exit(EXIT_SUCCESS);
    
    /* 让当前进程称为新会话组和新进程组的leader */
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    /* 重新配置进程的信号处理函数 */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* 再fork一次，这时候子进程就不是会话组的leader，以此避免进程获取终端 */
    pid = fork();
    if (pid < 0)
        exit(EXIT_FAILURE);
    if (pid > 0)
        exit(EXIT_SUCCESS);

    /* 设置新的文件权限掩码 */
    umask(0);

    /* 更改当前进程的工作目录 */
    chdir(new_dir);

    /* 关掉所有从父进程继承下来的文件 */
    int fd = sysconf(_SC_OPEN_MAX);
    while (fd >= 0)
    {
        close(fd--);
    }
    
    // /* 打开日志文件 */
    // openlog(logfile_name, LOG_PID, LOG_DAEMON);
}
```

