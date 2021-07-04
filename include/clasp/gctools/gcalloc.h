/*
    File: gcalloc.h
*/

/*
Copyright (c) 2014, Christian E. Schafmeister
 
CLASP is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.
 
See directory 'clasp/licenses' for full details.
 
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
/* -^- */
#ifndef gc_gcalloc_H
#define gc_gcalloc_H

//#define BOEHM_ONE_BIG_STACK 1
//#define DEBUG_BOEHM_STACK 1
// #define DEBUG_BOEHMPRECISE_ALLOC 1
//#define DEBUG_CONS_ALLOC 1


#include <limits>
#include <clasp/gctools/interrupt.h>
#include <clasp/gctools/threadlocal.fwd.h>
#include <clasp/gctools/snapshotSaveLoad.fwd.h>


#define STACK_ALIGNMENT alignof(char *)
#define STACK_ALIGN_UP(size) \
  (((size)+STACK_ALIGNMENT - 1) & ~(STACK_ALIGNMENT - 1))

namespace gctools {
  extern uintptr_t global_strong_weak_kind;
};

namespace gctools {
template <class OT, bool Needed = true>
struct GCObjectInitializer {};

template <class OT>
struct GCObjectInitializer<OT, true> {
  typedef smart_ptr<OT> smart_pointer_type;
  static void initializeIfNeeded(smart_pointer_type sp) {
    if ( sp.generalp() ) {
      sp.unsafe_general()->initialize();
    }
  };
};

template <class OT>
struct GCObjectInitializer<OT, false> {
  typedef smart_ptr<OT> smart_pointer_type;
  static void initializeIfNeeded(smart_pointer_type sp){
      // initialize not needed
  };
};

#ifdef TAGGED_POINTER
template <class OT>
struct GCObjectInitializer<tagged_pointer<OT>, true> {
  typedef tagged_pointer<OT> functor_pointer_type;
  static void initializeIfNeeded(functor_pointer_type sp) {
    throw_hard_error("Figure out why this is being invoked, you should never need to initialize a functor!");
  };
};
template <class OT>
struct GCObjectInitializer<tagged_pointer<OT>, false> {
  typedef tagged_pointer<OT> functor_pointer_type;
  static void initializeIfNeeded(functor_pointer_type sp){
      // initialize not needed
  };
};
#endif // end TAGGED_POINTER
}

#if defined(USE_BOEHM) || defined(USE_MPS)

#if defined(USE_BOEHM)
namespace gctools {
    template <class T>
    class root_allocator {};
};
#endif

inline void* verify_alignment(void* ptr) {
  if ((((uintptr_t)ptr)&gctools::ptr_mask) != (uintptr_t)ptr) {
    printf("%s:%d The pointer at %p is not aligned properly\n", __FILE__, __LINE__, ptr);
    abort();
  }
  return ptr;
}

inline void* monitor_alloc(void* ptr,size_t sz) {
  // printf("%s:%d Allocate pointer size: %lu at %p\n", __FILE__, __LINE__, sz, ptr);
  if (sz==32784) {
    printf("%s:%d  It's one of the special objects that fail when enumerating\n", __FILE__, __LINE__ );
  }
  return ptr;
}

namespace gctools {
extern void* malloc_kind_error(uintptr_t expected_kind, uintptr_t kind, uintptr_t size, uintptr_t stmp, void* addr);
};

#ifdef DEBUG_ALLOC_ALIGNMENT
#define MAYBE_VERIFY_ALIGNMENT(ptr) verify_alignment(ptr)
#else
#define MAYBE_VERIFY_ALIGNMENT(ptr) (void*)ptr
#endif

#if 1
#define MAYBE_MONITOR_ALLOC(_alloc_,_sz_) monitor_alloc(_alloc_,_sz_)
#else
#define MAYBE_MONITOR_ALLOC(_alloc_,_sz_) (_alloc_)
#endif

#if defined(USE_BOEHM)
# if TAG_BITS==3
#  define ALIGNED_GC_MALLOC(sz) MAYBE_MONITOR_ALLOC(GC_MALLOC(sz),sz)
#  define ALIGNED_GC_MALLOC_ATOMIC(sz) MAYBE_MONITOR_ALLOC(GC_MALLOC_ATOMIC(sz),sz)
#  define ALIGNED_GC_MALLOC_UNCOLLECTABLE(sz) MAYBE_MONITOR_ALLOC(GC_MALLOC_UNCOLLECTABLE(sz),sz)
#  define ALIGNED_GC_MALLOC_KIND(stmp,sz,knd,kndaddr) MAYBE_MONITOR_ALLOC(GC_malloc_kind_global(sz,knd),sz)
#  define ALIGNED_GC_MALLOC_STRONG_WEAK_KIND(sz,knd) MAYBE_MONITOR_ALLOC(GC_malloc_kind_global(sz,knd),sz)
#  define ALIGNED_GC_MALLOC_ATOMIC_KIND(stmp,sz,knd,kndaddr) MAYBE_MONITOR_ALLOC((knd==GC_I_PTRFREE) ? GC_malloc_kind_global(sz,knd) : malloc_kind_error(GC_I_PTRFREE,knd,sz,stmp,kndaddr), sz)
#  define ALIGNED_GC_MALLOC_UNCOLLECTABLE_KIND(stmp,sz,knd,kndaddr) MAYBE_MONITOR_ALLOC(GC_generic_malloc_uncollectable(sz,knd),sz)
# else
#  error "There is more work to do to support more than 3 tag bits"
#  define ALIGNED_GC_MALLOC(sz) MAYBE_VERIFY_ALIGNMENT(GC_memalign(Alignment(),sz))
#  define ALIGNED_GC_MALLOC_ATOMIC(sz) MAYBE_VERIFY_ALIGNMENT(GC_memalign(Alignment(),sz))
#  define ALIGNED_GC_MALLOC_UNCOLLECTABLE(sz) MAYBE_VERIFY_ALIGNMENT((void*)gctools::AlignUp((uintptr_t)GC_MALLOC_UNCOLLECTABLE(sz+Alignment())))
# endif
#endif

namespace gctools {
#ifdef USE_BOEHM

template <typename Cons, typename...ARGS>
inline Cons* do_boehm_cons_allocation(size_t size,ARGS&&... args)
{ RAII_DISABLE_INTERRUPTS();
#ifdef USE_PRECISE_GC
  Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_KIND(STAMP_UNSHIFT_MTAG(STAMPWTAG_CONS),size,global_cons_kind,&global_cons_kind));
# ifdef DEBUG_BOEHMPRECISE_ALLOC
  printf("%s:%d:%s cons = %p\n", __FILE__, __LINE__, __FUNCTION__, cons );
# endif
#else
  Header_s::StampWtagMtag* header = reinterpret_cast<Header_s::StampWtagMtag*>(ALIGNED_GC_MALLOC(size));
#endif
  Cons* cons = (Cons*)HeaderPtrToConsPtr(header);
  new (header) Header_s::StampWtagMtag(cons);
  new (cons) Cons(std::forward<ARGS>(args)...);
  return cons;
}


inline Header_s* do_boehm_atomic_allocation(const Header_s::StampWtagMtag& the_header, size_t size) 
{
  RAII_DISABLE_INTERRUPTS();
  size_t true_size = size;
#ifdef DEBUG_GUARD
  size_t tail_size = ((rand()%8)+1)*Alignment();
  true_size += tail_size;
#endif
#ifdef USE_PRECISE_GC
  uintptr_t stamp = the_header.stamp();
  Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_ATOMIC_KIND(stamp,true_size,global_stamp_layout[stamp].boehm._kind,&global_stamp_layout[stamp].boehm._kind));
#else
  Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_ATOMIC(true_size));
#endif
  my_thread_low_level->_Allocations.registerAllocation(the_header.unshifted_stamp(),true_size);
#ifdef DEBUG_GUARD
  memset(header,0x00,true_size);
  new (header) Header_s(the_header,size,tail_size,true_size);
#else
  new (header) Header_s(the_header);
#endif
  return header;
};
#endif

#ifdef USE_BOEHM
inline Header_s* do_boehm_weak_allocation(const Header_s::StampWtagMtag& the_header, size_t size) 
{
  RAII_DISABLE_INTERRUPTS();
  size_t true_size = size;
#ifdef USE_PRECISE_GC
  Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_ATOMIC(true_size));
//   Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_STRONG_WEAK_KIND_ATOMIC(true_size,global_strong_weak_kind));
#else
  Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_ATOMIC(true_size));
#endif
  my_thread_low_level->_Allocations.registerWeakAllocation(the_header._value,true_size);
#ifdef DEBUG_GUARD
  memset(header,0x00,true_size);
  new (header) Header_s(the_header,0,0,true_size);
#else
  new (header) Header_s(the_header);
#endif
  return header;
};
#endif

