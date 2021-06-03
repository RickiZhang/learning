/*
 * This is a simple echo server based on tbuspp
 */

#include "thirdparty/gflags/gflags.h"
#include "base_agent_client_api.h"
#include "server_name_address.h"
#include "base_agent_errno.h"
#include "notify_app.h"
#include "tbuspp_stddef.h"

#include <iostream>

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
DEFINE_string(echo_server, "test.echo_server", "echo server name");
DEFINE_string(user_data, "", "user_data");
DEFINE_int64(wait_timeout, 1, "wait event timeout, unit is ms");

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

    int Notify(
        int cmd,
        const troute::ServerNameAndAddress &dst,
        const troute::ServerNameAndAddress &data,
        int code,
        const std::string *pext)
    {
        std::cout << "recv notify msg, code=" << code << "(" << baseagent::GetErrMsg(code) << ")"
                  << ", cmd=" << cmd
                  << ", dst=" << dst.GetInstanceString()
                  << ", data=" << data.GetInstanceString()
                  << ",data_attr=" << data.GetAttr() << std::endl;

        if (cmd == tbuspp::Startup_Rsp)
        {
            if (code)
            {
                std::cout << "start instance failed, code:" << code << std::endl;
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

// return: length of the received message (0 for no message, -1 for fatal failure)
// if buf is set to NULL, then just drop the receive msg
int ReadMsg(unsigned long long event_handle, char *buf, unsigned int maxlen, const InstanceObj **dst = NULL)
{
    int mask;
    const InstanceObj *local_instance = NULL;
    std::string debugstr;
    int ret = baseagent::PollEvent(event_handle, FLAGS_wait_timeout, &local_instance, &mask, &debugstr);

    if (ret == tmsg::kEMPTY)
        return -1;

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

    int has_recv_pkg = 0;
    const int max_recv_pkg_per_loop = 100;

    ret = peek_obj->Peek(&index, &msg, &msg_len, &state, &remote_instance);

     //If the msg is notify,it will callback Notify,and the ret value is tmsg::kEMPTY
    if (ret == tmsg::kEMPTY)
        return 0;

    if (ret)
    {
        std::cout << "Peek() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        exit(-1);
    }
    if (dst != NULL)
        *dst = remote_instance;
    if (buf != NULL)
        memcpy(buf, msg, std::min(maxlen, msg_len));
    peek_obj->Commit(index);
    return buf != NULL ? std::min(maxlen, msg_len) : 0;
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

    ret = baseagent::TBaseAgentClientInit(&event_handle, FLAGS_game_id.c_str(), FLAGS_game_password.c_str(), NULL, FLAGS_conf.c_str());
    if (ret)
    {
        std::cout << "TBaseAgentClientInit() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        exit(-1);
    }

    //step 2: register instance
    InstanceObj *recv_instance = baseagent::CreateInstance(FLAGS_game_id + "." + FLAGS_echo_server, FLAGS_user_data);

    ret = baseagent::RegisterInstance(*recv_instance, baseagent::ENUM_OVERWRITE_WHEN_REG_CONFLICT);
    if (ret)
    {
        std::cout << "RegisterInstance() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        exit(-1);
    }


    //step 3: start instance
    RecvSysNotifyMsg notify_obj;
    baseagent::StartupCompleteNotifyFunc(&notify_obj);
    baseagent::InstanceOnlineNotifyFunc(&notify_obj);
    baseagent::InstanceOfflineNotifyFunc(&notify_obj);

    ret = baseagent::StartupInstance(*recv_instance);
    if (ret)
    {
        std::cout << "StartupInstance() failed, ret=" << ret << "," << baseagent::GetErrMsg(ret) << std::endl;
        exit(-1);
    }

    static baseagent::IPeekMsg *peek_obj = baseagent::CreatePeekMsgObj();

    //step 4: wait startup notification
    while (!notify_obj.m_is_startup)
    {
        ReadMsg(event_handle, NULL, 0);
    }

    //step 6: waiting for msg
    std::cout << "waitting for msg..." << std::endl;

    const InstanceObj *remote_inst = NULL;
    while (true)
    {
        int msg_len = ReadMsg(event_handle, g_tmp_buffer, sizeof(g_tmp_buffer), &remote_inst);
        if (msg_len <= 0)
        {
            continue;
        }

        std::cout << "recv msg << " << std::string(g_tmp_buffer, msg_len) << std::endl;
        Echo(g_tmp_buffer, msg_len, recv_instance, remote_inst);
    }

    baseagent::DeleteInstanceObj(recv_instance);
    return 0;
}
