#include "../include/controller.hpp"
#include <string>

controller::controller(uint16_t _start_port, uint32_t _robot_count) : running_(true) {
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    for (size_t i = 0; i < _robot_count; i++) {
        UA_Client* client = UA_Client_new();
        UA_ClientConfig_setDefault(UA_Client_getConfig(client));
        uint16_t remote_port = _start_port + i;
        std::string endpoint = "opc.tcp://localhost:" + std::to_string(remote_port);
        status = UA_Client_connect(client, endpoint.c_str());
        if(status != UA_STATUSCODE_GOOD) {
            UA_Client_delete(client);
        } else {
            port_remote_robot_map_[remote_port] = remote_robot(client, remote_port);
        }
    }
}

controller::~controller() {
}

void
controller::start() {
    for (std::pair<const uint16_t, remote_robot> port_remote_robot : port_remote_robot_map_) {
        port_client_thread_map_[port_remote_robot.first] = std::thread([&port_remote_robot, this]() {
            while(running_) {
                UA_Client_run_iterate(port_remote_robot.second.get_client(), 1000);
            }
        });
    }
    for (auto& port_client_thread : port_client_thread_map_) {
        port_client_thread.second.join();
    }
}

void
controller::stop() {
    running_ = false;
    for (auto& port_client_thread : port_client_thread_map_) {
        port_client_thread.second.join();
    }
}