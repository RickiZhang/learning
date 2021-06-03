/*
* This is a simple ping pong server based on tencent tbuspp framework
*/

#include "thirdparty/gflags/gflags.h"
#include "base_agent_client_api.h"
#include "advanced_tbuspp_api.h"
#include "server_name_address.h"
#include "base_agent_errno.h"
#include "notify_app.h"
#include "tbuspp_stddef.h"

#include <iostream>
#include <algorithm>
#include <string>

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

DEFINE_string(conf, "/dev/shm/baseagent/conf/base_agent.conf", "base_agent.conf");
DEFINE_string(game_id, "379440991", "game id");
DEFINE_string(game_password, "428a2a1e6bab7b6aa5e1e755dc16a627", "game password");
DEFINE_string(zone, "learning", "zone in tbuspp namespace");
DEFINE_string(server_name, "", "server name (ping or pong)");
DEFINE_string(user_data, "", "user data");
DEFINE_int64(wait_timeout, 1, "wati event timeout, unit is ms");
DEFINE_int32(send_delay_ms, 1000, "send delay time，unit is millisecond");
DEFINE_bool(self_register, false, "need register self into nameserver");
DEFINE_int64(seq_thredshold, 10, "total number of ping pong");

bool g_need_run = true;

void signal_handler(int sig)
{
    exit(-1);
}

void InitSignal()
{
    signal(SIGHUP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR1, signal_handler);
    signal(SIGUSR2, signal_handler);
    signal(SIGINT, signal_handler);
}

class RecvSysNotifyMsg : public baseagent::NotifyEvtToApp
{
public:
    RecvSysNotifyMsg()
    {
        m_is_startup = false;
    }

public:
    // 当tbuspp服务端接收到提示消息时，会调用该类型的Notify函数用于提醒客户端程序接收该消息
    // 类似于一个回调函数。
    int Notify(
        int cmd,    // 通知消息类型
        const troute::ServerNameAndAddress &dst,    // 接收该提醒消息的服务实例名字
        const troute::ServerNameAndAddress &data,   // 该提醒所涉及的服务实例名字
        int code,   // 当提醒为startup事件时，code代表startup是否成功
        const std::string *pext)
    {
        std::cout << "recv notify msg, code=" << code << "(" << baseagent::GetErrMsg(code) << ")"
                  << ", cmd=" << cmd
                  << ", dst=" << dst.GetInstanceString()
                  << ", data=" << data.GetInstanceString()
                  << ",data_attr=" << data.GetAttr() << std::endl;
        // 当前服务实例的startup完成事件
        if (cmd == tbuspp::Startup_Rsp)
        {
            if (code)
            {
                std::cout << "start instance failed" << std::endl;
                exit(-1);
            }
            std::cout << "start instance succ" << std::endl;

            m_is_startup = true;
        }

        return 0;
    }

public:
    bool m_is_startup;
};

char g_tmp_buffer[400 * 1024];
unsigned int g_tmp_size = 0; 

int Response(const char* msg,
            unsigned int msg_len,
            const InstanceObj* src, 
            const ServiceObj* dst)
{
    if (msg_len == 0 || !src || !dst)
        return -1;
    
    // parse the recv message
    int seq;
    std::string msgstr(msg, msg_len);
    std::string pattern = FLAGS_server_name == "ping" ? "pong: " : "ping: ";
    std::size_t msg_head = msgstr.find(pattern);
    if (msg_head == std::string::npos)
        return -1;

    // prepare the response message
    seq = std::stoi(msgstr.substr(msg_head + pattern.length()));
    if (FLAGS_server_name.find("ping") != std::string::npos)
        seq += 1;
    if (seq >= FLAGS_seq_thredshold)
    {
        // need to shut down
        g_need_run = false;
        if (FLAGS_server_name.find("pong") != std::string::npos)
            return 0;
    }
    std::string res_msg = FLAGS_server_name + ": " + std::to_string(seq);

    // response
    // 这里最后一个参数其实可以用来保存实际路由道到的服务实例，下次发消息的时候就不用路由了。但这里
    // 为了简化程序我就不指定了。
    int ret = baseagent::SendMsgToStatelessServer(*src, *dst, 
                                                    res_msg.c_str(), 
                                                    res_msg.length(), NULL);
    if (ret)
    {
        std::cout << "Send msg failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        return -1;
    }
    return 0;
}

// 这个函数纯粹就是用来处理等待startup完成事件的，例程那儿拷过来的，暂时保留着。
void Update(unsigned long long event_handle)
{
    int mask;
    const InstanceObj *local_instance = NULL;
    std::string debugstr;
    int ret = baseagent::PollEvent(event_handle, FLAGS_wait_timeout, &local_instance, &mask, &debugstr);

    if (ret == tmsg::kEMPTY)
        return;

    if (ret)
    {
        std::cout << "PollEvent() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << ", debugstr=" << debugstr << std::endl;
        exit(-1);
    }

    int index = 0;
    const char *msg = NULL;
    unsigned int msg_len = 0;
    tmsg::PkgState state;
    const InstanceObj *remote_instance = NULL;

    static baseagent::IPeekMsg *peek_obj = baseagent::CreatePeekMsgObj();
    peek_obj->Attach(local_instance);

    ret = peek_obj->Peek(&index, &msg, &msg_len, &state, &remote_instance);

    if (ret == tmsg::kEMPTY)
        return;

    if (ret)
    {
        std::cout << "Peek() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        exit(-1);
    }

    std::cout << "update recv msg << " << std::string(msg, msg_len) << std::endl;
    peek_obj->Commit(index);
}

