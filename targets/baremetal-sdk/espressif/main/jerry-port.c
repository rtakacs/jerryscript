/* Copyright JS Foundation and other contributors, http://js.foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdarg.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"

#include "jerryscript-port.h"

static const char ESP_JS_TAG[] = "JS";

/**
 * Translates jerry_log_level to esp_log_level_t.
 *
 * @return logging level for esp
 */
static esp_log_level_t esp_log_level(jerry_log_level_t jerry_log_level)
{
  switch(jerry_log_level)
  {
    case JERRY_LOG_LEVEL_ERROR: return ESP_LOG_ERROR;
    case JERRY_LOG_LEVEL_WARNING: return ESP_LOG_WARN;
    case JERRY_LOG_LEVEL_DEBUG: return ESP_LOG_DEBUG;
    case JERRY_LOG_LEVEL_TRACE: return ESP_LOG_VERBOSE;
  }

  return ESP_LOG_NONE;
}

/**
 * Provide log message implementation for the engine.
 */
void
jerry_port_log (jerry_log_level_t level, /**< log level */
                const char *format, /**< format string */
                ...)  /**< parameters */
{
  va_list args;
  va_start (args, format);
  esp_log_write (esp_log_level (level), ESP_JS_TAG, format, args);
  va_end (args);
} /* jerry_port_log */

/**
 * Implementation of jerry_port_print_char.
 */
void
jerry_port_print_char (char c) /**< the character to print */
{
  putchar (c);
} /* jerry_port_print_char */

/**
 * Implementation of jerry_port_fatal.
 * Calls 'abort' if exit code is non-zero, 'exit' otherwise.
 */
void jerry_port_fatal (jerry_fatal_code_t code) /**< cause of error */
{
  esp_log_write (JERRY_LOG_LEVEL_ERROR, ESP_JS_TAG, "Fatal error: %d", code);
  vTaskSuspend (NULL);
  abort ();
} /* jerry_port_fatal */

/**
 * Implementation of jerry_port_get_current_time.
 * Uses 'gettimeofday' if available on the system, does nothing otherwise.
 *
 * @return milliseconds since Unix epoch if 'gettimeofday' is available
 *         0 - otherwise.
 */
double jerry_port_get_current_time (void)
{
  struct timeval tv;

  if (gettimeofday (&tv, NULL) == 0)
  {
    return ((double) tv.tv_sec) * 1000.0 + ((double) tv.tv_usec) / 1000.0;
  }

  return 0.0;
} /* jerry_port_get_current_time */

/**
 * Dummy function to get the time zone adjustment.
 *
 * @return 0
 */
double
jerry_port_get_local_time_zone_adjustment (double unix_ms, bool is_utc)
{
  /* We live in UTC. */
  return 0;
} /* jerry_port_get_local_time_zone_adjustment */
