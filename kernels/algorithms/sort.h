// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "../../common/sys/platform.h"
#include "../../common/sys/sysinfo.h"
#include "../../common/sys/barrier.h"
#include "../../common/math/math.h"
#include "parallel_for.h"
#include <algorithm>

#if defined(__MIC__)
 #include "../../common/simd/avx512.h"
#endif

#define RADIX_SORT_MIN_BLOCK_SIZE 4096


namespace embree
{
  template<class T>
    __forceinline void insertionsort_ascending(T *__restrict__ array, const size_t length)
  {
    for(size_t i = 1;i<length;++i)
    {
      T v = array[i];
      size_t j = i;
      while(j > 0 && v < array[j-1])
      {
        array[j] = array[j-1];
        --j;
      }
      array[j] = v;
    }
  }
  
  template<class T>
    __forceinline void insertionsort_decending(T *__restrict__ array, const size_t length)
  {
    for(size_t i = 1;i<length;++i)
    {
      T v = array[i];
      size_t j = i;
      while(j > 0 && v > array[j-1])
      {
        array[j] = array[j-1];
        --j;
      }
      array[j] = v;
    }
  }
  
  template<class T> 
    void quicksort_ascending(T *__restrict__ t, 
			     const ssize_t begin, 
			     const ssize_t end)
  {
    if (likely(begin < end)) 
    {      
      const T pivotvalue = t[begin];
      ssize_t left  = begin - 1;
      ssize_t right = end   + 1;
      
      while(1) 
      {
        while (t[--right] > pivotvalue);
        while (t[++left] < pivotvalue);
        
        if (left >= right) break;
        
        const T temp = t[right];
        t[right] = t[left];
        t[left] = temp;
      }
      
      const int pivot = right;
      quicksort_ascending(t, begin, pivot);
      quicksort_ascending(t, pivot + 1, end);
    }
  }
  
  template<class T> 
    void quicksort_decending(T *__restrict__ t, 
			     const ssize_t begin, 
			     const ssize_t end)
  {
    if (likely(begin < end)) 
    {
      const T pivotvalue = t[begin];
      ssize_t left  = begin - 1;
      ssize_t right = end   + 1;
      
      while(1) 
      {
        while (t[--right] < pivotvalue);
        while (t[++left] > pivotvalue);
        
        if (left >= right) break;
        
        const T temp = t[right];
        t[right] = t[left];
        t[left] = temp;
      }
      
      const int pivot = right;
      quicksort_decending(t, begin, pivot);
      quicksort_decending(t, pivot + 1, end);
    }
  }
  
  
  template<class T, ssize_t THRESHOLD> 
    void quicksort_insertionsort_ascending(T *__restrict__ t, 
					   const ssize_t begin, 
					   const ssize_t end)
  {
    if (likely(begin < end)) 
    {      
      const ssize_t size = end-begin+1;
      if (likely(size <= THRESHOLD))
      {
        insertionsort_ascending<T>(&t[begin],size);
      }
      else
      {
        const T pivotvalue = t[begin];
        ssize_t left  = begin - 1;
        ssize_t right = end   + 1;
        
        while(1) 
        {
          while (t[--right] > pivotvalue);
          while (t[++left] < pivotvalue);
          
          if (left >= right) break;
          
          const T temp = t[right];
          t[right] = t[left];
          t[left] = temp;
        }
        
        const ssize_t pivot = right;
        quicksort_insertionsort_ascending<T,THRESHOLD>(t, begin, pivot);
        quicksort_insertionsort_ascending<T,THRESHOLD>(t, pivot + 1, end);
      }
    }
  }
  
  
  template<class T, ssize_t THRESHOLD> 
    void quicksort_insertionsort_decending(T *__restrict__ t, 
					   const ssize_t begin, 
					   const ssize_t end)
  {
    if (likely(begin < end)) 
    {
      const ssize_t size = end-begin+1;
      if (likely(size <= THRESHOLD))
      {
        insertionsort_decending<T>(&t[begin],size);
      }
      else
      {
        
        const T pivotvalue = t[begin];
        ssize_t left  = begin - 1;
        ssize_t right = end   + 1;
        
        while(1) 
        {
          while (t[--right] < pivotvalue);
          while (t[++left] > pivotvalue);
          
          if (left >= right) break;
          
          const T temp = t[right];
          t[right] = t[left];
          t[left] = temp;
        }
        
        const ssize_t pivot = right;
        quicksort_insertionsort_decending<T,THRESHOLD>(t, begin, pivot);
        quicksort_insertionsort_decending<T,THRESHOLD>(t, pivot + 1, end);
      }
    }
  }
  
#if defined(__MIC__)
  
