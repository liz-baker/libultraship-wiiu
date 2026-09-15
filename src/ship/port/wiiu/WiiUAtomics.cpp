#ifdef __WIIU__
// devkitPPC ships no static libatomic, so any std::atomic<uint64_t>/int64_t
// operation the PowerPC 750 has no native instruction for gets lowered by GCC
// to a call to __atomic_*_8, which normally resolves against libatomic. This
// provides the missing 8-byte atomic runtime, matching libatomic's ABI, using
// a single cross-core OSSpinLock critical section (the Espresso is a real
// 3-core chip, so an unsynchronized increment would be a genuine race).
//
// Only __atomic_fetch_add_8 is implemented because it's the only one any
// current Wii U code path reaches (Ship::Part's sNextPartId). Add siblings
// here the same way if a build reports another one missing.

#include <coreinit/spinlock.h>

#include <cstdint>

namespace {
OSSpinLock sAtomic8Lock;
}

extern "C" uint64_t __atomic_fetch_add_8(volatile void* mem, uint64_t val, int /*memorder*/) {
    OSUninterruptibleSpinLock_Acquire(&sAtomic8Lock);
    volatile uint64_t* ptr = static_cast<volatile uint64_t*>(mem);
    uint64_t old = *ptr;
    *ptr = old + val;
    OSUninterruptibleSpinLock_Release(&sAtomic8Lock);
    return old;
}

#endif
