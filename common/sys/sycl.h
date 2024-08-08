// Copyright 2009-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "platform.h"

using sycl::float16;
using sycl::float8;
using sycl::float4;
using sycl::float3;
using sycl::float2;
using sycl::int16;
using sycl::int8;
using sycl::int4;
using sycl::int3;
using sycl::int2;
using sycl::uint16;
using sycl::uint8;
using sycl::uint4;
using sycl::uint3;
using sycl::uint2;
using sycl::uchar16;
using sycl::uchar8;
using sycl::uchar4;
using sycl::uchar3;
using sycl::uchar2;
using sycl::ushort16;
using sycl::ushort8;
using sycl::ushort4;
using sycl::ushort3;
using sycl::ushort2;

#ifdef __SYCL_DEVICE_ONLY__
#define GLOBAL __attribute__((opencl_global))
#define LOCAL  __attribute__((opencl_local))

SYCL_EXTERNAL extern int   work_group_reduce_add(int x);
SYCL_EXTERNAL extern float work_group_reduce_min(float x);
SYCL_EXTERNAL extern float work_group_reduce_max(float x);

SYCL_EXTERNAL extern float atomic_min(volatile GLOBAL float *p, float val);
SYCL_EXTERNAL extern float atomic_min(volatile LOCAL  float *p, float val);
SYCL_EXTERNAL extern float atomic_max(volatile GLOBAL float *p, float val);
SYCL_EXTERNAL extern float atomic_max(volatile LOCAL  float *p, float val);

SYCL_EXTERNAL extern "C" unsigned int intel_sub_group_ballot(bool valid);

SYCL_EXTERNAL extern "C" void __builtin_IB_assume_uniform(void *p);

#else

#define GLOBAL 
#define LOCAL 

/* dummy functions for host */
inline int   work_group_reduce_add(int x) { return x; }
inline float work_group_reduce_min(float x) { return x; }
inline float work_group_reduce_max(float x) { return x; }

inline float atomic_min(volatile float *p, float val) { return val; };
inline float atomic_max(volatile float *p, float val) { return val; };

inline uint32_t intel_sub_group_ballot(bool valid) { return 0; }

#endif

/* creates a temporary that is enforced to be uniform */
#define SYCL_UNIFORM_VAR(Ty,tmp,k)					\
  Ty tmp##_data;							\
  Ty* p##tmp##_data = (Ty*) sub_group_broadcast((uint64_t)&tmp##_data,k);	\
  Ty& tmp = *p##tmp##_data;

#if !defined(__forceinline)
#define __forceinline          inline __attribute__((always_inline))
#endif

#if __SYCL_COMPILER_VERSION < 20210801
#define all_of_group all_of
#define any_of_group any_of
#define none_of_group none_of
#define group_broadcast broadcast
#define reduce_over_group reduce
#define exclusive_scan_over_group exclusive_scan
#define inclusive_scan_over_group inclusive_scan
#endif

namespace embree
{
  template<typename T>
  __forceinline T cselect(const bool mask, const T &a, const T &b)
  {
    return sycl::select(b,a,(int)mask);
  }
  
  template<typename T, typename M>
  __forceinline T cselect(const M &mask, const T &a, const T &b)
  {
    return sycl::select(b,a,mask);
  }
  
#define XSTR(x) STR(x)
#define STR(x) #x

  __forceinline const sycl::sub_group this_sub_group() {
#if __LIBSYCL_MAJOR_VERSION >= 8
    return sycl::ext::oneapi::this_work_item::get_sub_group();
#else
    return sycl::ext::oneapi::experimental::this_sub_group();
#endif
  }
  
  __forceinline const uint32_t get_sub_group_local_id() {
    return this_sub_group().get_local_id()[0];
  }

  __forceinline const uint32_t get_sub_group_size() {
    return this_sub_group().get_max_local_range().size();
  }

  __forceinline const uint32_t get_sub_group_id() {
    return this_sub_group().get_group_id()[0];
  }
  
  __forceinline const uint32_t get_num_sub_groups() {
    return this_sub_group().get_group_range().size();
  }
  
  __forceinline uint32_t sub_group_ballot(bool pred) {
    return intel_sub_group_ballot(pred);
  }

  __forceinline bool sub_group_all_of(bool pred) {
    return sycl::all_of_group(this_sub_group(),pred);
  }

