#pragma once

#include "../freertosinc.h"

namespace concurrency
{

/**
 * @brief Simple wrapper around FreeRTOS API for implementing a mutex lock
 */
class Lock
{
  public:
    Lock();

    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;

    /// Locks the lock.
    //
    // Must not be called from an ISR.
    void lock();

    /// Like lock(), but returns immediately if the lock is not available (non-blocking).
    /// @return true if the lock was acquired
    bool try_lock();

    // Unlocks the lock.
    //
    // Must not be called from an ISR.
    void unlock();

  private:
#ifdef HAS_FREE_RTOS
    SemaphoreHandle_t handle;
#endif
};

} // namespace concurrency
