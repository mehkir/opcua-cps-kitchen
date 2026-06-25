#include <atomic>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <pthread.h>
#include <thread>

#include "controller.hpp"
#include "kitchen_mape.hpp"

int main(int argc, char* argv[]) {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGUSR1); // nur zum internen Aufwecken beim normalen Ende

    // Wichtig: vor Konstruktion von controller blocken,
    // weil der controller-Konstruktor bereits Threads startet.
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    controller controller_instance(std::make_unique<kitchen_mape>());

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
                controller_instance.stop();
            }

            break;
        }
    });

    controller_instance.start();

    finishing.store(true);

    // Falls controller_instance.start() normal zurückkehrt, hängt signal_thread
    // noch in sigwait(). SIGUSR1 weckt ihn nur zum Beenden.
    pthread_kill(signal_thread.native_handle(), SIGUSR1);
    signal_thread.join();

    return 0;
}