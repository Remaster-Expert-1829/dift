#include <iostream>
#include <string_view>

#ifndef SPOINK_VERSION
#define SPOINK_VERSION "0.0.0"
#endif

int main(int argc, char* argv[]) {
  if (argc == 2 && std::string_view(argv[1]) == "version") {
    std::cout << "spoink " << SPOINK_VERSION << '\n';
    return 0;
  }

  std::cerr << "usage: spoink version\n";
  return 2;
}
