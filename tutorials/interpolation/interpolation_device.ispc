// Copyright 2009-2021 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "interpolation_device.isph"
#include "../common/tutorial/optics.isph"

//#define FORCE_FIXED_EDGE_TESSELLATION
#define FIXED_EDGE_TESSELLATION_VALUE 16

#define MAX_EDGE_LEVEL 64.0f
#define MIN_EDGE_LEVEL  4.0f
#define LEVEL_FACTOR  128.0f

/* scene data */
RTCScene g_scene = NULL;
uniform TutorialData data;
uniform unsigned int triCubeID, quadCubeID;

#define NUM_VERTICES 8

__aligned(16) uniform float cube_vertices[8][4] =
{
  { -1.0f, -1.0f, -1.0f, 0.0f },
  {  1.0f, -1.0f, -1.0f, 0.0f },
  {  1.0f, -1.0f,  1.0f, 0.0f },
  { -1.0f, -1.0f,  1.0f, 0.0f },
  { -1.0f,  1.0f, -1.0f, 0.0f },
  {  1.0f,  1.0f, -1.0f, 0.0f },
  {  1.0f,  1.0f,  1.0f, 0.0f },
  { -1.0f,  1.0f,  1.0f, 0.0f }
};

__aligned(16) uniform float cube_vertex_colors[8][4] =
{
  {  0.0f,  0.0f,  0.0f, 0.0f },
  {  1.0f,  0.0f,  0.0f, 0.0f },
  {  1.0f,  0.0f,  1.0f, 0.0f },
  {  0.0f,  0.0f,  1.0f, 0.0f },
  {  0.0f,  1.0f,  0.0f, 0.0f },
  {  1.0f,  1.0f,  0.0f, 0.0f },
  {  1.0f,  1.0f,  1.0f, 0.0f },
  {  0.0f,  1.0f,  1.0f, 0.0f }
};

__aligned(16) uniform float cube_vertex_crease_weights[8] = {
  inf, inf,inf, inf, inf, inf, inf, inf
};

__aligned(16) uniform unsigned int cube_vertex_crease_indices[8] = {
  0,1,2,3,4,5,6,7
};

__aligned(16) uniform float cube_edge_crease_weights[12] = {
  inf, inf, inf, inf, inf, inf, inf, inf, inf, inf, inf, inf
};

__aligned(16) uniform unsigned int cube_edge_crease_indices[24] =
{
  0,1, 1,2, 2,3, 3,0,
  4,5, 5,6, 6,7, 7,4,
  0,4, 1,5, 2,6, 3,7,
};

#define NUM_QUAD_INDICES 24
#define NUM_QUAD_FACES 6

uniform unsigned int cube_quad_indices[24] = {
  0, 4, 5, 1,
  1, 5, 6, 2,
  2, 6, 7, 3,
  0, 3, 7, 4,
  4, 7, 6, 5,
  0, 1, 2, 3,
};

uniform unsigned int cube_quad_faces[6] = {
  4, 4, 4, 4, 4, 4
};

#define NUM_TRI_INDICES 36
#define NUM_TRI_FACES 12

uniform unsigned int cube_tri_indices[36] = {
  1, 4, 5,  0, 4, 1,
  2, 5, 6,  1, 5, 2,
  3, 6, 7,  2, 6, 3,
  4, 3, 7,  0, 3, 4,
  5, 7, 6,  4, 7, 5,
  3, 1, 2,  0, 1, 3
};

uniform unsigned int cube_tri_faces[12] = {
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3
};

#define NUM_HAIR_VERTICES 4

__aligned(16) uniform float hair_vertices[4][4] =
{
  { 0.0f, 0.0f, 0.0f, 0.1f },
  { 0.5f, 1.0f, 0.0f, 0.1f },
  { 0.0f, 2.0f, -0.5f, 0.1f },
  { 0.0f, 3.0f, 0.0f, 0.1f }
};

