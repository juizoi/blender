/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup intern_mem
 *
 * \brief Read \ref MEMPage
 *
 * \page MEMPage Guarded memory(de)allocation
 *
 * \section aboutmem c-style guarded memory allocation
 *
 * \subsection memabout About the MEM module
 *
 * MEM provides guarded malloc/calloc calls. All memory is enclosed by
 * pads, to detect out-of-bound writes. All blocks are placed in a
 * linked list, so they remain reachable at all times. There is no
 * back-up in case the linked-list related data is lost.
 *
 * \subsection memissues Known issues with MEM
 *
 * There are currently no known issues with MEM. Note that there is a
 * second intern/ module with MEM_ prefix, for use in c++.
 *
 * \subsection memdependencies Dependencies
 * - `stdlib`
 * - `stdio`
 *
 * \subsection memdocs API Documentation
 * See \ref MEM_guardedalloc.h
 */

#ifndef __MEM_GUARDEDALLOC_H__
#define __MEM_GUARDEDALLOC_H__

/* Needed for uintptr_t and attributes, exception, don't use BLI anywhere else in `MEM_*` */
#include "../../source/blender/blenlib/BLI_compiler_attrs.h"
#include "../../source/blender/blenlib/BLI_sys_types.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the length of the allocated memory segment pointed at
 * by vmemh. If the pointer was not previously allocated by this
 * module, the result is undefined.
 */
extern size_t (*MEM_allocN_len)(const void *vmemh) ATTR_WARN_UNUSED_RESULT;

/**
 * Release memory previously allocated by the C-style and #MEM_cnew functions of this module.
 *
 * It is illegal to call this function with data allocated by #MEM_new.
 */
void MEM_freeN(void *vmemh);

#if 0 /* UNUSED */
/**
 * Return zero if memory is not in allocated list
 */
extern short (*MEM_testN)(void *vmemh);
#endif

/**
 * Duplicates a block of memory, and returns a pointer to the
 * newly allocated block.
 * NULL-safe; will return NULL when receiving a NULL pointer. */
extern void *(*MEM_dupallocN)(const void *vmemh) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT;

/**
 * Reallocates a block of memory, and returns pointer to the newly
 * allocated block, the old one is freed. this is not as optimized
 * as a system realloc but just makes a new allocation and copies
 * over from existing memory. */
extern void *(*MEM_reallocN_id)(void *vmemh,
                                size_t len,
                                const char *str) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT
    ATTR_ALLOC_SIZE(2);

/**
 * A variant of realloc which zeros new bytes
 */
extern void *(*MEM_recallocN_id)(void *vmemh,
                                 size_t len,
                                 const char *str) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT
    ATTR_ALLOC_SIZE(2);

#define MEM_reallocN(vmemh, len) MEM_reallocN_id(vmemh, len, __func__)
#define MEM_recallocN(vmemh, len) MEM_recallocN_id(vmemh, len, __func__)

/**
 * Allocate a block of memory of size len, with tag name str. The
 * memory is cleared. The name must be static, because only a
 * pointer to it is stored!
 */
extern void *(*MEM_callocN)(size_t len, const char *str) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT
    ATTR_ALLOC_SIZE(1) ATTR_NONNULL(2);

/**
 * Allocate a block of memory of size (len * size), with tag name
 * str, aborting in case of integer overflows to prevent vulnerabilities.
 * The memory is cleared. The name must be static, because only a
 * pointer to it is stored! */
extern void *(*MEM_calloc_arrayN)(size_t len,
                                  size_t size,
                                  const char *str) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT
    ATTR_ALLOC_SIZE(1, 2) ATTR_NONNULL(3);

/**
 * Allocate a block of memory of size len, with tag name str. The
 * name must be a static, because only a pointer to it is stored!
 */
extern void *(*MEM_mallocN)(size_t len, const char *str) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT
    ATTR_ALLOC_SIZE(1) ATTR_NONNULL(2);

/**
 * Allocate a block of memory of size (len * size), with tag name str,
 * aborting in case of integer overflow to prevent vulnerabilities. The
 * name must be a static, because only a pointer to it is stored!
 */
extern void *(*MEM_malloc_arrayN)(size_t len,
                                  size_t size,
                                  const char *str) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT
    ATTR_ALLOC_SIZE(1, 2) ATTR_NONNULL(3);

/**
 * Allocate an aligned block of memory of size len, with tag name str. The
 * name must be a static, because only a pointer to it is stored!
 */