#ifdef USE_BOEHM
inline Header_s* do_boehm_general_allocation(const Header_s::StampWtagMtag& the_header,  size_t size) 
{
  RAII_DISABLE_INTERRUPTS();
  size_t true_size = size;
#ifdef DEBUG_GUARD
  size_t tail_size = ((rand()%8)+1)*Alignment();
  true_size += tail_size;
#endif
#ifdef USE_PRECISE_GC
  Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_KIND(the_header.stamp(),true_size,global_stamp_layout[the_header.stamp()].boehm._kind,&global_stamp_layout[the_header.stamp()].boehm._kind));
# ifdef DEBUG_BOEHMPRECISE_ALLOC
  printf("%s:%d:%s header = %p\n", __FILE__, __LINE__, __FUNCTION__, header );
# endif
#else
  Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC(true_size));
#endif
  my_thread_low_level->_Allocations.registerAllocation(the_header.unshifted_stamp(),true_size);
#ifdef DEBUG_GUARD
  memset(header,0x00,true_size);
  new (header) Header_s(the_header,size,tail_size,true_size);
#else
  new (header) Header_s(the_header);
#endif
  return header;
};
#endif



#ifdef USE_BOEHM
inline Header_s* do_boehm_uncollectable_allocation(const Header_s::StampWtagMtag& the_header, size_t size) 
{
  RAII_DISABLE_INTERRUPTS();
  size_t true_size = size;
#ifdef DEBUG_GUARD
  size_t tail_size = ((rand()%8)+1)*Alignment();
  true_size += tail_size;
#endif
#ifdef USE_PRECISE_GC
  Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_UNCOLLECTABLE_KIND(the_header.stamp(),true_size,global_stamp_layout[the_header.stamp()].boehm._kind,&global_stamp_layout[the_header.stamp()].boehm._kind));
# ifdef DEBUG_BOEHMPRECISE_ALLOC
  printf("%s:%d:%s header = %p\n", __FILE__, __LINE__, __FUNCTION__, header );
# endif
#else
  Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_UNCOLLECTABLE(true_size));
#endif
  my_thread_low_level->_Allocations.registerAllocation(the_header.unshifted_stamp(),true_size);
#ifdef DEBUG_GUARD
  memset(header,0x00,true_size);
  new (header) Header_s(the_header,size,tail_size,true_size);
#else
  new (header) Header_s(the_header);
#endif
  return header;
};
#endif
};


namespace gctools {

class DontRegister {};
class DoRegister {};

template <typename Cons,typename Register=DontRegister>
struct ConsSizeCalculator {
  static inline size_t value() {
    static_assert(AlignUp(sizeof(Cons)+SizeofConsHeader()) == 24);
    size_t size = AlignUp(sizeof(Cons)+SizeofConsHeader());
    return size;
  }
};

template <typename Cons>
struct ConsSizeCalculator<Cons,DoRegister> {
  static inline size_t value() {
    size_t size = ConsSizeCalculator<Cons,DontRegister>::value();
    my_thread_low_level->_Allocations.registerAllocation(STAMPWTAG_CONS,size);
    return size;
  }
};

#ifdef USE_MPS
extern void bad_cons_mps_reserve_error();

template <typename ConsType, typename Register, typename... ARGS>
#ifdef ALWAYS_INLINE_MPS_ALLOCATIONS
__attribute__((always_inline))
#else
inline
#endif
smart_ptr<ConsType> do_cons_mps_allocation(mps_ap_t& allocation_point,
                                           const char* ap_name,
                                           ARGS &&... args) {
  gc::smart_ptr<ConsType> tagged_obj;
  { RAII_DISABLE_INTERRUPTS();
    RAII_DEBUG_RECURSIVE_ALLOCATIONS((size_t)STAMPWTAG_CONS);
    // printf("%s:%d cons_mps_allocation\n", __FILE__, __LINE__ );
    mps_addr_t addr;
    ConsType* cons;
    size_t cons_size = ConsSizeCalculator<ConsType,Register>::value();
    do {
      mps_res_t res = mps_reserve(&addr, allocation_point, cons_size);
      if ( res != MPS_RES_OK ) bad_cons_mps_reserve_error();
      cons = reinterpret_cast<ConsType*>(addr);
      new (cons) ConsType(std::forward<ARGS>(args)...);
      tagged_obj = smart_ptr<ConsType>((Tagged)tag_cons(cons));
    } while (!mps_commit(allocation_point, addr, cons_size));
    MAYBE_VERIFY_ALIGNMENT((void*)addr);
    //      printf("%s:%d cons_mps_allocation addr=%p size=%lu\n", __FILE__, __LINE__, addr, sizeof(Cons));
  }
  DEBUG_MPS_UNDERSCANNING_TESTS();
  handle_all_queued_interrupts();
#if 0
  globalMpsMetrics.totalMemoryAllocated += cons_size;
  ++globalMpsMetrics.consAllocations;
#endif    
  return tagged_obj;
};


extern void bad_general_mps_reserve_error(mps_ap_t* allocation_point);
  
template <class PTR_TYPE, typename... ARGS>
#ifdef ALWAYS_INLINE_MPS_ALLOCATIONS
__attribute__((always_inline))
#else
inline
#endif
PTR_TYPE general_mps_allocation(const Header_s::StampWtagMtag& the_header,
                                size_t size,
                                mps_ap_t& allocation_point,
                                ARGS &&... args) {
  mps_addr_t addr;
  typedef typename PTR_TYPE::Type T;
  typedef typename GCHeader<T>::HeaderType HeadT;
  RAII_DEBUG_RECURSIVE_ALLOCATIONS((size_t)STAMP_UNSHIFT_MTAG(the_header._value));
  PTR_TYPE tagged_obj;
  T* obj;
  size_t allocate_size = AlignUp(size);
#ifdef DEBUG_GUARD
  size_t tail_size = ((rand()%8)+1)*Alignment();
  allocate_size += tail_size;
#endif
  HeadT *header;
  { RAII_DISABLE_INTERRUPTS(); 
    do {
      mps_res_t res = mps_reserve(&addr, allocation_point, allocate_size);
      if ( res != MPS_RES_OK ) bad_general_mps_reserve_error(&allocation_point);
      header = reinterpret_cast<HeadT *>(addr);
#ifdef DEBUG_GUARD
      memset(header,0x00,allocate_size);
      new (header) HeadT(the_header,size,tail_size, allocate_size);
#else
      new (header) HeadT(the_header);
#endif
      obj = HeaderPtrToGeneralPtr<typename PTR_TYPE::Type>(addr);
      new (obj) (typename PTR_TYPE::Type)(std::forward<ARGS>(args)...);
      tagged_obj = PTR_TYPE(obj);
    } while (!mps_commit(allocation_point, addr, allocate_size));
    MAYBE_VERIFY_ALIGNMENT((void*)addr);
    my_thread_low_level->_Allocations.registerAllocation(the_header.unshifted_stamp(),allocate_size);
  }
#ifdef DEBUG_VALIDATE_GUARD
  header->validate();
#endif
  DEBUG_MPS_UNDERSCANNING_TESTS();
  handle_all_queued_interrupts();
  globalMpsMetrics.totalMemoryAllocated += allocate_size;
#ifdef DEBUG_MPS_SIZE
  {
    if ((((uintptr_t)obj)&ptag_mask)!=0) {
      printf("%s:%d The pointer at %p must be aligned to the Alignment() %lu ptag_mask=0x%zx ((uintptr_t)obj)&ptag_mask) = 0x%zx\n", __FILE__, __LINE__, (void*)obj, Alignment(), ptag_mask, (((uintptr_t)obj)&ptag_mask));
      abort();
    }
    if (AlignUp(allocate_size)!=allocate_size) {
      printf("%s:%d The allocate_size %lu must be a multiple of the Alignment() %lu\n", __FILE__, __LINE__, allocate_size, Alignment());
      abort();
    }
    mps_addr_t nextClient = obj_skip((mps_addr_t)obj);
    int skip_size = (int)((char*)nextClient-(char*)obj);
    if (skip_size != allocate_size) {
      mps_addr_t againNextClient = obj_skip_debug_wrong_size((mps_addr_t)obj,header,(size_t)header->_stamp_wtag_mtag._value,header->_stamp_wtag_mtag.stamp_(),
                                                             allocate_size,
                                                             skip_size,
                                                             ((int)allocate_size-(int)skip_size));
#ifdef DEBUG_GUARD
      printf("      header-size= %lu size= %zu tail_size=%lu \n", sizeof(HeadT), size, tail_size );
#else        
      printf("      header-size= %lu size= %zu\n", sizeof(HeadT), size );
#endif
    }
  }
#endif
  return tagged_obj;
};



template <class PTR_TYPE, typename... ARGS>
inline PTR_TYPE do_mps_weak_allocation(size_t allocate_size,
                                       mps_ap_t& allocation_point,
                                       const char* ap_name,
                                       ARGS &&... args) {
  typedef typename PTR_TYPE::Type T;
  typedef typename GCHeader<T>::HeaderType HeadT;
  PTR_TYPE tagged_obj;
  mps_addr_t addr;
  T* obj;
  { RAII_DISABLE_INTERRUPTS();
    RAII_DEBUG_RECURSIVE_ALLOCATIONS((size_t)STAMPWTAG_UNUSED);
    allocate_size = AlignUp(allocate_size);
    do {
      mps_res_t res = mps_reserve(&addr, allocation_point, allocate_size);
      if (res != MPS_RES_OK)
        throw_hard_error("Out of memory");
      GC_LOG(("allocated @%p %zu bytes\n", addr, allocate_size));
      obj = reinterpret_cast<T*>(addr);
      if (!obj)
        throw_hard_error("NULL address in allocate!");
      new (obj) T(std::forward<ARGS>(args)...);
      tagged_obj = PTR_TYPE(obj);
    } while (!mps_commit(allocation_point, addr, allocate_size));
    MAYBE_VERIFY_ALIGNMENT((void*)addr);
    my_thread_low_level->_Allocations.registerAllocation(STAMPWTAG_null,allocate_size);
  }
#ifdef DEBUG_MPS_SIZE
  {
    if ((((uintptr_t)obj)&ptag_mask)!=0) {
      printf("%s:%d The pointer at %p must be aligned to the Alignment() %lu ptag_mask=0x%zx ((uintptr_t)obj)&ptag_mask) = 0x%zx\n", __FILE__, __LINE__, (void*)obj, Alignment(), ptag_mask, (((uintptr_t)obj)&ptag_mask));
      abort();
    }
    if (AlignUp(allocate_size)!=allocate_size) {
      printf("%s:%d The allocate_size %lu must be a multiple of the Alignment() %lu\n", __FILE__, __LINE__, allocate_size, Alignment());
      abort();
    }
    mps_addr_t nextClient = weak_obj_skip((mps_addr_t)obj);
    int skip_size = (int)((char*)nextClient-(char*)obj);
    if (skip_size != allocate_size) {
      mps_addr_t againNextClient = weak_obj_skip_debug_wrong_size((mps_addr_t)obj,allocate_size,skip_size);
      printf("      header-size= %lu size= %zu\n", sizeof(HeadT), allocate_size );
    }
  }
#endif
  handle_all_queued_interrupts();
  DEBUG_MPS_UNDERSCANNING_TESTS();
  if (!obj)
    throw_hard_error("Could not allocate from GCBucketAllocator<Buckets<VT,VT,WeakLinks>>");
  GC_LOG(("malloc@%p %zu bytes\n", obj, allocate_size));
  return tagged_obj;
}
#endif // #ifdef USE_MPS

/*! Allocate regular C++ classes that are considered roots */
template <class T>
struct RootClassAllocator {
  template <class... ARGS>
  static gctools::tagged_pointer<T> allocate( ARGS &&... args) {
    return allocate_kind(Header_s::StampWtagMtag::make_Value<T>(),sizeof_with_header<T>(),std::forward<ARGS>(args)...);
  };