int main(int argc, char *argv[])
{
    unsigned long long event_handle;
    google::ParseCommandLineFlags(&argc, &argv, true);

    //step 1: init client and env
    InitSignal();
    int ret = baseagent::InitErrMsg();
    if (ret)
    {
        std::cout << "InitErrMsg() failed, ret=" << ret << std::endl;
        exit(-1);
    }

    std::string debugstr;
    //std::cout << "Start TBaseAgentClientInit()" << std::endl;
    ret = baseagent::TBaseAgentClientInit(&event_handle, FLAGS_game_id.c_str(), FLAGS_game_password.c_str(), &debugstr, FLAGS_conf.c_str());
    if (ret)
    {
        std::cout << "TBaseAgentClientInit() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << "debugstr=" << debugstr << std::endl;
        exit(-1);
    }
    //std::cout << "TBaseAgentClientInit() success. event_handle:" << event_handle << std::endl;

    //step 2: register instance
    if (FLAGS_server_name == "")
    {
        std::cout << "--server_name need to be specified (ping or pong)." << std::endl;
        return -1;
    }
    InstanceObj *pp_instance = baseagent::CreateInstance(FLAGS_game_id + "." + FLAGS_zone + "." + FLAGS_server_name, FLAGS_user_data);
    ret = baseagent::RegisterInstance(*pp_instance, baseagent::ENUM_OVERWRITE_WHEN_REG_CONFLICT);
    if (ret)
    {
	    std::cout << "RegisterInstance() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
	    exit(-1);
    }

    //std::cout << "RegisterInstance() success" << std::endl;

    //step 3: start instance
    RecvSysNotifyMsg notify_obj;
    baseagent::StartupCompleteNotifyFunc(&notify_obj);
    baseagent::InstanceOnlineNotifyFunc(&notify_obj);
    baseagent::InstanceOfflineNotifyFunc(&notify_obj);

    //std::cout << "Start StartupInstance()" << std::endl;

    ret = baseagent::StartupInstance(*pp_instance);
    if (ret)
    {
        std::cout << "StartupInstance() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        exit(-1);
    }

    //step 4: wait startup notification
    while (!notify_obj.m_is_startup)
    {
        Update(event_handle);
    }
    std::cout << "StartupInstance() success" << std::endl;

    //如果当前服务实例是ping，那么就向pong服务发送起始消息
    ServiceObj* remote_svc = NULL;
    if (FLAGS_server_name == "ping")
    {
        std::cout << "sending the first ping..." << std::endl;
        remote_svc = baseagent::CreateService(FLAGS_game_id + "." + FLAGS_zone + ".pong");
        ret = baseagent::SendMsgToStatelessServer(*pp_instance, *remote_svc, "ping: 0", strlen("ping: 0"), NULL);
    }
    else
        remote_svc = baseagent::CreateService(FLAGS_game_id + "." + FLAGS_zone + ".ping");

    //int timeout_ms = 60000; // 60s
    int max_empty = 100;
    int empty_cnt = 0;
    while (g_need_run)
    {
        int mask;
        const InstanceObj *local_instance = NULL;
        std::string debugstr;
        int ret = baseagent::PollEvent(event_handle, FLAGS_wait_timeout, &local_instance, &mask, &debugstr);
        
        // no message recv
        if (ret == tmsg::kEMPTY)
        {
            continue;
            //std::cout << "no read event, ready to exit..." << std::endl;
            //break;
        }
        if (ret)
        {
            std::cout << "PollEvent() failed, ret = " << ret << "," << baseagent::GetErrMsg(ret) 
                        << ", debugstr=" << debugstr << std::endl;
            break;
        }

        int index = 0;
        const char *msg = NULL;
        unsigned int msg_len = 0;
        tmsg::PkgState state;
        const InstanceObj *remote_instance = NULL;
        
        // 将baseagent::IPeekMsg对象绑定到local_instance上
        static baseagent::IPeekMsg *peek_obj = baseagent::CreatePeekMsgObj();
        peek_obj->Attach(local_instance);
    
        // 我这里先假设收到的消息都是很短的，不需要在一个循环中多次读取
        ret = peek_obj->Peek(&index, &msg, &msg_len, &state, &remote_instance);
        
        // 要么没有数据，要么读取失败
        if (ret == tmsg::kEMPTY)
        {
            if (++empty_cnt > max_empty)
            {
                std::cout << "empty msg cnt exceeds the threshold." << std::endl;
                break;
            }
        }
        if (ret)
        {
            std::cout << "Peek() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
            break;
        }

        // show recv message
        std::cout << std::string(msg, msg_len) << std::endl;
        // 将收到的数据打回到发送方
        if (Response(msg, msg_len, pp_instance, remote_svc))
            break;

        peek_obj->Commit(index);
    }

    baseagent::DeleteServiceObj(remote_svc);

    return 0;
}
