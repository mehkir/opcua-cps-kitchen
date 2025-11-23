#include "statistics_writer.hpp"
#include <iostream>
#include <memory>

int main (int argc, char* argv[]) {
    if(argc != 2) {
      std::cerr << "Usage: " + std::string(argv[0]) + " <robot_count>\n";
      std::cerr << "  Example: " + std::string(argv[0]) + " 4\n";
      return EXIT_FAILURE;
    }
    std::uint32_t robot_count = std::stoi(argv[1]);
    if (robot_count < 1) {
      std::cerr << "robot_count must be greater than 0\n";
      return EXIT_FAILURE;
    }
    std::unique_ptr<statistics_writer> sw(statistics_writer::get_instance(robot_count));
    sw->write_statistics();
    return EXIT_SUCCESS;
}