__aligned(16) uniform float hair_vertex_colors[4][4] =
{
  {  1.0f,  0.0f,  0.0f, 0.0f },
  {  1.0f,  1.0f,  0.0f, 0.0f },
  {  0.0f,  0.0f,  1.0f, 0.0f },
  {  1.0f,  1.0f,  1.0f, 0.0f },
};

uniform unsigned int hair_indices[1] = {
  0
};

inline uniform float updateEdgeLevel(const uniform Vec3f& cam_pos, uniform Vec3fa* uniform vtx, uniform unsigned int* uniform indices, const uniform unsigned int e0, const uniform unsigned int e1)
{
  const uniform Vec3fa v0 = vtx[indices[e0]];
  const uniform Vec3fa v1 = vtx[indices[e1]];
  const uniform Vec3fa edge = v1-v0;
  const uniform Vec3fa P = 0.5f*(v1+v0);
  const uniform Vec3fa dist = make_Vec3fa(cam_pos) - P;
  const uniform float level = max(min(LEVEL_FACTOR*(0.5f*length(edge)/length(dist)),MAX_EDGE_LEVEL),MIN_EDGE_LEVEL);
  return level;
}

/* adds a subdiv cube to the scene */
uniform unsigned int addTriangleSubdivCube (RTCScene scene_i, const uniform Vec3f& pos)
{
  RTCGeometry geom = rtcNewGeometry(g_device, RTC_GEOMETRY_TYPE_SUBDIVISION);

  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, cube_vertices, 0, sizeof(uniform Vec3fa  ), NUM_VERTICES);
  uniform Vec3fa* uniform vtx = (uniform Vec3fa* uniform) rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, sizeof(uniform Vec3fa), NUM_VERTICES);
  for (uniform unsigned int i=0; i<NUM_VERTICES; i++) vtx[i] = make_Vec3fa(cube_vertices[i][0]+pos.x,cube_vertices[i][1]+pos.y,cube_vertices[i][2]+pos.z);

  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT, cube_tri_indices, 0, sizeof(uniform unsigned int), NUM_TRI_INDICES);
  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_FACE,  0, RTC_FORMAT_UINT, cube_tri_faces,   0, sizeof(uniform unsigned int), NUM_TRI_FACES);

  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_EDGE_CREASE_INDEX,  0, RTC_FORMAT_UINT2, cube_edge_crease_indices, 0, 2*sizeof(uniform unsigned int), 0);
  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_EDGE_CREASE_WEIGHT, 0, RTC_FORMAT_FLOAT, cube_edge_crease_weights, 0, sizeof(uniform float),          0);

  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_CREASE_INDEX,  0, RTC_FORMAT_UINT,  cube_vertex_crease_indices, 0, sizeof(uniform unsigned int), 0);
  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_CREASE_WEIGHT, 0, RTC_FORMAT_FLOAT, cube_vertex_crease_weights, 0, sizeof(uniform float),        0);

  rtcSetGeometryVertexAttributeCount(geom,1);
  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, RTC_FORMAT_FLOAT3, cube_vertex_colors, 0, sizeof(uniform Vec3fa), NUM_VERTICES);

  uniform float* uniform level = (uniform float* uniform) rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_LEVEL, 0, RTC_FORMAT_FLOAT, sizeof(uniform float), NUM_TRI_INDICES);
  for (uniform unsigned int i=0; i<NUM_TRI_INDICES; i++) level[i] = FIXED_EDGE_TESSELLATION_VALUE;

  rtcCommitGeometry(geom);
  uniform unsigned int geomID = rtcAttachGeometry(scene_i, geom);
  rtcReleaseGeometry(geom);
  return geomID;
}

