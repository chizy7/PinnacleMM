#include "SecureInput.h"
#include <atomic>
#include <iostream>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace pinnacle {
namespace utils {

std::string SecureInput::readPassword(const std::string& prompt) {
  std::cout << prompt;
  std::cout.flush();

#ifdef _WIN32
  return readPasswordWindows(prompt);
#else
  return readPasswordUnix(prompt);
#endif
}

void SecureInput::clearSensitiveString(std::string& sensitive) {
  if (!sensitive.empty()) {
    // Use volatile to prevent compiler optimization
    volatile char* vptr = const_cast<volatile char*>(sensitive.data());
    for (size_t i = 0; i < sensitive.size(); ++i) {
      vptr[i] = '\0';
    }

    // Memory barrier to ensure clearing is not optimized away
    std::atomic_thread_fence(std::memory_order_acq_rel);

    // Clear the string
    sensitive.clear();
    sensitive.shrink_to_fit();
  }
}

#ifdef _WIN32
std::string SecureInput::readPasswordWindows(const std::string& /* prompt */) {
  std::string password;
  int ch;

  while ((ch = _getch()) != '\r') {
    if (ch == '\b') { // Backspace
      if (!password.empty()) {
        password.pop_back();
        std::cout << "\b \b";
      }
    } else if (ch >= 32 && ch <= 126) { // Printable characters
      password.push_back(static_cast<char>(ch));
      std::cout << '*';
    }
  }

  std::cout << std::endl;
  return password;
}
#else
std::string SecureInput::readPasswordUnix(const std::string& /* prompt */) {
  std::string password;
  struct termios old_termios, new_termios;

  // Get current terminal settings
  if (tcgetattr(STDIN_FILENO, &old_termios) != 0) {
    // Fallback to regular getline if terminal control fails
    std::cout << "(password input not hidden) ";
    std::getline(std::cin, password);
    return password;
  }

  // Set new terminal settings (disable echo)
  new_termios = old_termios;
  new_termios.c_lflag &= ~ECHO;
  new_termios.c_lflag &= ~ECHONL;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios) != 0) {
    // Fallback to regular getline if terminal control fails
    std::cout << "(password input not hidden) ";
    std::getline(std::cin, password);
    return password;
  }

  try {
    // Read password
    std::getline(std::cin, password);
    std::cout << std::endl;

    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
  } catch (...) {
    // Ensure terminal settings are restored even on exception
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
    throw;
  }

  return password;
}
#endif

} // namespace utils
} // namespace pinnacle
