#include "jerry.h"
#include <cstdlib>
void* context_alloc_fn(size_t size, void* cb_data)
{
	(void)cb_data;
	return malloc(size);
}
thread_local jerry_context_t* tls_context;
jerry_context_t* jerry_port_get_current_context(void)
{
	/* Returns the context assigned to the thread. */
	return tls_context;
}