void setTriangleSubdivCubeLevels (RTCGeometry geom, const uniform Vec3f& cam_pos)
{
  uniform Vec3fa* uniform vtx = (uniform Vec3fa* uniform) rtcGetGeometryBufferData(geom, RTC_BUFFER_TYPE_VERTEX, 0);
  if (vtx == NULL) return;
  uniform float* uniform level = (uniform float* uniform) rtcGetGeometryBufferData(geom, RTC_BUFFER_TYPE_LEVEL, 0);
  if (level == NULL) return;

  for (uniform unsigned int i=0; i<NUM_TRI_INDICES; i+=3)
  {
    level[i+0] = updateEdgeLevel(cam_pos, vtx, cube_tri_indices, i+0, i+1);
    level[i+1] = updateEdgeLevel(cam_pos, vtx, cube_tri_indices, i+1, i+2);
    level[i+2] = updateEdgeLevel(cam_pos, vtx, cube_tri_indices, i+2, i+0);
  }

  rtcUpdateGeometryBuffer(geom, RTC_BUFFER_TYPE_LEVEL, 0);
  rtcCommitGeometry(geom);
}

/* adds a subdiv cube to the scene */
uniform unsigned int addQuadSubdivCube (RTCScene scene_i, const uniform Vec3f& pos)
{
  RTCGeometry geom = rtcNewGeometry(g_device, RTC_GEOMETRY_TYPE_SUBDIVISION);

  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, cube_vertices, 0, sizeof(uniform Vec3fa  ), NUM_VERTICES);
  uniform Vec3fa* uniform vtx = (uniform Vec3fa* uniform) rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, sizeof(uniform Vec3fa), NUM_VERTICES);
  for (uniform unsigned int i=0; i<NUM_VERTICES; i++) vtx[i] = make_Vec3fa(cube_vertices[i][0]+pos.x,cube_vertices[i][1]+pos.y,cube_vertices[i][2]+pos.z);

  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT, cube_quad_indices, 0, sizeof(uniform unsigned int), NUM_QUAD_INDICES);
  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_FACE,  0, RTC_FORMAT_UINT, cube_quad_faces,   0, sizeof(uniform unsigned int), NUM_QUAD_FACES);

  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_EDGE_CREASE_INDEX,  0, RTC_FORMAT_UINT2, cube_edge_crease_indices, 0, 2*sizeof(uniform unsigned int), 0);
  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_EDGE_CREASE_WEIGHT, 0, RTC_FORMAT_FLOAT, cube_edge_crease_weights, 0, sizeof(uniform float),          0);

  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_CREASE_INDEX,  0, RTC_FORMAT_UINT,  cube_vertex_crease_indices, 0, sizeof(uniform unsigned int), 0);
  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_CREASE_WEIGHT, 0, RTC_FORMAT_FLOAT, cube_vertex_crease_weights, 0, sizeof(uniform float),        0);

  rtcSetGeometryVertexAttributeCount(geom,1);
  rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, RTC_FORMAT_FLOAT3, cube_vertex_colors, 0, sizeof(uniform Vec3fa), NUM_VERTICES);

  uniform float* uniform level = (uniform float* uniform) rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_LEVEL, 0, RTC_FORMAT_FLOAT, sizeof(uniform float), NUM_QUAD_INDICES);
  for (uniform unsigned int i=0; i<NUM_QUAD_INDICES; i++) level[i] = FIXED_EDGE_TESSELLATION_VALUE;

  rtcCommitGeometry(geom);
  uniform unsigned int geomID = rtcAttachGeometry(scene_i, geom);
  rtcReleaseGeometry(geom);
  return geomID;
}

