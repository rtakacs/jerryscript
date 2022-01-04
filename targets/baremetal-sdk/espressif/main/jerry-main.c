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

#include "jerryscript.h"
#include "jerryscript-ext/handler.h"

/**
 * Jerryscript simple test
 */
void app_main()
{
  jerry_value_t ret_value = jerry_undefined ();

  const jerry_char_t script[] = "print ('Hello, World!');";
  printf ("This test run the following script code: [%s]\n\n", script);

  /* Initialize engine */
  jerry_init (JERRY_INIT_EMPTY);

  /* Register the print function in the global object */
  jerryx_handler_register_global ("print", jerryx_handler_print);

  /* Setup Global scope code */
  ret_value = jerry_parse (script, sizeof (script) - 1, NULL);

  if (!jerry_value_is_exception (ret_value))
  {
    /* Execute the parsed source code in the Global scope */
    ret_value = jerry_run (ret_value);
  }

  if (jerry_value_is_exception (ret_value))
  {
    printf ("Script Error!");
  }

  jerry_value_free (ret_value);

  /* Cleanup engine */
  jerry_cleanup ();
} /* test_jerry */