  __forceinline bool sub_group_any_of(bool pred) {
    return sycl::any_of_group(this_sub_group(),pred);
  }
  
  __forceinline bool sub_group_none_of(bool pred) {
    return sycl::none_of_group(this_sub_group(),pred);
  }

  template <typename T> __forceinline T sub_group_broadcast(T x, sycl::id<1> local_id) {
    return sycl::group_broadcast<sycl::sub_group>(this_sub_group(),x,local_id);
  }
  
  template <typename T> __forceinline T sub_group_make_uniform(T x) {
    return sub_group_broadcast(x,sycl::ctz(intel_sub_group_ballot(true)));
  }

  __forceinline void assume_uniform_array(void* ptr) {
#ifdef __SYCL_DEVICE_ONLY__
    __builtin_IB_assume_uniform(ptr);
#endif
  }

  template <typename T, class BinaryOperation> __forceinline T sub_group_reduce(T x, BinaryOperation binary_op) {
    return sycl::reduce_over_group<sycl::sub_group>(this_sub_group(),x,binary_op);
  }

  template <typename T, class BinaryOperation> __forceinline T sub_group_reduce(T x, T init, BinaryOperation binary_op) {
    return sycl::reduce_over_group<sycl::sub_group>(this_sub_group(),x,init,binary_op);
  }
  
  template <typename T> __forceinline T sub_group_reduce_min(T x, T init) {
    return sub_group_reduce(x, init, sycl::ext::oneapi::minimum<T>());
  }

  template <typename T> __forceinline T sub_group_reduce_min(T x) {
    return sub_group_reduce(x, sycl::ext::oneapi::minimum<T>());
  }

  template <typename T> __forceinline T sub_group_reduce_max(T x) {
    return sub_group_reduce(x, sycl::ext::oneapi::maximum<T>());
  }
  
  template <typename T> __forceinline T sub_group_reduce_add(T x) {
    return sub_group_reduce(x, sycl::ext::oneapi::plus<T>());
  }

  template <typename T, class BinaryOperation> __forceinline T sub_group_exclusive_scan(T x, BinaryOperation binary_op) {
    return sycl::exclusive_scan_over_group(this_sub_group(),x,binary_op);
  }

  template <typename T, class BinaryOperation> __forceinline T sub_group_exclusive_scan_min(T x) {
    return sub_group_exclusive_scan(x,sycl::ext::oneapi::minimum<T>());
  }

  template <typename T, class BinaryOperation> __forceinline T sub_group_exclusive_scan(T x, T init, BinaryOperation binary_op) {
    return sycl::exclusive_scan_over_group(this_sub_group(),x,init,binary_op);
  }

  template <typename T, class BinaryOperation> __forceinline T sub_group_inclusive_scan(T x, BinaryOperation binary_op) {
    return sycl::inclusive_scan_over_group(this_sub_group(),x,binary_op);
  }

  template <typename T, class BinaryOperation> __forceinline T sub_group_inclusive_scan(T x, BinaryOperation binary_op, T init) {
    return sycl::inclusive_scan_over_group(this_sub_group(),x,binary_op,init);
  }

  template <typename T> __forceinline T sub_group_shuffle(T x, sycl::id<1> local_id) {
    return this_sub_group().shuffle(x, local_id);
  }

  template <typename T> __forceinline T sub_group_shuffle_down(T x, uint32_t delta) {
    return this_sub_group().shuffle_down(x, delta);
  }
  
  template <typename T> __forceinline T sub_group_shuffle_up(T x, uint32_t delta) {
    return this_sub_group().shuffle_up(x, delta);
  }

  template <typename T> __forceinline T sub_group_load(const void* src) {
    return this_sub_group().load(sycl::multi_ptr<T,sycl::access::address_space::global_space>((T*)src));
  }

  template <typename T> __forceinline void sub_group_store(void* dst, const T& x) {
    this_sub_group().store(sycl::multi_ptr<T,sycl::access::address_space::global_space>((T*)dst),x);
  }

  __forceinline const void sub_group_barrier() {
    return this_sub_group().barrier();
  }

}

#if __SYCL_COMPILER_VERSION < 20210801
#undef all_of_group
#undef any_of_group
#undef none_of_group
#undef group_broadcast
#undef reduce_over_group
#undef exclusive_scan_over_group
#undef inclusive_scan_over_group
#endif
