## Copyright 2009-2022 Intel Corporation
## SPDX-License-Identifier: Apache-2.0

#PROJECT(rthwif_testing)
#CMAKE_MINIMUM_REQUIRED(VERSION 3.1.0)

SET(CMAKE_CXX_STANDARD 17)

IF (NOT WIN32)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")                       # generate position independent code suitable for shared libraries
ENDIF()

IF (NOT DEFINED EMBREE_SYCL_IMPLICIT_DISPATCH_GLOBALS)
  OPTION(EMBREE_SYCL_IMPLICIT_DISPATCH_GLOBALS "Using L0 allocated Dispatch Globals" ON)
ENDIF()

#IF (NOT EMBREE_SYCL_IMPLICIT_DISPATCH_GLOBALS)
#  ADD_DEFINITIONS("-DEMBREE_SYCL_ALLOC_DISPATCH_GLOBALS")
#ENDIF()

IF (NOT DEFINED EMBREE_SYCL_RT_VALIDATION_API)
  OPTION(EMBREE_SYCL_RT_VALIDATION_API "Use rt_validation API instead of IGC provided rt_production API" OFF)
ENDIF()

IF (EMBREE_SYCL_RT_VALIDATION_API)
  ADD_DEFINITIONS("-DEMBREE_SYCL_RT_VALIDATION_API")
ENDIF()

IF (EMBREE_SYCL_RT_VALIDATION_API AND NOT EMBREE_SYCL_IMPLICIT_DISPATCH_GLOBALS)
  ADD_DEFINITIONS("-DEMBREE_SYCL_ALLOC_DISPATCH_GLOBALS")
ENDIF()

IF (EMBREE_SYCL_RT_SIMULATION)
  SET(RT_SIM_LIBRARY rtcore)
ENDIF()

#ADD_EXECUTABLE(embree_rthwif_cornell_box rthwif_cornell_box.cpp)
#TARGET_LINK_LIBRARIES(embree_rthwif_cornell_box embree_rthwif sys simd ${TBB_TARGET} ${RT_SIM_LIBRARY})
#SET_PROPERTY(TARGET embree_rthwif_cornell_box APPEND PROPERTY COMPILE_FLAGS "-fsycl -fsycl-targets=spir64 -DEMBREE_SYCL_SUPPORT")
#SET_PROPERTY(TARGET embree_rthwif_cornell_box APPEND PROPERTY LINK_FLAGS "-fsycl -lze_loader -fsycl-targets=spir64 -Xsycl-target-backend=spir64 \" -cl-intel-greater-than-4GB-buffer-required \"")

#ADD_EXECUTABLE(rthwif_test rthwif_test.cpp)
#TARGET_LINK_LIBRARIES(rthwif_test embree_rthwif sys simd tbb)
#IF (EMBREE_SYCL_GPU_BVH_BUILDER)
#TARGET_LINK_LIBRARIES(rthwif_test embree_rthwif_gpu sys tbb)
#ENDIF()

#SET_PROPERTY(TARGET rthwif_test APPEND PROPERTY COMPILE_FLAGS "-fsycl -fsycl-targets=spir64 -DEMBREE_SYCL_SUPPORT")
#SET_PROPERTY(TARGET rthwif_test APPEND PROPERTY LINK_FLAGS "-fsycl -lze_loader -fsycl-targets=spir64 -Xsycl-target-backend=spir64 \" -cl-intel-greater-than-4GB-buffer-required \"")

ADD_EXECUTABLE(embree_rthwif_test rthwif_test.cpp)
TARGET_LINK_LIBRARIES(embree_rthwif_test embree_rthwif sys simd ${TBB_TARGET} ${RT_SIM_LIBRARY})
IF (EMBREE_SYCL_GPU_BVH_BUILDER)
TARGET_LINK_LIBRARIES(embree_rthwif_test embree_rthwif_gpu sys tbb)
ENDIF()

SET_PROPERTY(TARGET embree_rthwif_test APPEND PROPERTY COMPILE_FLAGS "-fsycl -fsycl-targets=spir64 -DEMBREE_SYCL_SUPPORT")
SET_PROPERTY(TARGET embree_rthwif_test APPEND PROPERTY LINK_FLAGS "-fsycl -lze_loader -fsycl-targets=spir64 -Xsycl-target-backend=spir64 \" -cl-intel-greater-than-4GB-buffer-required \"")

