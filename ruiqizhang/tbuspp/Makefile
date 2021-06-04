TBUSPP_HOME = /root/tbuspp
LIBS_V2 = -L $(TBUSPP_HOME)/lib64_release -lbase_agent_client_api -L $(TBUSPP_HOME)/lib64_release/thirdparty -lgflags -lcrypto -ldl -lpthread -std=gnu++0x
LIBS_V3 = -L $(TBUSPP_HOME)/lib64_release -ltbuspp -lbase_agent_client_api -L $(TBUSPP_HOME)/lib64_release/thirdparty -lgflags -lcrypto -ldl -lpthread -std=gnu++0x
INC = -I $(TBUSPP_HOME)/include
TARGETS = ping_server echo_server

.PHONY: all clean

all:$(TARGETS)

ping_server: ping_server.cpp
	g++ $< -o $@ $(INC) ${LIBS_V2}

echo_server: echo_server.cpp
	g++ $< -o $@ $(INC) ${LIBS_V2}

clean:
	rm -f $(TARGETS)
