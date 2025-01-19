#include "statistics_writer.hpp"
#include <iostream>
#include <memory>

int main (int argc, char* argv[]) {
    if(argc != 4) {
      std::cerr << "Usage: " + std::string(argv[0]) + " <member_count> <absolute_results_directory_path> <result_file_name>\n";
      std::cerr << "  Example: " + std::string(argv[0]) + " 20 /path/to/results/directory myfilename\n";
      return 1;
    }
    std::uint32_t member_count = std::stoi(argv[1]);
    std::string absolute_results_directory_path(argv[2]);
    std::string result_filename(argv[3]);
    if (member_count <= 0) {
      std::cerr << "member_count must be greater than 0\n";
      return 1;
    }
    result_filename += "-" + std::to_string(member_count);

    std::string slash_char("/");
    if (absolute_results_directory_path.compare(absolute_results_directory_path.length()-1,1,slash_char)) {
        absolute_results_directory_path += "/";
    }
    std::unique_ptr<statistics_writer> sw(statistics_writer::get_instance(member_count, absolute_results_directory_path, result_filename));
    sw->write_statistics();
    return 0;
}