  template <class... ARGS>
  static gctools::tagged_pointer<T> allocate_kind(const Header_s::StampWtagMtag& the_header, size_t size, ARGS &&... args) {
#ifdef USE_BOEHM
    Header_s* base = do_boehm_uncollectable_allocation(the_header,size);
    T *obj = HeaderPtrToGeneralPtr<T>(base);
    new (obj) T(std::forward<ARGS>(args)...);
    handle_all_queued_interrupts();
    gctools::tagged_pointer<T> tagged_obj(obj);
    return tagged_obj;
#endif
#ifdef USE_MPS
    globalMpsMetrics.nonMovingAllocations++;
    tagged_pointer<T> tagged_obj =
        general_mps_allocation<tagged_pointer<T>>(the_header,
                                                  size,
                                                  my_thread_allocation_points._non_moving_allocation_point,
                                                  std::forward<ARGS>(args)...);
    return tagged_obj;
#endif
  }

  template <class... ARGS>
  static T *untagged_allocate(ARGS &&... args) {
    gctools::tagged_pointer<T> tagged_obj = allocate(args...);
    return &*tagged_obj;
  }

  static void deallocate(gctools::tagged_pointer<T> memory) {
#if defined(USE_BOEHM)
    GC_FREE(&*memory);
#endif
#if defined(USE_MPS) && !defined(RUNNING_MPSPREP)
    throw_hard_error("I need a way to deallocate MPS allocated objects that are not moveable or collectable");
    GCTOOLS_ASSERT(false); // ADD SOME WAY TO FREE THE MEMORY
#endif
  };

  static void untagged_deallocate(void *memory) {
#if defined(USE_BOEHM)
    GC_FREE(memory);
#endif
#ifdef USE_MPS
    GCTOOLS_ASSERT(false); // ADD SOME WAY TO FREE THE MEMORY
#endif
  };

  static void* allocateRootsAndZero(size_t num) {
#ifdef USE_BOEHM
    void* buffer = ALIGNED_GC_MALLOC_UNCOLLECTABLE(sizeof(void*)*num);
    memset( buffer, 0, sizeof(void*)*num);
#else
    void* buffer = NULL;
    printf("%s:%d:%s Add support\n", __FILE__, __LINE__, __FUNCTION__ );
#endif
    return buffer;
  }

  static void freeRoots(void* roots) {
#ifdef USE_BOEHM
    GC_FREE(roots);
#else
    printf("%s:%d:%s Add support\n", __FILE__, __LINE__, __FUNCTION__ );
#endif
  };
  
};


template <class Cons, class Register>
struct ConsAllocator {
  template <class... ARGS>
#ifdef ALWAYS_INLINE_MPS_ALLOCATIONS
  __attribute__((always_inline))
#else
  inline
#endif
  static smart_ptr<Cons> allocate(ARGS &&... args) {
#ifdef USE_BOEHM
    Cons* cons;
    size_t cons_size = ConsSizeCalculator<Cons,Register>::value();
    cons = do_boehm_cons_allocation<Cons,ARGS...>(cons_size,std::forward<ARGS>(args)...);
    handle_all_queued_interrupts();
    return smart_ptr<Cons>((Tagged)tag_cons(cons));
#endif
#ifdef USE_MPS
    mps_ap_t obj_ap = my_thread_allocation_points._cons_allocation_point;
        //        globalMpsMetrics.consAllocations++;
    smart_ptr<Cons> obj =
        do_cons_mps_allocation<Cons,Register>(obj_ap,"CONS",
                                              std::forward<ARGS>(args)...);
    return obj;
#endif
  }


#ifdef USE_PRECISE_GC
  static smart_ptr<Cons> snapshot_save_load_allocate(Header_s::StampWtagMtag the_header, core::T_sp car, core::T_sp cdr ) {
# ifdef USE_BOEHM
    Header_s* header = reinterpret_cast<Header_s*>(ALIGNED_GC_MALLOC_KIND(STAMP_UNSHIFT_MTAG(STAMPWTAG_CONS),SizeofConsHeader()+sizeof(Cons),global_cons_kind,&global_cons_kind));
    header->_stamp_wtag_mtag = the_header;
# else
    printf("%s:%d:%s add support for mps\n", __FILE__, __LINE__, __FUNCTION__ );
# endif
    Cons* cons = (Cons*)HeaderPtrToConsPtr(header);
    new (cons) Cons(car,cdr);
    return smart_ptr<Cons>((Tagged)tag_cons(cons));
  }
#endif
};
};

namespace gctools {
  template <class OT, GCInfo_policy Policy = normal>
    struct GCObjectAppropriatePoolAllocator {
      typedef OT value_type;
      typedef OT *pointer_type;
      typedef smart_ptr<OT> smart_pointer_type;
      template <typename... ARGS>
      static smart_pointer_type allocate_in_appropriate_pool_kind(const Header_s::StampWtagMtag& the_header, size_t size, ARGS &&... args) {
#ifdef USE_BOEHM
        Header_s* base = do_boehm_general_allocation(the_header,size);
        pointer_type ptr = HeaderPtrToGeneralPtr<OT>(base);
        new (ptr) OT(std::forward<ARGS>(args)...);
        smart_pointer_type sp = smart_ptr<value_type>(ptr);
        return sp;
#endif
#ifdef USE_MPS
        mps_ap_t obj_ap = my_thread_allocation_points._automatic_mostly_copying_allocation_point;
        globalMpsMetrics.movingAllocations++;
        smart_ptr<OT> sp =
          general_mps_allocation<smart_ptr<OT>>(the_header,size,obj_ap,
                                           std::forward<ARGS>(args)...);
        return sp;
#endif
      };