  class __aligned(64) ParallelRadixSort
  {
  public:
    static const size_t MAX_TASKS = MAX_THREADS;
    static const size_t BITS = 8;
    static const size_t BUCKETS = (1 << BITS);
    typedef unsigned int TyRadixCount[MAX_TASKS][BUCKETS];
    
    template<typename Ty, typename Key>
      class Task
    {
      template<typename T>
	static bool compare(const T& v0, const T& v1) {
	return (Key)v0 < (Key)v1;
      }
      
    public:
      Task (ParallelRadixSort* parent, Ty* const src, Ty* const tmp, const size_t N, const size_t blockSize)
	: parent(parent), src(src), tmp(tmp), N(N) 
      {
        assert(blockSize > 0);
        
	/* perform single threaded sort for small N */
	if (N<=blockSize) // handles also special case of 0!
	{	  
	  /* do inplace sort inside destination array */
	  std::sort(src,src+N,compare<Ty>);
	}
	
	/* perform parallel sort for large N */
	else 
	{
	  LockStepTaskScheduler* scheduler = LockStepTaskScheduler::instance();
	  const size_t numThreads = scheduler->getNumThreads(); 
	  parent->barrier.init(numThreads);
	  scheduler->dispatchTask(task_radixsort,this,0,numThreads);
	}
      }
      
    private:
      
      void radixIteration(Ty* __restrict src, 
			  Ty* __restrict dst, 
			  const size_t startID, 
			  const size_t endID, 
			  const size_t threadIndex, 
			  const size_t threadCount,
			  const size_t byte_iteration)
      {
	const size_t L1_PREFETCH_ITEMS = 4;
        
	for (size_t b=0; b<byte_iteration; b++)
        {
#pragma unroll(16)
          for (size_t i=0; i<16; i++)
            vint16::store(&parent->radixCount[threadIndex][i*16],vint16::zero());
          
          __assume_aligned(&parent->radixCount[threadIndex][0],64);
          
          for (size_t i=startID; i<endID; i++) {
            const Key &key = src[i];
            const unsigned char *__restrict const byte = (const unsigned char*)&key;
            prefetch<PFHINT_NT>(byte + 64*4);
            parent->radixCount[threadIndex][(unsigned int)byte[b]]++;
          }
          
          parent->barrier.wait(threadIndex,threadCount);
          
          vint16 count[16];
#pragma unroll(16)
          for (size_t i=0; i<16; i++)
            count[i] = vint16::zero();
          
          
          for (size_t i=0; i<threadIndex; i++)
#pragma unroll(16)
            for (size_t j=0; j<16; j++)
              count[j] += vint16::load((int*)&parent->radixCount[i][j*16]);
          
          __aligned(64) unsigned int inner_offset[BUCKETS];
          
#pragma unroll(16)
          for (size_t i=0; i<16; i++)
            vint16::store(&inner_offset[i*16],count[i]);
          
#pragma unroll(16)
          for (size_t i=0; i<16; i++)
            count[i] = vint16::load((int*)&inner_offset[i*16]);
          
          for (size_t i=threadIndex; i<threadCount; i++)
#pragma unroll(16)
            for (size_t j=0; j<16; j++)
              count[j] += vint16::load((int*)&parent->radixCount[i][j*16]);	  
          
          __aligned(64) unsigned int total[BUCKETS];
          
#pragma unroll(16)
          for (size_t i=0; i<16; i++)
            vint16::store(&total[i*16],count[i]);
          
          __aligned(64) unsigned int offset[BUCKETS];
          
          /* calculate start offset of each bucket */
          offset[0] = 0;
          for (size_t i=1; i<BUCKETS; i++)    
            offset[i] = offset[i-1] + total[i-1];
          
          /* calculate start offset of each bucket for this thread */
          
#pragma unroll(BUCKETS)
          for (size_t j=0; j<BUCKETS; j++)
            offset[j] += inner_offset[j];
	  
          for (size_t i=startID; i<endID; i++) {
            const Key &key = src[i];
            
            const unsigned char *__restrict const byte = (const unsigned char*)&key;
            prefetch<PFHINT_NT>((char*)byte + 2*64);
            
            const unsigned int index = byte[b];
            
            assert(index < BUCKETS);
            dst[offset[index]] = src[i];
            prefetch<PFHINT_L2EX>(&dst[offset[index]+L1_PREFETCH_ITEMS]);
            offset[index]++;
          }
          
          if (b<byte_iteration-1) parent->barrier.wait(threadIndex,threadCount);
          std::swap(src,dst);
        }
      }
      
