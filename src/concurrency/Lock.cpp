#include "Lock.h"
#include "configuration.h"
#include <cassert>

namespace concurrency
{

#ifdef HAS_FREE_RTOS
Lock::Lock() : handle(xSemaphoreCreateBinary())
{
    assert(handle);
    if (xSemaphoreGive(handle) == false) {
        abort();
    }
}

void Lock::lock()
{
    if (xSemaphoreTake(handle, portMAX_DELAY) == false) {
        abort();
    }
}

bool Lock::try_lock()
{
    // this will return immediately if the lock is not available
    return xSemaphoreTake(handle, 0) == pdTRUE;
}

void Lock::unlock()
{
    if (xSemaphoreGive(handle) == false) {
        abort();
    }
}
#else
Lock::Lock() {}

void Lock::lock() {}

bool Lock::try_lock()
{
    return true;
}

void Lock::unlock() {}
#endif

} // namespace concurrency