    static smart_pointer_type snapshot_save_load_allocate(snapshotSaveLoad::snapshot_save_load_init_s* snapshot_save_load_init) {
        size_t sizeWithHeader = sizeof(Header_s)+(snapshot_save_load_init->_clientEnd-snapshot_save_load_init->_clientStart);
#ifdef USE_BOEHM
        Header_s* base = do_boehm_general_allocation(snapshot_save_load_init->_headStart->_stamp_wtag_mtag,sizeWithHeader);
#ifdef DEBUG_GUARD
        // Copy the source from the image save/load memory.
        base->_source = snapshot_save_load_init->_headStart->_source;
#endif
        pointer_type ptr = HeaderPtrToGeneralPtr<OT>(base);
#ifdef DEBUG_GUARD        
        uintptr_t guardBefore0 = *(uintptr_t*)((uintptr_t*)ptr-1);
        uintptr_t guardAfter0 = *(uintptr_t*)((uintptr_t*)((char*)ptr+sizeWithHeader-sizeof(Header_s))+1);
#endif
        new (ptr) OT(snapshot_save_load_init);
#ifdef DEBUG_GUARD        
        uintptr_t guardBefore1 = *(uintptr_t*)((uintptr_t*)ptr-1);
        uintptr_t guardAfter1 = *(uintptr_t*)((uintptr_t*)((char*)ptr+sizeWithHeader-sizeof(Header_s))+1);
        if (guardBefore0!=guardBefore1) {
          printf("%s:%d:%s We stomped on the memory before the object\n", __FILE__, __LINE__, __FUNCTION__ );
        }
        if (guardAfter0!=guardAfter1) {
          printf("%s:%d:%s We stomped on the memory after the object\n", __FILE__, __LINE__, __FUNCTION__ );
        }
#endif
        smart_pointer_type sp = smart_ptr<value_type>(ptr);
        return sp;
#endif
#ifdef USE_MPS
        printf("%s:%d:%s add support for mps\n", __FILE__, __LINE__, __FUNCTION__ );
#endif
      };


    static void deallocate(OT* memory) {
      // Nothing needs to be done but this function needs to be here
      // so that the static analyzer has something to call
      };
    };

  template <class OT>
    struct GCObjectAppropriatePoolAllocator<OT, /* Policy= */ atomic> {
    typedef OT value_type;
    typedef OT *pointer_type;
    typedef smart_ptr<OT> smart_pointer_type;
    template <typename... ARGS>
      static smart_pointer_type allocate_in_appropriate_pool_kind( const Header_s::StampWtagMtag& the_header, size_t size, ARGS &&... args) {
#ifdef USE_BOEHM
    // Atomic objects (do not contain pointers) are allocated in separate pool
      Header_s* base = do_boehm_atomic_allocation(the_header,size);
      pointer_type ptr = HeaderPtrToGeneralPtr<OT>(base);
      new (ptr) OT(std::forward<ARGS>(args)...);
      smart_pointer_type sp = /*gctools::*/ smart_ptr<value_type>(ptr);
      return sp;
#endif
#ifdef USE_MPS
      mps_ap_t obj_ap = my_thread_allocation_points._automatic_mostly_copying_zero_rank_allocation_point;
      globalMpsMetrics.movingZeroRankAllocations++;
      smart_pointer_type sp =
        general_mps_allocation<smart_pointer_type>(the_header,size,obj_ap,
                                              std::forward<ARGS>(args)...);
      return sp;
#endif
    };
    static void deallocate(OT* memory) {
      // Nothing needs to be done but this function needs to be here
      // so that the static analyzer has something to call
    };

    static smart_pointer_type snapshot_save_load_allocate(snapshotSaveLoad::snapshot_save_load_init_s* snapshot_save_load_init, size_t size) {
#ifdef USE_BOEHM
      Header_s* base = do_boehm_atomic_allocation(snapshot_save_load_init->_headStart->_stamp_wtag_mtag,size);
#ifdef DEBUG_GUARD
        // Copy the source from the image save/load memory.
        base->_source = snapshot_save_load_init->_headStart->_source;
#endif
      pointer_type ptr = HeaderPtrToGeneralPtr<OT>(base);
      new (ptr) OT(snapshot_save_load_init);
      printf("%s:%d:%s This is where we should copy in the stuff from the snapshot_save_load_init object\n", __FILE__, __LINE__, __FUNCTION__ );
      smart_pointer_type sp = smart_ptr<value_type>(ptr);
      return sp;
#endif
#ifdef USE_MPS
      printf("%s:%d:%s add support for mps\n", __FILE__, __LINE__, __FUNCTION__ );
#endif
    };

  };

  /*! This Policy of collectible_immobile may not be a useful policy.
When would I ever want the GC to automatically collect objects but not move them?
*/
  template <class OT>
    struct GCObjectAppropriatePoolAllocator<OT,  /* Policy= */ collectable_immobile > {
    typedef OT value_type;
    typedef OT *pointer_type;
    typedef /*gctools::*/ smart_ptr<OT> smart_pointer_type;
    template <typename... ARGS>
    static smart_pointer_type allocate_in_appropriate_pool_kind( const Header_s::StampWtagMtag& the_header, size_t size, ARGS &&... args) {
#ifdef USE_BOEHM
      Header_s* base = do_boehm_general_allocation(the_header,size);
      pointer_type ptr = HeaderPtrToGeneralPtr<OT>(base);
      new (ptr) OT(std::forward<ARGS>(args)...);
      smart_pointer_type sp = /*gctools::*/ smart_ptr<value_type>(ptr);
      return sp;
#endif
#ifdef USE_MPS
      mps_ap_t obj_ap = my_thread_allocation_points._non_moving_allocation_point;
      globalMpsMetrics.nonMovingAllocations++;
      smart_pointer_type sp =
        general_mps_allocation<smart_pointer_type>(the_header,size,obj_ap,
                                              std::forward<ARGS>(args)...);
      return sp;
#endif
    };
    static void deallocate(OT* memory) {
      // Nothing needs to be done but this function needs to be here
      // so that the static analyzer has something to call
    };

    static smart_pointer_type snapshot_save_load_allocate(snapshotSaveLoad::snapshot_save_load_init_s* snapshot_save_load_init, size_t size) {
#ifdef USE_BOEHM
      Header_s* base = do_boehm_general_allocation(snapshot_save_load_init->_headStart->_stamp_wtag_mtag,size);
#ifdef DEBUG_GUARD
        // Copy the source from the image save/load memory.
        base->_source = snapshot_save_load_init->_headStart->_source;
#endif
      pointer_type ptr = HeaderPtrToGeneralPtr<OT>(base);
      new (ptr) OT(snapshot_save_load_init);
      printf("%s:%d:%s This is where we should copy in the stuff from the snapshot_save_load_init object\n", __FILE__, __LINE__, __FUNCTION__ );
      smart_pointer_type sp = smart_ptr<value_type>(ptr);
      return sp;
#endif
#ifdef USE_MPS
      printf("%s:%d:%s add support for mps\n", __FILE__, __LINE__, __FUNCTION__ );
#endif
    };

  };


  /*! This is for CL classes that derive from C++ classes and other CL classes that
should not be managed by the GC */
  template <class OT>
    struct GCObjectAppropriatePoolAllocator<OT, unmanaged > {
    typedef OT value_type;
    typedef OT *pointer_type;
    typedef /*gctools::*/ smart_ptr<OT> smart_pointer_type;
    template <typename... ARGS>
      static smart_pointer_type allocate_in_appropriate_pool_kind( const Header_s::StampWtagMtag& the_header, size_t size, ARGS &&... args) {
#ifdef USE_BOEHM
      Header_s* base = do_boehm_uncollectable_allocation(the_header,size);
      OT *obj = HeaderPtrToGeneralPtr<OT>(base);
      new (obj) OT(std::forward<ARGS>(args)...);
      handle_all_queued_interrupts();
      gctools::smart_ptr<OT> sp(obj);
      return sp;
#endif
#ifdef USE_MPS
      mps_ap_t obj_ap = my_thread_allocation_points._non_moving_allocation_point;
      globalMpsMetrics.nonMovingAllocations++;
      gctools::smart_ptr<OT> sp =
        general_mps_allocation<gctools::smart_ptr<OT>>(the_header,size,obj_ap,
                                                  std::forward<ARGS>(args)...);
      return sp;
#endif
    }

    static smart_pointer_type snapshot_save_load_allocate(snapshotSaveLoad::snapshot_save_load_init_s* snapshot_save_load_init, size_t size) {
#ifdef USE_BOEHM
      Header_s* base = do_boehm_uncollectable_allocation(snapshot_save_load_init->_headStart->_stamp_wtag_mtag,size);
#ifdef DEBUG_GUARD
        // Copy the source from the image save/load memory.
        base->_source = snapshot_save_load_init->_headStart->_source;
#endif
      pointer_type ptr = HeaderPtrToGeneralPtr<OT>(base);
      new (ptr) OT(snapshot_save_load_init);
      printf("%s:%d:%s This is where we should copy in the stuff from the snapshot_save_load_init object\n", __FILE__, __LINE__, __FUNCTION__ );
      smart_pointer_type sp = smart_ptr<value_type>(ptr);
      return sp;
#endif
#ifdef USE_MPS
      printf("%s:%d:%s add support for mps\n", __FILE__, __LINE__, __FUNCTION__ );
#endif
    };

    static void deallocate(OT* memory) {
#ifdef USE_BOEHM
      printf("%s:%d Using GC_FREE to free memory at@%p\n", __FILE__, __LINE__, memory );
      GC_FREE(memory);
#endif
#if defined(USE_MPS) && !defined(RUNNING_MPSPREP)
      throw_hard_error(" GCObjectAppropriatePoolAllocator<OT, unmanaged > I need a way to deallocate MPS allocated objects that are not moveable or collectable");
      GCTOOLS_ASSERT(false); // ADD SOME WAY TO FREE THE MEMORY
#endif
    };
  };
}