IF (NOT DEFINED EMBREE_SYCL_RT_VALIDATION_API OR EMBREE_SYCL_IMPLICIT_DISPATCH_GLOBALS)

  IF (DEFINED EMBREE_MODEL_DIR)
    SET(CORNELL_BOX_REFERENCE "${EMBREE_MODEL_DIR}/reference/cornell_box_reference.tga")
  ELSE()
    SET(CORNELL_BOX_REFERENCE "${CMAKE_CURRENT_SOURCE_DIR}/cornell_box_reference.tga")
  ENDIF()
  
  ADD_TEST(NAME rthwif_cornell_box
           COMMAND embree_rthwif_cornell_box --compare "${CORNELL_BOX_REFERENCE}"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
ENDIF()

ADD_TEST(NAME rthwif_test_builder_triangles_expected      COMMAND embree_rthwif_test --build_test_triangles   --build_mode_expected)
ADD_TEST(NAME rthwif_test_builder_procedurals_expected    COMMAND embree_rthwif_test --build_test_procedurals --build_mode_expected)
ADD_TEST(NAME rthwif_test_builder_instances_expected      COMMAND embree_rthwif_test --build_test_instances   --build_mode_expected)
ADD_TEST(NAME rthwif_test_builder_mixed_expected          COMMAND embree_rthwif_test --build_test_mixed       --build_mode_expected)

ADD_TEST(NAME rthwif_test_benchmark_triangles             COMMAND embree_rthwif_test --benchmark_triangles)
ADD_TEST(NAME rthwif_test_benchmark_procedurals           COMMAND embree_rthwif_test --benchmark_procedurals)

ADD_TEST(NAME rthwif_test_builder_triangles_worst_case      COMMAND embree_rthwif_test --build_test_triangles   --build_mode_worst_case)
ADD_TEST(NAME rthwif_test_builder_procedurals_worst_case    COMMAND embree_rthwif_test --build_test_procedurals --build_mode_worst_case)
ADD_TEST(NAME rthwif_test_builder_instances_worst_case      COMMAND embree_rthwif_test --build_test_instances   --build_mode_worst_case)
ADD_TEST(NAME rthwif_test_builder_mixed_worst_case          COMMAND embree_rthwif_test --build_test_mixed       --build_mode_worst_case)

ADD_TEST(NAME rthwif_test_triangles_committed_hit        COMMAND embree_rthwif_test --no-instancing --triangles-committed-hit)
ADD_TEST(NAME rthwif_test_triangles_potential_hit        COMMAND embree_rthwif_test --no-instancing --triangles-potential-hit)
ADD_TEST(NAME rthwif_test_triangles_anyhit_shader_commit COMMAND embree_rthwif_test --no-instancing --triangles-anyhit-shader-commit)
ADD_TEST(NAME rthwif_test_triangles_anyhit_shader_reject COMMAND embree_rthwif_test --no-instancing --triangles-anyhit-shader-reject)
ADD_TEST(NAME rthwif_test_procedurals_committed_hit      COMMAND embree_rthwif_test --no-instancing --procedurals-committed-hit)

ADD_TEST(NAME rthwif_test_hwinstancing_triangles_committed_hit        COMMAND embree_rthwif_test --hw-instancing --triangles-committed-hit)
ADD_TEST(NAME rthwif_test_hwinstancing_triangles_potential_hit        COMMAND embree_rthwif_test --hw-instancing --triangles-potential-hit)
ADD_TEST(NAME rthwif_test_hwinstancing_triangles_anyhit_shader_commit COMMAND embree_rthwif_test --hw-instancing --triangles-anyhit-shader-commit)
ADD_TEST(NAME rthwif_test_hwinstancing_triangles_anyhit_shader_reject COMMAND embree_rthwif_test --hw-instancing --triangles-anyhit-shader-reject)
ADD_TEST(NAME rthwif_test_hwinstancing_procedurals_committed_hit      COMMAND embree_rthwif_test --hw-instancing --procedurals-committed-hit)

ADD_TEST(NAME rthwif_test_swinstancing_triangles_committed_hit        COMMAND embree_rthwif_test --sw-instancing --triangles-committed-hit)
ADD_TEST(NAME rthwif_test_swinstancing_triangles_potential_hit        COMMAND embree_rthwif_test --sw-instancing --triangles-potential-hit)
ADD_TEST(NAME rthwif_test_swinstancing_triangles_anyhit_shader_commit COMMAND embree_rthwif_test --sw-instancing --triangles-anyhit-shader-commit)
ADD_TEST(NAME rthwif_test_swinstancing_triangles_anyhit_shader_reject COMMAND embree_rthwif_test --sw-instancing --triangles-anyhit-shader-reject)
ADD_TEST(NAME rthwif_test_swinstancing_procedurals_committed_hit      COMMAND embree_rthwif_test --sw-instancing --procedurals-committed-hit)
