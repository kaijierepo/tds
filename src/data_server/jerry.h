#pragma once
#include "jerryscript.h"
void* context_alloc_fn(size_t size, void* cb_data);
extern thread_local jerry_context_t* tls_context;
jerry_context_t* jerry_port_get_current_context(void);