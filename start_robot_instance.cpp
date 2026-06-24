#include <atomic>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <pthread.h>
#include <thread>

#include "robot.hpp"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "Usage: " << argv[0]
                  << "<position> <capabilities_file_name> <conveyor_size>"
                  << std::endl;
        return 1;
    }

    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGUSR1); // nur zum internen Aufwecken beim normalen Ende

    // Wichtig: vor Konstruktion von robot blocken,
    // weil der robot-Konstruktor bereits Threads startet.
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    robot robot_instance(
        std::atoi(argv[1]),
        argv[2],
        std::atoi(argv[3])
    );

    std::atomic_bool finishing{false};

    std::thread signal_thread([&]() {
        int sig = 0;

        while (sigwait(&signals, &sig) == 0) {
            if (sig == SIGUSR1) {
                break;
            }

            if (!finishing.exchange(true)) {
                std::cout << "received signal " << sig << ", stopping robot"
                          << std::endl;
                robot_instance.stop();
            }

            break;
        }
    });

    robot_instance.start();

    finishing.store(true);

    // Falls robot_instance.start() normal zurückkehrt, hängt signal_thread
    // noch in sigwait(). SIGUSR1 weckt ihn nur zum Beenden.
    pthread_kill(signal_thread.native_handle(), SIGUSR1);
    signal_thread.join();

    return 0;
}