void *MEM_mallocN_aligned(size_t len,
                          size_t alignment,
                          const char *str) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT
    ATTR_ALLOC_SIZE(1) ATTR_NONNULL(3);

/**
 * Allocate an aligned block of memory that remains uninitialized.
 */
extern void *(*MEM_malloc_arrayN_aligned)(
    size_t len,
    size_t size,
    size_t alignment,
    const char *str) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT ATTR_ALLOC_SIZE(1, 2)
    ATTR_NONNULL(4);

/**
 * Allocate an aligned block of memory that is initialized with zeros.
 */
extern void *(*MEM_calloc_arrayN_aligned)(
    size_t len,
    size_t size,
    size_t alignment,
    const char *str) /* ATTR_MALLOC */ ATTR_WARN_UNUSED_RESULT ATTR_ALLOC_SIZE(1, 2)
    ATTR_NONNULL(4);

/**
 * Print a list of the names and sizes of all allocated memory
 * blocks. as a python dict for easy investigation.
 */
extern void (*MEM_printmemlist_pydict)(void);

/**
 * Print a list of the names and sizes of all allocated memory blocks.
 */
extern void (*MEM_printmemlist)(void);

/** calls the function on all allocated memory blocks. */
extern void (*MEM_callbackmemlist)(void (*func)(void *));

/** Print statistics about memory usage */
extern void (*MEM_printmemlist_stats)(void);

/** Set the callback function for error output. */
extern void (*MEM_set_error_callback)(void (*func)(const char *));

/**
 * Are the start/end block markers still correct ?
 *
 * \retval true for correct memory, false for corrupted memory.
 */
extern bool (*MEM_consistency_check)(void);

/** Attempt to enforce OSX (or other OS's) to have malloc and stack nonzero */
extern void (*MEM_set_memory_debug)(void);

/** Memory usage stats. */
extern size_t (*MEM_get_memory_in_use)(void);
/** Get amount of memory blocks in use. */
extern unsigned int (*MEM_get_memory_blocks_in_use)(void);

/** Reset the peak memory statistic to zero. */
extern void (*MEM_reset_peak_memory)(void);

/** Get the peak memory usage in bytes, including `mmap` allocations. */
extern size_t (*MEM_get_peak_memory)(void) ATTR_WARN_UNUSED_RESULT;

#ifdef __cplusplus
#  define MEM_SAFE_FREE(v) \
    do { \
      if (v) { \
        MEM_freeN(v); \
        (v) = nullptr; \
      } \
    } while (0)
#else
#  define MEM_SAFE_FREE(v) \
    do { \
      void **_v = (void **)&(v); \
      if (*_v) { \
        MEM_freeN(*_v); \
        *_v = NULL; \
      } \
    } while (0)
#endif

/** Overhead for lockfree allocator (use to avoid slop-space). */
#define MEM_SIZE_OVERHEAD sizeof(size_t)
#define MEM_SIZE_OPTIMAL(size) ((size)-MEM_SIZE_OVERHEAD)

#ifndef NDEBUG
extern const char *(*MEM_name_ptr)(void *vmemh);
/**
 * Change the debugging name/string assigned to the memory allocated at \a vmemh. Only affects the
 * guarded allocator. The name must be a static string, because only a pointer to it is stored!
 *
 * Handy when debugging leaking memory allocated by some often called, generic function with a
 * unspecific name. A caller with more info can set a more specific name, and see which call to the
 * generic function allocates the leaking memory.
 */
extern void (*MEM_name_ptr_set)(void *vmemh, const char *str) ATTR_NONNULL();
#endif

/**
 * This should be called as early as possible in the program. When it has been called, information
 * about memory leaks will be printed on exit.
 */
void MEM_init_memleak_detection(void);

/**
 * Use this if we want to call #exit during argument parsing for example,
 * without having to free all data.
 */
void MEM_use_memleak_detection(bool enabled);

/**
 * When this has been called and memory leaks have been detected, the process will have an exit
 * code that indicates failure. This can be used for when checking for memory leaks with automated
 * tests.
 */
void MEM_enable_fail_on_memleak(void);

/**
 * Switch allocator to fast mode, with less tracking.
 *
 * Use in the production code where performance is the priority, and exact details about allocation
 * is not. This allocator keeps track of number of allocation and amount of allocated bytes, but it
 * does not track of names of allocated blocks.
 *
 * \note The switch between allocator types can only happen before any allocation did happen.
 */
void MEM_use_lockfree_allocator(void);

