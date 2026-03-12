#include "app.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance, LPSTR command_line, int show_command)
{
  (void)previous_instance;
  (void)command_line;
  (void)instance;
  (void)show_command;
  return app_run();
}

#else

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  return app_run();
}

#endif