void setQuadSubdivCubeLevels (RTCGeometry geom, const uniform Vec3f& cam_pos)
{
  uniform Vec3fa* uniform vtx = (uniform Vec3fa* uniform) rtcGetGeometryBufferData(geom, RTC_BUFFER_TYPE_VERTEX, 0);
  if (vtx == NULL) return;
  uniform float* uniform level = (uniform float* uniform) rtcGetGeometryBufferData(geom, RTC_BUFFER_TYPE_LEVEL, 0);
  if (level == NULL) return;

  for (uniform unsigned int i=0; i<NUM_QUAD_INDICES; i+=4)
  {
    level[i+0] = updateEdgeLevel(cam_pos, vtx, cube_quad_indices, i+0, i+1);
    level[i+1] = updateEdgeLevel(cam_pos, vtx, cube_quad_indices, i+1, i+2);
    level[i+2] = updateEdgeLevel(cam_pos, vtx, cube_quad_indices, i+2, i+3);
    level[i+3] = updateEdgeLevel(cam_pos, vtx, cube_quad_indices, i+3, i+0);
  }

  rtcUpdateGeometryBuffer(geom, RTC_BUFFER_TYPE_LEVEL, 0);
  rtcCommitGeometry(geom);
}

/* adds a triangle cube to the scene */
uniform unsigned int addTriangleCube (RTCScene scene_i, const uniform Vec3f& pos)
{
  RTCGeometry geom = rtcNewGeometry(g_device, RTC_GEOMETRY_TYPE_TRIANGLE);

  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, cube_vertices, 0, sizeof(uniform Vec3fa  ), NUM_VERTICES);
  uniform Vec3fa* uniform vtx = (uniform Vec3fa* uniform) rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, sizeof(uniform Vec3fa), NUM_VERTICES);
  for (uniform unsigned int i=0; i<NUM_VERTICES; i++) vtx[i] = make_Vec3fa(cube_vertices[i][0]+pos.x,cube_vertices[i][1]+pos.y,cube_vertices[i][2]+pos.z);

  rtcSetGeometryVertexAttributeCount(geom,1);
  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX,            0, RTC_FORMAT_UINT3,  cube_tri_indices,   0, 3*sizeof(uniform unsigned int), NUM_TRI_INDICES/3);
  void* uniform indices = rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3*sizeof(uniform unsigned int), NUM_TRI_INDICES/3);
  memcpy(indices, cube_tri_indices, NUM_TRI_INDICES*sizeof(uniform unsigned int));
  
  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, RTC_FORMAT_FLOAT3, cube_vertex_colors, 0, sizeof(uniform Vec3fa),         NUM_VERTICES);
  void* uniform colors = rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, RTC_FORMAT_FLOAT3, sizeof(uniform Vec3fa), NUM_VERTICES);
  memcpy(colors, cube_vertex_colors, NUM_VERTICES*sizeof(uniform Vec3fa));

  rtcCommitGeometry(geom);
  uniform unsigned int geomID = rtcAttachGeometry(scene_i, geom);
  rtcReleaseGeometry(geom);
  return geomID;
}

/* adds a quad cube to the scene */
uniform unsigned int addQuadCube (RTCScene scene_i, const uniform Vec3f& pos)
{
  RTCGeometry geom = rtcNewGeometry(g_device, RTC_GEOMETRY_TYPE_QUAD);

  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, cube_vertices, 0, sizeof(uniform Vec3fa  ), NUM_VERTICES);
  uniform Vec3fa* uniform vtx = (uniform Vec3fa* uniform) rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, sizeof(uniform Vec3fa), NUM_VERTICES);
  for (uniform unsigned int i=0; i<NUM_VERTICES; i++) vtx[i] = make_Vec3fa(cube_vertices[i][0]+pos.x,cube_vertices[i][1]+pos.y,cube_vertices[i][2]+pos.z);

  rtcSetGeometryVertexAttributeCount(geom,1);
  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX,            0, RTC_FORMAT_UINT4,  cube_quad_indices,  0, 4*sizeof(uniform unsigned int), NUM_QUAD_INDICES/4);
  void* uniform indices = rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT4, 4*sizeof(uniform unsigned int), NUM_QUAD_INDICES/4);
  memcpy(indices, cube_quad_indices, NUM_QUAD_INDICES*sizeof(uniform unsigned int));
  
  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, RTC_FORMAT_FLOAT3, cube_vertex_colors, 0, sizeof(uniform Vec3fa),         NUM_VERTICES);
  void* uniform colors = rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, RTC_FORMAT_FLOAT3, sizeof(uniform Vec3fa), NUM_VERTICES);
  memcpy(colors, cube_vertex_colors, NUM_VERTICES*sizeof(uniform Vec3fa));
  
  rtcCommitGeometry(geom);
  uniform unsigned int geomID = rtcAttachGeometry(scene_i, geom);
  rtcReleaseGeometry(geom);
  return geomID;
}

