#define _CRT_SECURE_NO_WARNINGS
#include <ctype.h>  // for tolower()
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool ask(const char* prompt) {
  char input[4];  // Buffer for "YES", "NO", or null terminator
  bool valid_input = false;
  bool result = false;

  do {
    // Display prompt
    printf("%s [YES/NO]: ", prompt);
    fflush(stdout);

    // Read input line
    if (fgets(input, sizeof(input), stdin) == NULL) {
      // Handle EOF or error
      continue;
    }

    // Remove trailing newline if present
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
      input[len - 1] = '\0';
      len--;
    }

    // Convert input to uppercase for case-insensitive comparison
    for (size_t i = 0; i < len; i++) {
      input[i] = toupper(input[i]);
    }

    // Validate input
    if (strcmp(input, "YES") == 0) {
      valid_input = true;
      result = true;
    } else if (strcmp(input, "NO") == 0) {
      valid_input = true;
      result = false;
    } else {
      printf("Invalid input. Please enter YES or NO.\n");
    }

    // Consume any remaining characters in the input buffer
    if (len == sizeof(input) - 1 && input[len - 1] != '\n') {
      int ch;
      while ((ch = getchar()) != '\n' && ch != EOF) {
      }
    }
  } while (!valid_input);

  // Print confirmation
  printf("Received: %s\n\n", input);

  return result;
}

int exec(const char* command, char* sout, char* serr) {
  HANDLE hStdOutRead, hStdOutWrite;
  HANDLE hStdErrRead, hStdErrWrite;
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

  // Create pipes for stdout and stderr
  if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0))
    return -1;
  if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0))
    return -1;

  // Ensure the read handles are not inherited
  SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);

  PROCESS_INFORMATION pi;
  STARTUPINFOA si;
  ZeroMemory(&pi, sizeof(pi));
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = hStdOutWrite;
  si.hStdError = hStdErrWrite;
  si.hStdInput = NULL;

  // Replace line breaks with " & " to allow multiple commands in one line
  // char multi_cmd[2048];
  // size_t j = 0;
  // for (size_t i = 0; command[i] && j < sizeof(multi_cmd) - 1; ++i) {
  //   if (command[i] == '\r' && command[i + 1] == '\n') {
  //     if (j + 3 < sizeof(multi_cmd) - 1) {
  //       multi_cmd[j++] = ' ';
  //       multi_cmd[j++] = '&';
  //       multi_cmd[j++] = ' ';
  //     }
  //     ++i;  // skip '\n'
  //   } else if (command[i] == '\n' || command[i] == '\r') {
  //     if (j + 3 < sizeof(multi_cmd) - 1) {
  //       multi_cmd[j++] = ' ';
  //       multi_cmd[j++] = '&';
  //       multi_cmd[j++] = ' ';
  //     }
  //   } else {
  //     multi_cmd[j++] = command[i];
  //   }
  // }
  // multi_cmd[j] = '\0';

  // Format the full command line
  char cmdline[2048];
  // snprintf(cmdline, sizeof(cmdline), "cmd.exe /C \"%s\"", multi_cmd);
  snprintf(cmdline, sizeof(cmdline), "cmd.exe /C \"%s\"", command);

  BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);

  // Close write ends in parent process
  CloseHandle(hStdOutWrite);
  CloseHandle(hStdErrWrite);

  if (!ok) {
    CloseHandle(hStdOutRead);
    CloseHandle(hStdErrRead);
    return -2;
  }

  // Read from stdout pipe
  DWORD n;
  char buffer[4096];
  sout[0] = '\0';
  serr[0] = '\0';
  while (ReadFile(hStdOutRead, buffer, sizeof(buffer) - 1, &n, NULL) && n > 0) {
    buffer[n] = '\0';
    strcat(sout, buffer);
  }

  while (ReadFile(hStdErrRead, buffer, sizeof(buffer) - 1, &n, NULL) && n > 0) {
    buffer[n] = '\0';
    strcat(serr, buffer);
  }

  // Wait for child to finish
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  // Cleanup
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(hStdOutRead);
  CloseHandle(hStdErrRead);

  return (int)exit_code;
}

// usage:
//   #define MAX_SIZE (4096)
//   char out[MAX_SIZE], err[MAX_SIZE], res[MAX_SIZE];
//   int code = prompt_exec("podman ps", out, err, res);
//   printf("%s", res);
int prompt_exec(const char* command, char* out, char* err, char* res) {
  printf("\n🔴 About to execute:\n\n  %s\n\n", command);

  if (!ask("Proceed?")) {
    // printf("Command aborted by user.\n");
    return -1;
  }

  int code = exec(command, out, err);

  sprintf(
      res,
      "I executed the command:\n"
      "```\n%s\n```\n\n"
      "The result was:\n"
      "exit code: `%d`\n"
      "stderr:\n"
      "```\n%s\n```\n\n"
      "stdout:\n"
      "```\n%s\n```\n",
      command,
      code,
      err,
      out);

  return code;
}