typedef void (*BoehmFinalizerFn)(void *obj, void *data);

extern "C" {
  void my_mps_finalize(core::T_O* tagged);
};

namespace gctools {
  extern void boehm_general_finalizer_from_BoehmFinalizer(void* client, void* dummy);
  
#ifdef USE_BOEHM
template <class OT>
void BoehmFinalizer(void *base, void *data) {
//  printf("%s:%d Finalizing base=%p\n", __FILE__, __LINE__, base);
  OT *client = HeaderPtrToGeneralPtr<OT>(base);
  boehm_general_finalizer_from_BoehmFinalizer((void*)client,data);
  client->~OT();
  GC_FREE(base);
}
#endif


template <class OT, bool Needed = true>
struct GCObjectFinalizer {
  typedef /*gctools::*/ smart_ptr<OT> smart_pointer_type;
  static void finalizeIfNeeded(smart_pointer_type sp) {
#ifdef USE_BOEHM
    void *dummyData;
    BoehmFinalizerFn dummyFn;
//    printf("%s:%d About to register finalize base -> %p\n", __FILE__, __LINE__, (void*)sp.tagged_());
    GC_register_finalizer_no_order(SmartPtrToBasePtr(sp),
                                   BoehmFinalizer<OT>, NULL,
                                   &dummyFn, &dummyData);
//    printf("%s:%d Finished finalize sp -> %p\n", __FILE__, __LINE__, (void*)sp.tagged_());
#endif
#ifdef USE_MPS
    // Defined in mpsGarbageCollection.cc
    my_mps_finalize(sp.raw_());
#endif
  };
};

template <class OT>
struct GCObjectFinalizer<OT, false> {
  typedef /*gctools::*/ smart_ptr<OT> smart_pointer_type;
  static void finalizeIfNeeded(smart_pointer_type sp){
      // finalize not needed
  };
};
}


namespace gctools {

  template <class OT>
    class GCObjectAllocator {
  public:
    typedef OT value_type;
    typedef OT *pointer_type;
    typedef /*gctools::*/ smart_ptr<OT> smart_pointer_type;
  public:
    template <typename... ARGS>
      static smart_pointer_type root_allocate(ARGS &&... args) {
      return root_allocate_kind(GCStamp<OT>::Stamp,sizeof_with_header<OT>(),std::forward<ARGS>(args)...);
    }
    template <typename... ARGS>
      static smart_pointer_type root_allocate_kind( const Header_s::StampWtagMtag& the_header, size_t size, ARGS &&... args) {
#ifdef USE_BOEHM
      Header_s* base = do_boehm_uncollectable_allocation(the_header,size);
      pointer_type ptr = HeaderPtrToGeneralPtr<OT>(base);
      new (ptr) OT(std::forward<ARGS>(args)...);
      smart_pointer_type sp = /*gctools::*/ smart_ptr<value_type>(ptr);
      GCObjectInitializer<OT, /*gctools::*/ GCInfo<OT>::NeedsInitialization>::initializeIfNeeded(sp);
      GCObjectFinalizer<OT, /*gctools::*/ GCInfo<OT>::NeedsFinalization>::finalizeIfNeeded(sp);
      return sp;
#endif
#ifdef USE_MPS
      smart_pointer_type sp = GCObjectAppropriatePoolAllocator<OT, GCInfo<OT>::Policy>::allocate_in_appropriate_pool_kind(the_header,size,std::forward<ARGS>(args)...);
      GCObjectInitializer<OT, GCInfo<OT>::NeedsInitialization>::initializeIfNeeded(sp);
      GCObjectFinalizer<OT, GCInfo<OT>::NeedsFinalization>::finalizeIfNeeded(sp);
      handle_all_queued_interrupts();
      return sp;
#endif
    };

    template <typename... ARGS>
    static smart_pointer_type allocate_kind_partial_scan(size_t scanSize, const Header_s::StampWtagMtag& the_header, size_t size, ARGS &&... args) {
      smart_pointer_type sp = GCObjectAppropriatePoolAllocator<OT, GCInfo<OT>::Policy>::allocate_in_appropriate_pool_kind_partial_scan(scanSize,the_header,size,std::forward<ARGS>(args)...);
      GCObjectInitializer<OT, /*gctools::*/ GCInfo<OT>::NeedsInitialization>::initializeIfNeeded(sp);
      GCObjectFinalizer<OT, /*gctools::*/ GCInfo<OT>::NeedsFinalization>::finalizeIfNeeded(sp);
    //            printf("%s:%d About to return allocate result ptr@%p\n", __FILE__, __LINE__, sp.px_ref());
      handle_all_queued_interrupts();
      return sp;
    };


    template <typename... ARGS>
      static smart_pointer_type allocate_kind(const Header_s::StampWtagMtag& the_header, size_t size, ARGS &&... args) {
      smart_pointer_type sp = GCObjectAppropriatePoolAllocator<OT, GCInfo<OT>::Policy>::allocate_in_appropriate_pool_kind(the_header,size,std::forward<ARGS>(args)...);
      GCObjectInitializer<OT, GCInfo<OT>::NeedsInitialization>::initializeIfNeeded(sp);
      GCObjectFinalizer<OT, GCInfo<OT>::NeedsFinalization>::finalizeIfNeeded(sp);
    //            printf("%s:%d About to return allocate result ptr@%p\n", __FILE__, __LINE__, sp.px_ref());
      handle_all_queued_interrupts();
      return sp;
    };

    static smart_pointer_type snapshot_save_load_allocate(snapshotSaveLoad::snapshot_save_load_init_s* snapshot_save_load_init ) {
      smart_pointer_type sp = GCObjectAppropriatePoolAllocator<OT, GCInfo<OT>::Policy>::snapshot_save_load_allocate( snapshot_save_load_init );
      // No initializer
//      GCObjectInitializer<OT, GCInfo<OT>::NeedsInitialization>::initializeIfNeeded(sp);
      GCObjectFinalizer<OT, GCInfo<OT>::NeedsFinalization>::finalizeIfNeeded(sp);
    //            printf("%s:%d About to return allocate result ptr@%p\n", __FILE__, __LINE__, sp.px_ref());
      return sp;
    };

    
    template <typename... ARGS>
    static smart_pointer_type static_allocate_kind(const Header_s::StampWtagMtag& the_header, size_t size, ARGS &&... args) {
      smart_pointer_type sp = GCObjectAppropriatePoolAllocator<OT, unmanaged>::allocate_in_appropriate_pool_kind(the_header,size,std::forward<ARGS>(args)...);
      GCObjectInitializer<OT, GCInfo<OT>::NeedsInitialization>::initializeIfNeeded(sp);
      GCObjectFinalizer<OT, GCInfo<OT>::NeedsFinalization>::finalizeIfNeeded(sp);
      handle_all_queued_interrupts();
      return sp;
    };




    static smart_pointer_type register_class_with_redeye() {
      throw_hard_error("Never call this - it's only used to register with the redeye static analyzer");
    }
    static smart_pointer_type copy_kind(const Header_s::StampWtagMtag& the_header, size_t size, const OT &that) {
#ifdef USE_BOEHM
    // Copied objects must be allocated in the appropriate pool
      smart_pointer_type sp = GCObjectAppropriatePoolAllocator<OT, GCInfo<OT>::Policy>::allocate_in_appropriate_pool_kind(  the_header, size, that);
    // Copied objects are not initialized.
    // Copied objects are finalized if necessary
      GCObjectFinalizer<OT, /*gctools::*/ GCInfo<OT>::NeedsFinalization>::finalizeIfNeeded(sp);
      return sp;
#endif
#ifdef USE_MPS
    // Copied objects must be allocated in the appropriate pool
      smart_pointer_type sp = GCObjectAppropriatePoolAllocator<OT, GCInfo<OT>::Policy>::allocate_in_appropriate_pool_kind(  the_header, size, that);
    // Copied objects are not initialized.
    // Copied objects are finalized if necessary
      GCObjectFinalizer<OT, GCInfo<OT>::NeedsFinalization>::finalizeIfNeeded(sp);
      return sp;
#endif
    }
  };
};


  
namespace gctools {
  template <class OT,bool Can>
  struct GCObjectDefaultConstructorAllocator {};

  template <class OT>
    struct GCObjectDefaultConstructorAllocator<OT,true> {
    static smart_ptr<OT> allocate(const Header_s::StampWtagMtag& kind) {
      // FIXSTAMP
      return GCObjectAllocator<OT>::allocate_kind(kind, sizeof_with_header<OT>());
    }
  };

  template <class OT>
    struct GCObjectDefaultConstructorAllocator<OT,false> {
    [[noreturn]] static smart_ptr<OT> allocate(const Header_s::StampWtagMtag& kind) {
      lisp_errorCannotAllocateInstanceWithMissingDefaultConstructor(OT::static_classSymbol());
    }
  };
};