/* add curve geometry */
uniform unsigned int addCurve (RTCScene scene, const uniform Vec3f& pos)
{
  RTCGeometry geom = rtcNewGeometry (g_device, RTC_GEOMETRY_TYPE_ROUND_BEZIER_CURVE);

  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, hair_vertices, 0, sizeof(uniform Vec3fa), NUM_HAIR_VERTICES);
  uniform Vec3ff* uniform vtx = (uniform Vec3ff* uniform) rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT4, sizeof(uniform Vec3ff), NUM_HAIR_VERTICES);
  for (uniform unsigned int i=0; i<NUM_HAIR_VERTICES; i++) {
    vtx[i].x = hair_vertices[i][0]+pos.x;
    vtx[i].y = hair_vertices[i][1]+pos.y;
    vtx[i].z = hair_vertices[i][2]+pos.z;
    vtx[i].w = hair_vertices[i][3];
  }

  rtcSetGeometryVertexAttributeCount(geom,1);
  
  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX,            0, RTC_FORMAT_UINT,   hair_indices,       0, sizeof(uniform unsigned int), 1);
  void* uniform indices = rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX,            0, RTC_FORMAT_UINT,   sizeof(uniform unsigned int), 1);
  memcpy(indices, hair_indices, 1*sizeof(uniform unsigned int));
  
  //rtcSetSharedGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, RTC_FORMAT_FLOAT3, hair_vertex_colors, 0, sizeof(uniform Vec3fa),       NUM_HAIR_VERTICES);
  void* uniform colors = rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, RTC_FORMAT_FLOAT3, sizeof(uniform Vec3fa), NUM_HAIR_VERTICES);
  memcpy(colors, hair_vertex_colors, NUM_HAIR_VERTICES*sizeof(uniform Vec3fa));

  rtcCommitGeometry(geom);
  uniform unsigned int geomID = rtcAttachGeometry(scene, geom);
  rtcReleaseGeometry(geom);
  return geomID;
}

/* adds a ground plane to the scene */
uniform unsigned int addGroundPlane (RTCScene scene_i)
{
  /* create a triangulated plane with 2 triangles and 4 vertices */
  RTCGeometry geom = rtcNewGeometry (g_device, RTC_GEOMETRY_TYPE_TRIANGLE);

  /* set vertices */
  uniform Vertex* uniform vertices = (uniform Vertex* uniform) rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, sizeof(uniform Vertex), 4);
  vertices[0].x = -10; vertices[0].y = -2; vertices[0].z = -10;
  vertices[1].x = -10; vertices[1].y = -2; vertices[1].z = +10;
  vertices[2].x = +10; vertices[2].y = -2; vertices[2].z = -10;
  vertices[3].x = +10; vertices[3].y = -2; vertices[3].z = +10;

  /* set triangles */
  uniform Triangle* uniform triangles = (uniform Triangle* uniform) rtcSetNewGeometryBuffer(geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, sizeof(uniform Triangle), 2);
  triangles[0].v0 = 0; triangles[0].v1 = 1; triangles[0].v2 = 2;
  triangles[1].v0 = 1; triangles[1].v1 = 3; triangles[1].v2 = 2;

  rtcCommitGeometry(geom);
  uniform unsigned int geomID = rtcAttachGeometry(scene_i, geom);
  rtcReleaseGeometry(geom);
  return geomID;
}

