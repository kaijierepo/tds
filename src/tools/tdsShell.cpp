#include "tdsShell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jerryscript.h"
#include "jerryscript-ext/handler.h"

static void
print_value(const jerry_value_t jsvalue)
{
    jerry_value_t value;
    /* If there is an error extract the object from it */
    if (jerry_value_is_error(jsvalue))
    {
        printf("Error value detected: ");
        value = jerry_get_value_from_error(jsvalue, false);
    }
    else
    {
        value = jerry_acquire_value(jsvalue);
    }

    if (jerry_value_is_undefined(value))
    {
        printf("undefined");
    }
    else if (jerry_value_is_null(value))
    {
        printf("null");
    }
    else if (jerry_value_is_boolean(value))
    {
        if (jerry_value_is_true(value))
        {
            printf("true");
        }
        else
        {
            printf("false");
        }
    }
    /* Float value */
    else if (jerry_value_is_number(value))
    {
        printf("number: %lf", jerry_get_number_value(value));
    }
    /* String value */
    else if (jerry_value_is_string(value))
    {
        jerry_char_t str_buf_p[256];

        /* Determining required buffer size */
        jerry_size_t req_sz = jerry_get_string_size(value);

        if (req_sz <= 255)
        {
            jerry_string_to_char_buffer(value, str_buf_p, req_sz);
            str_buf_p[req_sz] = '\0';
            printf("%s", (const char*)str_buf_p);
        }
        else
        {
            printf("error: buffer isn't big enough");
        }
    }
    /* Object reference */
    else if (jerry_value_is_object(value))
    {
        printf("[JS object]");
    }

    printf("\n");
    jerry_release_value(value);
}

int doShell(void)
{
    printf("»¶Ó­Ê¹ÓÃ TDS javascript shell \r\n");

    bool is_done = false;

    /* Initialize engine */
    jerry_init(JERRY_INIT_EMPTY);

    /* Register 'print' function from the extensions */
    jerryx_handler_register_global((const jerry_char_t*)"print",
        jerryx_handler_print);

    while (!is_done)
    {
        char cmd[256];
        char* cmd_tail = cmd;
        size_t len = 0;

        printf("> ");

        /* Read next command */
        while (true)
        {
            if (fread(cmd_tail, 1, 1, stdin) != 1 && len == 0)
            {
                is_done = true;
                break;
            }
            if (*cmd_tail == '\n')
            {
                break;
            }

            cmd_tail++;
            len++;
        }

        /* If the command is "quit", break the loop */
        if (!strncmp(cmd, "quit\n", sizeof("quit\n") - 1))
        {
            break;
        }

        jerry_value_t ret_val;

        /* Evaluate entered command */
        ret_val = jerry_eval((const jerry_char_t*)cmd,
            len,
            JERRY_PARSE_NO_OPTS);

        /* Print out the value */
        print_value(ret_val);

        jerry_release_value(ret_val);
    }

    /* Cleanup engine */
    jerry_cleanup();

    return 0;
}