      void radixsort(const size_t threadIndex, const size_t numThreads)
      {
	const size_t startID = (threadIndex+0)*N/numThreads;
	const size_t endID   = (threadIndex+1)*N/numThreads;
        
	if (sizeof(Key) == sizeof(uint32_t)) 
	  radixIteration(src,tmp,startID,endID,threadIndex,numThreads,4);
	else if (sizeof(Key) == sizeof(uint64_t)) 
	  radixIteration(src,tmp,startID,endID,threadIndex,numThreads,8);
      }
      
      static void task_radixsort (void* data, const size_t threadIndex, const size_t threadCount) { 
	((Task*)data)->radixsort(threadIndex,threadCount);                          
      }
      
    private:
      ParallelRadixSort* const parent;
      Ty* const src;
      Ty* const tmp;
      const size_t N;
    };
    
  private:
    __aligned(64) TyRadixCount radixCount;
    __aligned(64) QuadTreeBarrier barrier; 
  };
  
#else
  
  class __aligned(64) ParallelRadixSort
  {
  public:
    static const size_t MAX_TASKS = MAX_THREADS;
    static const size_t BITS = 8;
    static const size_t BUCKETS = (1 << BITS);
    typedef unsigned int TyRadixCount[BUCKETS];
    
    ParallelRadixSort() 
      : radixCount(nullptr) {}

    template<typename Ty, typename Key>
      class Task
    {
      template<typename T>
        static bool compare(const T& v0, const T& v1) {
        return (Key)v0 < (Key)v1;
      }
      
    public:
      Task (ParallelRadixSort* parent, Ty* const src, Ty* const tmp, const size_t N, const size_t blockSize)
        : parent(parent), src(src), tmp(tmp), N(N)
      {
        assert(blockSize > 0);
        
        /* perform single threaded sort for small N */
        if (N<=blockSize) // handles also special case of 0!
        {	  
          /* do inplace sort inside destination array */
          std::sort(src,src+N,compare<Ty>);
        }
        
        /* perform parallel sort for large N */
        else 
        {
          const size_t numThreads = min((N+blockSize-1)/blockSize,TaskSchedulerTBB::threadCount(),size_t(MAX_TASKS));
          tbbRadixSort(numThreads);
        }
      }
      
    private:
      
      void tbbRadixIteration0(const Key shift, 
                              const Ty* __restrict const src, 
                              Ty* __restrict const dst, 
                              const size_t threadIndex, const size_t threadCount)
      {
        const size_t startID = (threadIndex+0)*N/threadCount;
        const size_t endID   = (threadIndex+1)*N/threadCount;
        
        /* mask to extract some number of bits */
        const Key mask = BUCKETS-1;
        
        /* count how many items go into the buckets */
        for (size_t i=0; i<BUCKETS; i++)
          parent->radixCount[threadIndex][i] = 0;
        
        for (size_t i=startID; i<endID; i++) {
          const Key index = ((Key)src[i] >> shift) & mask;
          parent->radixCount[threadIndex][index]++;
        }
      }
      
      void tbbRadixIteration1(const Key shift, 
                              const Ty* __restrict const src, 
                              Ty* __restrict const dst, 
                              const size_t threadIndex, const size_t threadCount)
      {
        const size_t startID = (threadIndex+0)*N/threadCount;
        const size_t endID   = (threadIndex+1)*N/threadCount;
        
        /* mask to extract some number of bits */
        const Key mask = BUCKETS-1;
        
        /* calculate total number of items for each bucket */
        __aligned(64) unsigned int total[BUCKETS];
        for (size_t i=0; i<BUCKETS; i++)
          total[i] = 0;
        
        for (size_t i=0; i<threadCount; i++)
          for (size_t j=0; j<BUCKETS; j++)
            total[j] += parent->radixCount[i][j];
        
        /* calculate start offset of each bucket */
        __aligned(64) unsigned int offset[BUCKETS];
        offset[0] = 0;
        for (size_t i=1; i<BUCKETS; i++)    
          offset[i] = offset[i-1] + total[i-1];
        
        /* calculate start offset of each bucket for this thread */
        for (size_t i=0; i<threadIndex; i++)
          for (size_t j=0; j<BUCKETS; j++)
            offset[j] += parent->radixCount[i][j];
        
        /* copy items into their buckets */
        for (size_t i=startID; i<endID; i++) {
          const Ty elt = src[i];
          const Key index = ((Key)src[i] >> shift) & mask;
          dst[offset[index]++] = elt;
          //prefetchEX(((char*)&dst[offset[index]]) + 64);
        }
      }
      