/* called by the C++ code for initialization */
export void device_init (uniform int8* uniform cfg)
{
   /* create scene */
  TutorialData_Constructor(&data);
  g_scene = data.scene = rtcNewScene(g_device);

  /* add ground plane */
  addGroundPlane(g_scene);

  /* add cubes */
  addCurve(g_scene,make_Vec3f(4.0f,-1.0f,-3.5f));
  //quadCubeID = addQuadSubdivCube(g_scene,make_Vec3f(4.0f,0.0f,0.0f));
  //triCubeID  = addTriangleSubdivCube(g_scene,make_Vec3f(4.0f,0.0f,3.5f));
  addTriangleCube(g_scene,make_Vec3f(0.0f,0.0f,-3.0f));
  addQuadCube(g_scene,make_Vec3f(0.0f,0.0f,3.0f));

  /* commit changes to scene */
  rtcCommitScene (g_scene);
}

/* task that renders a single screen tile */
Vec3f renderPixel(const uniform TutorialData& data, float x, float y, const uniform ISPCCamera& camera, uniform RayStats& stats)
{
  /* initialize ray */
  Ray ray = make_Ray(make_Vec3f(camera.xfm.p), make_Vec3f(normalize(x*camera.xfm.l.vx + y*camera.xfm.l.vy + camera.xfm.l.vz)), 0.0f, inf);

  /* intersect ray with scene */
  rtcIntersectV(data.scene,RTCRayHit_(ray));
  RayStats_addRay(stats);

  /* shade pixels */
  Vec3f color = make_Vec3f(0.0f);
  if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
  {
    /* interpolate diffuse color */
    Vec3f diffuse = make_Vec3f(1.0f,0.0f,0.0f);
    if (ray.geomID > 0)
    {
      foreach_unique (geomID in ray.geomID) {
        rtcInterpolateV0(rtcGetGeometry(data.scene,geomID),ray.primID,ray.u,ray.v,RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,0,&diffuse.x,3);
      }
      //return diffuse;
      diffuse = 0.5f*diffuse;
    }

    /* calculate smooth shading normal */
    Vec3f Ng = ray.Ng;
    /*if (ray.geomID == 2 || ray.geomID == 3) {
      Vec3f dPdu,dPdv;
      foreach_unique (geomID in ray.geomID) {
        rtcInterpolateV1(rtcGetGeometry(data.scene,geomID),ray.primID,ray.u,ray.v,RTC_BUFFER_TYPE_VERTEX,0,NULL,&dPdu.x,&dPdv.x,3);
      }
      //return dPdu;
      Ng = cross(dPdu,dPdv);
    }*/
    Ng = normalize(Ng);
    color = color + diffuse*0.5f;
    Vec3f lightDir = normalize(make_Vec3f(-1,-1,-1));

    /* initialize shadow ray */
    Ray shadow = make_Ray(ray.org + ray.tfar*ray.dir, neg(lightDir), 0.001f, inf);

    /* trace shadow ray */
    rtcOccludedV(data.scene,RTCRay_(shadow));
    RayStats_addShadowRay(stats);

    /* add light contribution */
    if (shadow.tfar >= 0.0f) {
      Vec3f r = normalize(reflect(ray.dir,Ng));
      float s = pow(clamp(dot(r,lightDir),0.0f,1.0f),10.0f);
      float d = clamp(-dot(lightDir,Ng),0.0f,1.0f);
      color = color + diffuse*d + 0.5f*make_Vec3f(s);
    }
  }
  return color;
}