namespace gctools {
  /*! This is the public interface to the GCObjectAllocator */
  template <class OT>
    class GC {
  public:
    typedef OT value_type;
    typedef OT *pointer_type;
    typedef /*gctools::*/ smart_ptr<OT> smart_pointer_type;
  public:
    template <typename... ARGS>
      static smart_pointer_type root_allocate(ARGS &&... args) {
      return GCObjectAllocator<OT>::root_allocate_kind(Header_s::StampWtagMtag::make_Value<OT>(),sizeof_with_header<OT>(),std::forward<ARGS>(args)...);
    }

    template <typename... ARGS>
      static smart_pointer_type root_allocate_with_stamp(ARGS &&... args) {
      return GCObjectAllocator<OT>::root_allocate_kind(Header_s::StampWtagMtag::make_Value<OT>(),sizeof_with_header<OT>(),std::forward<ARGS>(args)...);
    }

    template <typename... ARGS>
      static smart_pointer_type never_invoke_allocator( ARGS &&... args) {
      auto kind = GCStamp<OT>::Stamp;
      return GCObjectAllocator<OT>::allocate_kind(kind,0, std::forward<ARGS>(args)...);
    }

    template <typename... ARGS>
      static smart_pointer_type allocate_kind( const Header_s::StampWtagMtag& kind, ARGS &&... args) {
      size_t size = sizeof_with_header<OT>();
      return GCObjectAllocator<OT>::allocate_kind(kind,size, std::forward<ARGS>(args)...);
    }

    template <typename... ARGS>
      static smart_pointer_type allocate_instance(const Header_s::StampWtagMtag& kind, size_t size, ARGS &&... args) {
      return GCObjectAllocator<OT>::allocate_kind(kind,size, std::forward<ARGS>(args)...);
    }

    template <typename... ARGS>
      static smart_pointer_type allocate( ARGS &&... args) {
      auto kind = Header_s::StampWtagMtag::make_StampWtagMtag(OT::static_ValueStampWtagMtag);
      size_t size = sizeof_with_header<OT>();
      return GCObjectAllocator<OT>::allocate_kind(kind,size, std::forward<ARGS>(args)...);
    }

    static smart_pointer_type allocate_with_default_constructor() {
      return GCObjectDefaultConstructorAllocator<OT,std::is_default_constructible<OT>::value>::allocate(Header_s::StampWtagMtag::make_StampWtagMtag(OT::static_ValueStampWtagMtag));
    }

    /*! Allocate enough space for capacity elements, but set the length to length */

    // Allocates an object with proper header and everything.
    // Uses the underlying constructor. Like, GC<SimpleVector_O>::allocate_container(...)
    // ends up passing the ... to the SimpleVector_O constructor.
    template <typename... ARGS>
    static smart_pointer_type allocate_container( bool static_container_p, int64_t length, ARGS &&... args) {
      size_t capacity = std::abs(length);
      size_t size = sizeof_container_with_header<OT>(capacity);
      if (static_container_p) return GCObjectAllocator<OT>::static_allocate_kind(Header_s::StampWtagMtag::make_StampWtagMtag(OT::static_ValueStampWtagMtag),size,length,std::forward<ARGS>(args)...);
      return GCObjectAllocator<OT>::allocate_kind(Header_s::StampWtagMtag(OT::static_ValueStampWtagMtag),size,length,std::forward<ARGS>(args)...);
    }


    template <typename... ARGS>
    static smart_pointer_type allocate_container_null_terminated_string( bool static_container_p,
                                                                         size_t length, ARGS &&... args) {
      size_t capacity = length+1;
      size_t size = sizeof_container_with_header<OT>(capacity);
      if (static_container_p)
        return GCObjectAllocator<OT>::static_allocate_kind(Header_s::StampWtagMtag::make_StampWtagMtag(OT::static_ValueStampWtagMtag), size, length,
                                                           std::forward<ARGS>(args)...);
      else
        return GCObjectAllocator<OT>::allocate_kind(Header_s::StampWtagMtag::make_StampWtagMtag(OT::static_ValueStampWtagMtag), size, length,
                                                    std::forward<ARGS>(args)...);
    }


            /*! Allocate enough space for capacity elements, but set the length to length */

    // Allocates an object with proper header and everything.
    // Uses the underlying constructor. Like, GC<SimpleVector_O>::allocate_container(...)
    // ends up passing the ... to the SimpleVector_O constructor.
    template <typename... ARGS>
    static smart_pointer_type allocate_container_partial_scan(size_t dataScanSize, int64_t length, ARGS &&... args) {
      size_t capacity = std::abs(length);
      size_t size = sizeof_container_with_header<OT>(capacity);
      size_t scanSize = sizeof_container_with_header<OT>(dataScanSize);
      return GCObjectAllocator<OT>::allocate_kind_partial_scan(scanSize,Header_s::StampWtagMtag::make_StampWtagMtag(OT::static_ValueStampWtagMtag),size,length,std::forward<ARGS>(args)...);
    }


    
    template <typename... ARGS>
    static smart_pointer_type allocate_bitunit_container(bool static_container_p,
                                                         size_t length, ARGS &&... args) {
      size_t size = sizeof_bitunit_container_with_header<OT>(length);
#ifdef DEBUG_BITUNIT_CONTAINER
      printf("%s:%d  In allocate_bitunit_container length = %lu  size= %lu\n", __FILE__, __LINE__, length, size );
#endif
      smart_pointer_type result;
      if (static_container_p)
        result = GCObjectAllocator<OT>::static_allocate_kind(Header_s::StampWtagMtag::make_Value<OT>(),size,length,
                                                             std::forward<ARGS>(args)...);
      else
        result = GCObjectAllocator<OT>::allocate_kind(Header_s::StampWtagMtag::make_Value<OT>(),size,length,
                                                      std::forward<ARGS>(args)...);
#if DEBUG_BITUNIT_CONTAINER
      {
        printf("%s:%d allocate_bitunit_container \n", __FILE__, __LINE__ );
        printf("            Allocated object tagged ptr = %p\n", (void*)result.raw_());
      }
#endif

      return result;
    }
    
    static smart_pointer_type copy(const OT &that) {
      return GCObjectAllocator<OT>::copy_kind(Header_s::StampWtagMtag::make_Value<OT>(),sizeof_with_header<OT>(),that);
    }

    static void deallocate_unmanaged_instance(OT* obj) {
      GCObjectAppropriatePoolAllocator<OT, GCInfo<OT>::Policy>::deallocate(obj);
    }
  };
};

namespace gctools {
template <class TY>
class GCContainerAllocator /* : public GCAlloc<TY> */ {
public:
  // type definitions
  typedef TY container_type;
  typedef container_type *container_pointer;
  typedef typename container_type::value_type value_type;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;
  typedef value_type &reference;
  typedef const value_type &const_reference;
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;

  /* constructors and destructor
         * - nothing to do because the allocator has no state
         */
  GCContainerAllocator() throw() {}
  GCContainerAllocator(const GCContainerAllocator &) throw() {}
  template <class U>
  GCContainerAllocator(const GCContainerAllocator<U> &) throw() {}
  ~GCContainerAllocator() throw() {}

  // return maximum number of elements that can be allocated
  size_type max_size() const throw() {
    return std::numeric_limits<std::size_t>::max() / sizeof(value_type);
  }

    // allocate but don't initialize num elements of type value_type
  gc::tagged_pointer<container_type> allocate(size_type num, const void * = 0) {
    return allocate_kind(Header_s::StampWtagMtag::make_Value<TY>(),num);
  }

  // allocate but don't initialize num elements of type value_type
  gc::tagged_pointer<container_type> allocate_kind(const Header_s::StampWtagMtag& the_header, size_type num, const void * = 0) {
#ifdef USE_BOEHM
    size_t size = sizeof_container_with_header<TY>(num);
    Header_s* base = do_boehm_general_allocation(the_header,size);
    container_pointer myAddress = HeaderPtrToGeneralPtr<TY>(base);
    handle_all_queued_interrupts();
    return gctools::tagged_pointer<container_type>(myAddress);
#endif
#ifdef USE_MPS
    size_t size = sizeof_container_with_header<TY>(num);
    mps_ap_t obj_ap = my_thread_allocation_points._automatic_mostly_copying_allocation_point;
    globalMpsMetrics.movingAllocations++;
    gc::tagged_pointer<container_type> obj =
      general_mps_allocation<gc::tagged_pointer<container_type>>(the_header,
                                                            size,obj_ap,
                                                            num);
    return obj;
#endif
  }

  // initialize elements of allocated storage p with value value
  template <typename... ARGS>
  void construct(pointer p, ARGS &&... args) {
    // initialize memory with placement new
    new ((void *)p) value_type(std::forward<ARGS>(args)...);
  }

  // destroy elements of initialized storage p
  void destroy(pointer p) {
    // Do nothing
  }

  // deallocate storage p of deleted elements
  void deallocate(gctools::tagged_pointer<container_type> p, size_type num) {
    // Do nothing
  }
};
};