      void tbbRadixIteration(const Key shift, const bool last,
                             const Ty* __restrict src, Ty* __restrict dst,
                             const size_t numTasks)
      {
        parallel_for(numTasks,[&] (size_t taskIndex) { tbbRadixIteration0(shift,src,dst,taskIndex,numTasks); });
        parallel_for(numTasks,[&] (size_t taskIndex) { tbbRadixIteration1(shift,src,dst,taskIndex,numTasks); });
      }
      
      void tbbRadixSort(const size_t numTasks)
      {
        assert(parent->radixCount == nullptr);

        parent->radixCount = (TyRadixCount*) alignedMalloc(MAX_TASKS*sizeof(TyRadixCount));

        if (sizeof(Key) == sizeof(uint32_t)) {
          tbbRadixIteration(0*BITS,0,src,tmp,numTasks);
          tbbRadixIteration(1*BITS,0,tmp,src,numTasks);
          tbbRadixIteration(2*BITS,0,src,tmp,numTasks);
          tbbRadixIteration(3*BITS,1,tmp,src,numTasks);
        }
        else if (sizeof(Key) == sizeof(uint64_t))
        {
          tbbRadixIteration(0*BITS,0,src,tmp,numTasks);
          tbbRadixIteration(1*BITS,0,tmp,src,numTasks);
          tbbRadixIteration(2*BITS,0,src,tmp,numTasks);
          tbbRadixIteration(3*BITS,0,tmp,src,numTasks);
          tbbRadixIteration(4*BITS,0,src,tmp,numTasks);
          tbbRadixIteration(5*BITS,0,tmp,src,numTasks);
          tbbRadixIteration(6*BITS,0,src,tmp,numTasks);
          tbbRadixIteration(7*BITS,1,tmp,src,numTasks);
        }
        alignedFree(parent->radixCount); 
        parent->radixCount = nullptr;
      }
      
    private:
      ParallelRadixSort* const parent;
      Ty* const src;
      Ty* const tmp;
      const size_t N;
    };

  private:
    TyRadixCount* radixCount;
  };
  
#endif
  
  /*! parallel radix sort */
  template<typename Key>
    struct ParallelRadixSortT
    {
      ParallelRadixSortT (ParallelRadixSort& state) 
      : state(state) {} 
      
      template<typename Ty>
      void operator() (Ty* const src, Ty* const tmp, const size_t N, const size_t blockSize = RADIX_SORT_MIN_BLOCK_SIZE) {
        ParallelRadixSort::Task<Ty,Key>(&state,src,tmp,N,blockSize);
      }
      
      ParallelRadixSort& state;
    };
  
  template<typename Ty>
    void radix_sort(Ty* const src, Ty* const tmp, const size_t N, const size_t blockSize = RADIX_SORT_MIN_BLOCK_SIZE)
  {
    ParallelRadixSort radix_sort_state;
    ParallelRadixSortT<Ty> sort(radix_sort_state);
    sort(src,tmp,N,blockSize);
  }
  
  template<typename Ty, typename Key>
    void radix_sort(Ty* const src, Ty* const tmp, const size_t N, const size_t blockSize = RADIX_SORT_MIN_BLOCK_SIZE)
  {
    ParallelRadixSort radix_sort_state;
    ParallelRadixSortT<Key> sort(radix_sort_state);
    sort(src,tmp,N,blockSize);
  }
  
  template<typename Ty>
    void radix_sort_u32(Ty* const src, Ty* const tmp, const size_t N, const size_t blockSize = RADIX_SORT_MIN_BLOCK_SIZE) {
    radix_sort<Ty,uint32_t>(src,tmp,N,blockSize);
  }
  
  template<typename Ty>
    void radix_sort_u64(Ty* const src, Ty* const tmp, const size_t N, const size_t blockSize = RADIX_SORT_MIN_BLOCK_SIZE) {
    radix_sort<Ty,uint64_t>(src,tmp,N,blockSize);
  }
}