/**
 * Switch allocator to slow fully guarded mode.
 *
 * Use for debug purposes. This allocator contains lock section around every allocator call, which
 * makes it slow. What is gained with this is the ability to have list of allocated blocks (in an
 * addition to the tracking of number of allocations and amount of allocated bytes).
 *
 * \note The switch between allocator types can only happen before any allocation did happen.
 */
void MEM_use_guarded_allocator(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#ifdef __cplusplus

#  include <any>
#  include <memory>
#  include <new>
#  include <type_traits>
#  include <utility>

#  include "intern/mallocn_intern_function_pointers.hh"

/**
 * Conservative value of memory alignment returned by non-aligned OS-level memory allocation
 * functions. For alignments smaller than this value, using non-aligned versions of allocator API
 * functions is okay, allowing use of `calloc`, for example.
 */
#  define MEM_MIN_CPP_ALIGNMENT \
    (__STDCPP_DEFAULT_NEW_ALIGNMENT__ < alignof(void *) ? __STDCPP_DEFAULT_NEW_ALIGNMENT__ : \
                                                          alignof(void *))

/**
 * Allocate new memory for an object of type #T, and construct it.
 * #MEM_delete must be used to delete the object. Calling #MEM_freeN on it is illegal.
 *
 * Do not assume that this ever zero-initializes memory (even when it does), explicitly initialize.
 *
 * Although calling this without arguments will cause zero-initialization for many types, simple
 * changes to the type can break this. Basic explanation:
 * With no arguments, this will initialize using `T()` (value initialization) not `T` (default
 * initialization). Details are involved, but for "C-style" structs ("Plain old Data" structs or
 * structs with a compiler generated constructor) memory will be zero-initialized. A change like
 * simply adding a custom default constructor would change initialization behavior.
 * See: https://stackoverflow.com/a/4982720, https://stackoverflow.com/a/620402
 */
template<typename T, typename... Args>
inline T *MEM_new(const char *allocation_name, Args &&...args)
{
  void *buffer = mem_guarded::internal::mem_mallocN_aligned_ex(
      sizeof(T), alignof(T), allocation_name, mem_guarded::internal::AllocationType::NEW_DELETE);
  return new (buffer) T(std::forward<Args>(args)...);
}

/**
 * Destruct and deallocate an object previously allocated and constructed with #MEM_new, or some
 * type-overloaded `new` operators using MEM_guardedalloc as backend.
 *
 * As with the `delete` C++ operator, passing in `nullptr` is allowed and does nothing.
 *
 * It is illegal to call this function with data allocated by #MEM_cnew or the C-style allocation
 * functions of this module.
 */
template<typename T> inline void MEM_delete(const T *ptr)
{
  static_assert(
      !std::is_void_v<T>,
      "MEM_delete on a void pointer is not possible, `static_cast` it to the correct type");
  if (ptr == nullptr) {
    return;
  }
  /* C++ allows destruction of `const` objects, so the pointer is allowed to be `const`. */
  ptr->~T();
  mem_guarded::internal::mem_freeN_ex(const_cast<T *>(ptr),
                                      mem_guarded::internal::AllocationType::NEW_DELETE);
}

/**
 * Helper shortcut to #MEM_delete, that also ensures that the target pointer is set to nullptr
 * after deleting it.
 */
#  define MEM_SAFE_DELETE(v) \
    do { \
      if (v) { \
        MEM_delete(v); \
        (v) = nullptr; \
      } \
    } while (0)

/**
 * Allocate zero-initialized memory for an object of type #T. The constructor of #T is not called,
 * therefore this should only be used with trivial types (like all C types).
 *
 * #MEM_freeN must be used to free a pointer returned by this call. Calling #MEM_delete on it is
 * illegal.
 */
template<typename T> inline T *MEM_cnew(const char *allocation_name)
{
  static_assert(std::is_trivial_v<T>, "For non-trivial types, MEM_new must be used.");
  return static_cast<T *>(MEM_calloc_arrayN_aligned(1, sizeof(T), alignof(T), allocation_name));
}

/**
 * Same as MEM_cnew but for arrays, better alternative to #MEM_calloc_arrayN.
 */
template<typename T> inline T *MEM_cnew_array(const size_t length, const char *allocation_name)
{
  static_assert(std::is_trivial_v<T>, "For non-trivial types, MEM_new must be used.");
  return static_cast<T *>(
      MEM_calloc_arrayN_aligned(length, sizeof(T), alignof(T), allocation_name));
}

/**
 * Allocate memory for an object of type #T and memory-copy `other` into it.
 * Only applicable for trivial types.
 *
 * This function works around the problem of copy-constructing DNA structs which contains
 * deprecated fields: some compilers will generate access deprecated field warnings in implicitly
 * defined copy constructors.
 *
 * This is a better alternative to #MEM_dupallocN.
 */
template<typename T> inline T *MEM_cnew(const char *allocation_name, const T &other)
{
  static_assert(std::is_trivial_v<T>, "For non-trivial types, MEM_new must be used.");
  T *new_object = static_cast<T *>(MEM_mallocN_aligned(sizeof(T), alignof(T), allocation_name));
  if (new_object) {
    memcpy(new_object, &other, sizeof(T));
  }
  return new_object;
}

template<typename T> inline void MEM_freeN(T *ptr)
{
#  ifdef _MSC_VER
  /* MSVC seems to consider C-style types using the DNA_DEFINE_CXX_METHODS as non-trivial. GCC
   * and clang (both on linux, OSX and clang-cl on Windows on Arm) do not.
   *
   * So for now, disable the triviality check on MSVC. */
  static_assert(std::is_trivially_destructible_v<T>,
                "For non-trivial types, MEM_delete must be used.");
#  else
  static_assert(std::is_trivial_v<T>, "For non-trivial types, MEM_delete must be used.");
#  endif
  mem_guarded::internal::mem_freeN_ex(const_cast<void *>(static_cast<const void *>(ptr)),
                                      mem_guarded::internal::AllocationType::ALLOC_FREE);
}

/** Allocation functions (for C++ only). */
#  define MEM_CXX_CLASS_ALLOC_FUNCS(_id) \
   public: \
    void *operator new(size_t num_bytes) \
    { \
      return mem_guarded::internal::mem_mallocN_aligned_ex( \
          num_bytes, \
          __STDCPP_DEFAULT_NEW_ALIGNMENT__, \
          _id, \
          mem_guarded::internal::AllocationType::NEW_DELETE); \
    } \
    void *operator new(size_t num_bytes, std::align_val_t alignment) \
    { \
      return mem_guarded::internal::mem_mallocN_aligned_ex( \
          num_bytes, size_t(alignment), _id, mem_guarded::internal::AllocationType::NEW_DELETE); \
    } \
    void operator delete(void *mem) \
    { \
      if (mem) { \
        mem_guarded::internal::mem_freeN_ex(mem, \
                                            mem_guarded::internal::AllocationType::NEW_DELETE); \
      } \
    } \
    void *operator new[](size_t num_bytes) \
    { \
      return mem_guarded::internal::mem_mallocN_aligned_ex( \
          num_bytes, \
          __STDCPP_DEFAULT_NEW_ALIGNMENT__, \
          _id "[]", \
          mem_guarded::internal::AllocationType::NEW_DELETE); \
    } \
    void *operator new[](size_t num_bytes, std::align_val_t alignment) \
    { \
      return mem_guarded::internal::mem_mallocN_aligned_ex( \
          num_bytes, \
          size_t(alignment), \
          _id "[]", \
          mem_guarded::internal::AllocationType::NEW_DELETE); \
    } \
    void operator delete[](void *mem) \
    { \
      if (mem) { \
        mem_guarded::internal::mem_freeN_ex(mem, \
                                            mem_guarded::internal::AllocationType::NEW_DELETE); \
      } \
    } \
    void *operator new(size_t /*count*/, void *ptr) \
    { \
      return ptr; \
    } \
    /** \
     * This is the matching delete operator to the placement-new operator above. \
     * Both parameters \
     * will have the same value. Without this, we get the warning C4291 on windows. \
     */ \
    void operator delete(void * /*ptr_to_free*/, void * /*ptr*/) {}

/**
 * Construct a T that will only be destructed after leak detection is run.
 *
 * This call is thread-safe. Calling code should typically keep a reference to that data as a
 * `static thread_local` variable, or use some lock, to prevent concurrent accesses.
 *
 * The returned value should not own any memory allocated with `MEM_*` functions, since these would
 * then be detected as leaked.
 */
template<typename T, typename... Args> T &MEM_construct_leak_detection_data(Args &&...args)
{
  std::shared_ptr<T> data = std::make_shared<T>(std::forward<Args>(args)...);
  std::any any_data = std::make_any<std::shared_ptr<T>>(data);
  mem_guarded::internal::add_memleak_data(any_data);
  return *data;
}

#endif /* __cplusplus */

#endif /* __MEM_GUARDEDALLOC_H__ */