namespace gctools {
template <class TY>
class GCAbstractAllocator /* : public GCAlloc<TY> */ {
public:
  // type definitions
  typedef TY container_type;
  typedef container_type *container_pointer;
  typedef typename container_type::value_type value_type;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;
  typedef value_type &reference;
  typedef const value_type &const_reference;
  typedef std::size_t size_type;
  /* constructors and destructor
         * - nothing to do because the allocator has no state
         */
  GCAbstractAllocator() throw() {}
  ~GCAbstractAllocator() throw() {}

    // allocate but don't initialize num elements of type value_type
  void never_invoke_allocate() {};
};
};






namespace gctools {
/*! This allocator is for allocating containers that are fixed in position and Capacity.
      Things like the MultipleValues for multiple value return are allocated with this.
      */

template <class TY>
class GCContainerNonMoveableAllocator /* : public GCAlloc<TY> */ {
public:
  // type definitions
  typedef TY container_type;
  typedef container_type *container_pointer;
  typedef typename container_type::value_type value_type;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;
  typedef value_type &reference;
  typedef const value_type &const_reference;
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;

  /* constructors and destructor
         * - nothing to do because the allocator has no state
         */
  GCContainerNonMoveableAllocator() throw() {}
  GCContainerNonMoveableAllocator(const GCContainerNonMoveableAllocator &) throw() {}
  template <class U>
  GCContainerNonMoveableAllocator(const GCContainerNonMoveableAllocator<U> &) throw() {}
  ~GCContainerNonMoveableAllocator() throw() {}

  // return maximum number of elements that can be allocated
  size_type max_size() const throw() {
    return std::numeric_limits<std::size_t>::max() / sizeof(value_type);
  }

  // allocate but don't initialize num elements of type value_type
  gctools::tagged_pointer<container_type> allocate_kind( const Header_s::StampWtagMtag& the_header, size_type num, const void * = 0) {
#ifdef USE_BOEHM
    size_t size = sizeof_container_with_header<TY>(num);
    // prepend a one pointer header with a pointer to the typeinfo.name
    Header_s* base = do_boehm_general_allocation(the_header,size);
    container_pointer myAddress = HeaderPtrToGeneralPtr<TY>(base);
    handle_all_queued_interrupts();
    return myAddress;
#endif
#ifdef USE_MPS
    size_t size = sizeof_container_with_header<TY>(num);
    mps_ap_t obj_ap = my_thread_allocation_points._non_moving_allocation_point;
    globalMpsMetrics.nonMovingAllocations++;
    gctools::tagged_pointer<container_type> obj =
      general_mps_allocation<gc::tagged_pointer<container_type>>(the_header,size,obj_ap,num);
    return obj;
#endif
  }

  // initialize elements of allocated storage p with value value
  template <typename... ARGS>
  void construct(pointer p, ARGS &&... args) {
    // initialize memory with placement new
    new ((void *)p) value_type(std::forward<ARGS>(args)...);
  }

  // destroy elements of initialized storage p
  void destroy(pointer p) {
    // Do nothing
  }

  // deallocate storage p of deleted elements
  void deallocate(gctools::tagged_pointer<container_type> p, size_type num) {
    // Do nothing
  }
};
};



namespace gctools {

#ifdef USE_BOEHM
inline void BoehmWeakLinkDebugFinalizer(void *base, void *data) {
  printf("%s:%d Boehm finalized weak linked address %p at %p\n", __FILE__, __LINE__, base, data);
}
#endif

struct WeakLinks {};
struct StrongLinks {};

template <class KT, class VT, class LT>
struct Buckets;




#ifdef USE_MPS
 //
 // Allocation point for weak links is different from strong links
 //
template <typename StrongWeakType=StrongLinks>
struct StrongWeakAllocationPoint {
  static mps_ap_t& get() { return my_thread_allocation_points._strong_link_allocation_point; };
  static const char* name() { return "strong_links"; };
};

template <>
struct StrongWeakAllocationPoint<WeakLinks> {
  static mps_ap_t& get() { return my_thread_allocation_points._weak_link_allocation_point; };
  static const char* name() { return "weak_links"; };
};

#endif


 template <class VT>
   class GCBucketAllocator{};
 
 
 template <class VT,class StrongWeakLinkType>
   class GCBucketAllocator<Buckets<VT, VT, StrongWeakLinkType>> {
 public:
  typedef Buckets<VT, VT, StrongWeakLinkType> TY;
  typedef TY container_type;
  typedef container_type *container_pointer;
  typedef typename container_type::value_type value_type;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;
  typedef value_type &reference;
  typedef const value_type &const_reference;
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;

  /* constructors and destructor
         * - nothing to do because the allocator has no state
         */
  GCBucketAllocator() throw() {}
  GCBucketAllocator(const GCBucketAllocator &) throw() {}
  ~GCBucketAllocator() throw() {}

  // return maximum number of elements that can be allocated
  size_type max_size() const throw() {
    return std::numeric_limits<std::size_t>::max() / sizeof(value_type);
  }

  // allocate but don't initialize num elements of type value_type
  static gctools::tagged_pointer<container_type> allocate(Header_s::StampWtagMtag the_header, size_type num, const void * = 0) {
    size_t size = sizeof_container_with_header<container_type>(num);
#ifdef USE_BOEHM
    Header_s* base = do_boehm_weak_allocation(the_header,size);
    container_pointer myAddress = (container_pointer)HeaderPtrToWeakPtr(base);
    if (!myAddress) throw_hard_error("Out of memory in allocate");
    new (myAddress) container_type(num);
#ifdef DEBUG_GCWEAK
    printf("%s:%d Check if Buckets has been initialized to unbound\n", __FILE__, __LINE__);
#endif
    return gctools::tagged_pointer<container_type>(myAddress);
#endif
#ifdef USE_MPS
    printf("%s:%d:%s Handle allocation\n",__FILE__, __LINE__, __FUNCTION__);
    mps_addr_t addr;
    container_pointer myAddress(NULL);
    printf("%s:%d:%s Handle weak object allocation properly - I added normal headers\n", __FILE__, __LINE__, __FUNCTION__ );
    gctools::tagged_pointer<container_type> obj =
      do_mps_weak_allocation<gctools::tagged_pointer<container_type>>(size,StrongWeakAllocationPoint<StrongWeakLinkType>::get(),StrongWeakAllocationPoint<StrongWeakLinkType>::name(),num);
    return obj;
#endif
  }

   static gctools::tagged_pointer<container_type> snapshot_save_load_allocate( snapshotSaveLoad::snapshot_save_load_init_s* init ) {
    size_t size = (init->_clientEnd-init->_clientStart)+SizeofWeakHeader();
#ifdef USE_BOEHM
    Header_s* base = do_boehm_weak_allocation(init->_headStart->_stamp_wtag_mtag, size);
#ifdef DEBUG_GUARD
        // Copy the source from the image save/load memory.
        base->_source = init->_headStart->_source;
#endif
    container_pointer myAddress = (container_pointer)HeaderPtrToWeakPtr(base);
    new (myAddress) container_type(init);
    return gctools::tagged_pointer<container_type>(myAddress);
#endif
#ifdef USE_MPS
    printf("%s:%d:%s Handle allocation in MPS\n");
    mps_addr_t addr;
    container_pointer myAddress(NULL);
    printf("%s:%d:%s Handle weak object allocation properly - I added normal headers\n", __FILE__, __LINE__, __FUNCTION__ );
    gctools::tagged_pointer<container_type> obj =
      do_mps_weak_allocation<gctools::tagged_pointer<container_type>>(size,StrongWeakAllocationPoint<StrongWeakLinkType>::get(),StrongWeakAllocationPoint<StrongWeakLinkType>::name());
    return obj;
#endif
  }

  // initialize elements of allocated storage p with value value
  template <typename... ARGS>
  void construct(pointer p, ARGS &&... args) {
    // initialize memory with placement new
    throw_hard_error("What do I do here");
    //            new((void*)p)value_type(std::forward<ARGS>(args)...);
  }

  // destroy elements of initialized storage p
  void destroy(pointer p) {
    // Do nothing
  }

  // deallocate storage p of deleted elements
  void deallocate(gctools::tagged_pointer<container_type> p, size_type num) {
    // Do nothing
  }
};

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ----------------------------------------------------------------------

template <class KT, class VT, class LT>
struct Mapping;

template <class TY>
class GCMappingAllocator /* : public GCAlloc<TY> */ {};


 template <class VT, class StrongWeakLinkType>
class GCMappingAllocator<Mapping<VT, VT, StrongWeakLinkType>> {
public:
  typedef Mapping<VT, VT, StrongWeakLinkType> TY;
  typedef TY container_type;
  typedef TY *container_pointer;
  /* constructors and destructor
         * - nothing to do because the allocator has no state
         */
  GCMappingAllocator() throw() {}
  GCMappingAllocator(const GCMappingAllocator &) throw() {}
  ~GCMappingAllocator() throw() {}

