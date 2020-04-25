#pragma once

#include <stdio.h>
#include <stdint.h>
#include "misc.h"

static int g_TestsFailed = 0;

static inline void TestFailed(const char *szExpr, uint32_t nLine)
{
  printf("Test failed! Line=%u, Expression: %s\n", nLine, szExpr);
  g_TestsFailed++;
}

#define verify_test(x)          \
  do                            \
  {                             \
    if (!(x))                   \
      TestFailed(#x, __LINE__); \
  } while (false)

static inline int getTestsExistCode()
{
  return g_TestsFailed ? -1 : 0;
}

#define VERIFY_TEST verify_test

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

#define DEBUG_PRINT(msg, arr, len)                                         \
  printf(ANSI_COLOR_CYAN "Line=%u" ANSI_COLOR_RESET ", Msg=%s ", __LINE__, \
         msg);                                                             \
  printf(ANSI_COLOR_YELLOW);                                               \
  for (size_t i = 0; i < len; i++)                                         \
  {                                                                        \
    printf("%02x", arr[i]);                                                \
  }                                                                        \
  printf(ANSI_COLOR_RESET "\n");

int IS_EQUAL_HEX(const char *hex_str, const uint8_t *bytes, size_t str_size)
{
  uint8_t tmp[str_size / 2];
  hex2bin(tmp, hex_str, str_size);
  return memcmp(tmp, bytes, str_size / 2) == 0;
}