void renderPixelStandard(const uniform TutorialData& data,
                         int x, int y,
                         uniform int* uniform pixels,
                         const uniform unsigned int width,
                         const uniform unsigned int height,
                         const uniform float time,
                         const uniform ISPCCamera& camera,
                         uniform RayStats& stats)
{
  /* calculate pixel color */
  Vec3f color = renderPixel(data,(float)x,(float)y,camera,stats);
  
  /* write color to framebuffer */
  unsigned int r = (unsigned int) (255.0f * clamp(color.x,0.0f,1.0f));
  unsigned int g = (unsigned int) (255.0f * clamp(color.y,0.0f,1.0f));
  unsigned int b = (unsigned int) (255.0f * clamp(color.z,0.0f,1.0f));
  pixels[y*width+x] = (b << 16) + (g << 8) + r;
}

/* task that renders a single screen tile */
task void renderTileTask(uniform int* uniform pixels,
                         const uniform unsigned int width,
                         const uniform unsigned int height,
                         const uniform float time,
                         const uniform ISPCCamera& camera,
                         const uniform int numTilesX,
                         const uniform int numTilesY)
{
  const uniform unsigned int tileY = taskIndex / numTilesX;
  const uniform unsigned int tileX = taskIndex - tileY * numTilesX;
  const uniform unsigned int x0 = tileX * TILE_SIZE_X;
  const uniform unsigned int x1 = min(x0+TILE_SIZE_X,width);
  const uniform unsigned int y0 = tileY * TILE_SIZE_Y;
  const uniform unsigned int y1 = min(y0+TILE_SIZE_Y,height);

  foreach_tiled (y = y0 ... y1, x = x0 ... x1)
  {
    renderPixelStandard(data,x,y,pixels,width,height,time,camera,g_stats[threadIndex]);
  }
}

export void renderFrameStandard (uniform int* uniform pixels,
                          const uniform unsigned int width,
                          const uniform unsigned int height,
                          const uniform float time,
                          const uniform ISPCCamera& camera)
{
#if defined(EMBREE_SYCL_TUTORIAL) && !defined(EMBREE_SYCL_RT_SIMULATION)
  TutorialData ldata = data;
  sycl::event event = global_gpu_queue->submit([=](sycl::handler& cgh){
    const sycl::nd_range<2> nd_range = make_nd_range(height,width);
    cgh.parallel_for(nd_range,[=](sycl::nd_item<2> item) {
      const unsigned int x = item.get_global_id(1); if (x >= width ) return;
      const unsigned int y = item.get_global_id(0); if (y >= height) return;
      RayStats stats;
      renderPixelStandard(ldata,x,y,pixels,width,height,time,camera,stats);
    });
  });
  global_gpu_queue->wait_and_throw();

  const auto t0 = event.template get_profiling_info<sycl::info::event_profiling::command_start>();
  const auto t1 = event.template get_profiling_info<sycl::info::event_profiling::command_end>();
  const double dt = (t1-t0)*1E-9;
  ((ISPCCamera*)&camera)->render_time = dt;
  
#else
  const uniform int numTilesX = (width +TILE_SIZE_X-1)/TILE_SIZE_X;
  const uniform int numTilesY = (height+TILE_SIZE_Y-1)/TILE_SIZE_Y;
  launch[numTilesX*numTilesY] renderTileTask(pixels,width,height,time,camera,numTilesX,numTilesY); sync;
#endif
}

/* called by the C++ code to render */
export void device_render (uniform int* uniform pixels,
                           const uniform unsigned int width,
                           const uniform unsigned int height,
                           const uniform float time,
                           const uniform ISPCCamera& camera)
{
#if !defined(FORCE_FIXED_EDGE_TESSELLATION)
  //setQuadSubdivCubeLevels (rtcGetGeometry(g_scene, quadCubeID), camera.xfm.p);
  //setTriangleSubdivCubeLevels (rtcGetGeometry(g_scene, triCubeID), camera.xfm.p);
#endif

  rtcCommitScene(g_scene);
}

/* called by the C++ code for cleanup */
export void device_cleanup ()
{
  TutorialData_Destructor(&data);
}

export void device_gui ()
{
}