  // allocate but don't initialize num elements of type value_type
  static gctools::tagged_pointer<container_type> allocate(Header_s::StampWtagMtag the_header, const VT &val) {
    size_t size = sizeof_with_header<container_type>();
#ifdef USE_BOEHM
    Header_s* base = do_boehm_weak_allocation(the_header,size);
    container_pointer myAddress = (container_pointer)HeaderPtrToWeakPtr(base);
    if (!myAddress) throw_hard_error("Out of memory in allocate");
    new (myAddress) container_type(val);
    printf("%s:%d Check if Mapping has been initialized to unbound\n", __FILE__, __LINE__);
    return gctools::tagged_pointer<container_type>(myAddress);
#endif
#ifdef USE_MPS
    typedef typename GCHeader<TY>::HeaderType HeadT;
    mps_addr_t addr;
    container_pointer myAddress(NULL);
    printf("%s:%d:%s Handle weak object allocation properly - I added normal headers\n", __FILE__, __LINE__, __FUNCTION__ );
    gctools::tagged_pointer<container_type> obj =
      do_mps_weak_allocation<gctools::tagged_pointer<container_type>>(size,StrongWeakAllocationPoint<StrongWeakLinkType>::get(),StrongWeakAllocationPoint<StrongWeakLinkType>::name());
    return obj;
#endif
  }
};



 
template <class VT>
class GCWeakPointerAllocator {
public:
  typedef VT value_type;
  typedef value_type *value_pointer;
  typedef typename VT::value_type contained_type;
  /* constructors and destructor
         * - nothing to do because the allocator has no state
         */
  GCWeakPointerAllocator() throw() {}
  GCWeakPointerAllocator(const GCWeakPointerAllocator &) throw() {}
  ~GCWeakPointerAllocator() throw() {}

  // allocate but don't initialize num elements of type value_type
  static gctools::tagged_pointer<value_type> allocate(Header_s::StampWtagMtag the_header, const contained_type &val) {
    size_t size = sizeof_with_header<VT>();
#ifdef USE_BOEHM
#ifdef DEBUG_GCWEAK
    printf("%s:%d Allocating WeakPointer with GC_MALLOC_ATOMIC\n", __FILE__, __LINE__);
#endif
    Header_s* base = do_boehm_weak_allocation(the_header,size);
    VT* myAddress = (VT*)HeaderPtrToWeakPtr(base);
    if (!myAddress) throw_hard_error("Out of memory in allocate");
    new (myAddress) VT(val);
    return gctools::tagged_pointer<value_type>(myAddress);
#endif
#ifdef USE_MPS
    mps_addr_t addr;
    value_pointer myAddress;
    gctools::tagged_pointer<value_type> obj =
      do_mps_weak_allocation<gctools::tagged_pointer<value_type>>(size,my_thread_allocation_points._weak_link_allocation_point,"weak_link3_Allocator",val);
    return obj;
#endif
  }
};
};


namespace gctools {

#if 0
/*! Maintain a stack containing pointers that are garbage collected
*/
class GCStack {
public:
  typedef enum { undefined_t,
                 frame_t,
                 pad_t } frameType;
  size_t _MaxSize;
  size_t _TotalSize;
  size_t _TotalAllocations;
#ifdef USE_BOEHM
#ifdef BOEHM_ONE_BIG_STACK
  uintptr_t *_StackCur;
  uintptr_t *_StackBottom;
  size_t _StackMinOffset;
  size_t _StackMiddleOffset;
  uintptr_t *_StackLimit;
#else
// Nothing
#endif
#endif
#ifdef USE_MPS
  mps_pool_t _Pool;
  mps_ap_t _AllocationPoint;
  mps_fmt_t _ObjectFormat;
  bool _IsActive;
  vector<mps_frame_t> frames;
#endif
  //! Return true if this Stack object is active and can receive pushFrame/popFrame messages
public:
  size_t maxSize() const { return this->_MaxSize; };
  bool isActive() {
#ifdef USE_BOEHM
    return true;
#endif
#ifdef USE_MPS
    return _IsActive;
#endif
  };
#ifdef BOEHM_ONE_BIG_STACK
  void growStack();
  void shrinkStack();
#endif
  //*! Allocate a buffer for this
  bool allocateStack(size_t bufferSize) {
    bufferSize = STACK_ALIGN_UP(bufferSize);
#ifdef USE_BOEHM
#ifdef BOEHM_ONE_BIG_STACK
    DEPRECATED();
    this->_StackBottom = (uintptr_t *)ALIGNED_GC_MALLOC(bufferSize);
    this->_StackMiddleOffset = (bufferSize / 2);
    this->_StackLimit = (uintptr_t *)((char *)this->_StackBottom + bufferSize);
    this->_StackMinOffset = bufferSize;
    this->_StackCur = this->_StackBottom;
    memset(this->_StackBottom, 0, bufferSize);
#else
// Do nothing
#endif
#endif
    return true;
  };
  void deallocateStack() {
#ifdef USE_BOEHM
#ifdef BOEHM_ONE_BIG_STACK
    if (this->_StackCur != this->_StackBottom) {
      throw_hard_error("The stack is not empty");
    }
    GC_FREE(this->_StackBottom);
#else
// Do nothing
#endif
#endif
  };

  size_t totalSize() const {
    return this->_TotalSize;
  }

  size_t totalAllocations() const {
    return this->_TotalAllocations;
  }

#define FRAME_HEADER_SIZE (sizeof(int) * 2)
#define FRAME_HEADER_TYPE_FIELD(hptr) *(((int *)hptr))
#define FRAME_HEADER_SIZE_FIELD(hptr) *(((int *)hptr) + 1)
#define FRAME_START(hptr) (uintptr_t *)(((char *)hptr) + FRAME_HEADER_SIZE)
#define FRAME_HEADER(fptr) (uintptr_t *)(((char *)fptr) - FRAME_HEADER_SIZE)
  void *frameImplHeaderAddress(void *frameImpl) {
    return (void *)((char *)frameImpl - FRAME_HEADER_SIZE);
  }
  GCStack::frameType frameImplHeaderType(void *frameImpl) {
    void *frameImplHeader = (void *)((char *)frameImpl - FRAME_HEADER_SIZE);
    return (GCStack::frameType)(FRAME_HEADER_TYPE_FIELD(frameImplHeader));
  }
  int frameImplHeaderSize(void *frameImpl) {
    void *frameImplHeader = (void *)((char *)frameImpl - FRAME_HEADER_SIZE);
    return (int)(FRAME_HEADER_SIZE_FIELD(frameImplHeader));
  }

  int frameImplBodySize(void *frameImpl) {
    void *frameImplHeader = (void *)((char *)frameImpl - FRAME_HEADER_SIZE);
    return (int)(FRAME_HEADER_SIZE_FIELD(frameImplHeader) - FRAME_HEADER_SIZE);
  }

  void *pushFrameImpl(size_t frameSize);
  void popFrameImpl(void *frameImpl) {
#ifdef USE_BOEHM
    uintptr_t *frameHeaderP = reinterpret_cast<uintptr_t *>(frameImpl) - 1;
    uintptr_t headerAndFrameSize = FRAME_HEADER_SIZE_FIELD(frameHeaderP);
    this->_TotalSize = this->_TotalSize - headerAndFrameSize;
#ifdef BOEHM_ONE_BIG_STACK
    memset(frameHeaderP, 0, headerAndFrameSize);
    this->_StackCur = frameHeaderP;
    if (this->_StackMinOffset <= (this->_StackLimit - this->_StackBottom) && (this->_StackCur - this->_StackBottom) < this->_StackMiddleOffset)
      this->shrinkStack();
#ifdef DEBUG_BOEHM_STACK
    size_t calcSize = (char *)this->_StackTop - (char *)this->_StackBottom;
    if (calcSize != this->_TotalSize) {
      throw_hard_error_side_stack_damaged(this->_TotalSize,calcSize);
    }
    for (char *i = (char *)this->_StackTop; i < (char *)this->_StackLimit; ++i) {
      if (*i) {
        throw_hard_error("The side-stack has garbage in it!");
      }
    }
#endif
#else
    GC_FREE(frameHeaderP);
#endif
#endif // USE_BOEHM
#ifdef USE_MPS
    uintptr_t *frameHeaderP = FRAME_HEADER(frameImpl);
    uintptr_t headerAndFrameSize = FRAME_HEADER_SIZE_FIELD(frameHeaderP);
    this->_TotalSize -= headerAndFrameSize;
    mps_frame_t frame_o = this->frames.back();
    this->frames.pop_back();
    mps_res_t res = mps_ap_frame_pop(this->_AllocationPoint, frame_o);
    if (res != MPS_RES_OK) {
      throw_hard_error_mps_bad_result(res);
    }
#endif // USE_MPS
  }

 GCStack() : _TotalSize(0), _TotalAllocations(0), _MaxSize(0)
#ifdef USE_BOEHM
#endif
#ifdef USE_MPS
// What do I do here?
#endif
              {};
  virtual ~GCStack(){
#ifdef USE_BOEHM
// Nothing to do
#endif
#ifdef USE_MPS
// What do I do here?
#endif
  };
};
#endif
 
};

#endif // USE_BOEHM || USE_MPS

#endif
