// ======================================================================== //
// Copyright 2009-2017 Intel Corporation                                    //
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

#include "../common/math/random_sampler.isph"
#include "../common/math/sampling.isph"
#include "../common/tutorial/tutorial_device.isph"
#include "../common/tutorial/scene_device.h"

#define USE_INTERFACE 0 // 0 = stream, 1 = single rays/packets, 2 = single rays/packets using stream interface
#define AMBIENT_OCCLUSION_SAMPLES 64
//#define rtcOccluded rtcIntersect
//#define rtcOccludedVM rtcIntersectVM

#define SIMPLE_SHADING 1
#define SHADING 0
#define MSAA 1

extern uniform ISPCScene* uniform g_ispc_scene;

/* scene data */
RTCDevice g_device = NULL;
RTCScene g_scene = NULL;

RTCScene convertScene(uniform ISPCScene* uniform scene_in)
{
  uniform int scene_flags = RTC_SCENE_STATIC | RTC_SCENE_INCOHERENT | RTC_SCENE_HIGH_QUALITY;
  uniform int scene_aflags = RTC_INTERSECT_UNIFORM | RTC_INTERSECT_VARYING | RTC_INTERSECT_STREAM | RTC_INTERPOLATE;
  RTCScene scene_out = ConvertScene(g_device, scene_in,(RTCSceneFlags)scene_flags, (RTCAlgorithmFlags) scene_aflags, RTC_GEOMETRY_STATIC);
  return scene_out;
}

/* renders a single pixel casting with ambient occlusion */
Vec3f ambientOcclusionShading(int x, int y, RTCRay& ray, uniform RayStats& stats)
{
  RTCRay rays[AMBIENT_OCCLUSION_SAMPLES];

  Vec3f Ng = normalize(ray.Ng);
  if (dot(ray.dir,Ng) > 0.0f) Ng = neg(Ng);

  Vec3f col = make_Vec3f(min(1.0f,0.3f+0.8f*abs(dot(Ng,normalize(ray.dir)))));

  /* calculate hit point */
  float intensity = 0;
  Vec3f hitPos = ray.org + ray.tfar * ray.dir;

  RandomSampler sampler;
  RandomSampler_init(sampler,x,y,0);

  /* enable only valid rays */
  for (uniform int i=0; i<AMBIENT_OCCLUSION_SAMPLES; i++)
  {
    /* sample random direction */
    Vec2f s = RandomSampler_get2D(sampler);
    Sample3f dir;
    dir.v = cosineSampleHemisphere(s);
    dir.pdf = cosineSampleHemispherePDF(dir.v);
    dir.v = frame(Ng) * dir.v;

    /* initialize shadow ray */
    RTCRay& shadow = rays[i];
    shadow.org = hitPos;
    shadow.dir = dir.v;
    bool mask = __mask; unmasked { // invalidate inactive rays
      shadow.tnear = mask ? 0.001f       : (float)(pos_inf);
      shadow.tfar  = mask ? (float)(inf) : (float)(neg_inf);
    }
    shadow.geomID = RTC_INVALID_GEOMETRY_ID;
    shadow.primID = RTC_INVALID_GEOMETRY_ID;
    shadow.mask = -1;
    shadow.time = 0;
    RayStats_addShadowRay(stats);
  }

  uniform RTCIntersectContext context;
  context.flags = g_iflags_incoherent;

  /* trace occlusion rays */
#if USE_INTERFACE == 0
  rtcOccludedVM(g_scene,&context,rays,AMBIENT_OCCLUSION_SAMPLES,sizeof(RTCRay));
#elif USE_INTERFACE == 1
  for (uniform size_t i=0; i<AMBIENT_OCCLUSION_SAMPLES; i++)
    rtcOccluded(g_scene,rays[i]);
#else
  for (uniform size_t i=0; i<AMBIENT_OCCLUSION_SAMPLES; i++)
    rtcOccludedVM(g_scene,&context,&rays[i],1,sizeof(RTCRay));
#endif

  /* accumulate illumination */
  for (uniform int i=0; i<AMBIENT_OCCLUSION_SAMPLES; i++) {
    if (rays[i].geomID == RTC_INVALID_GEOMETRY_ID)
      intensity += 1.0f;
  }

  /* shade pixel */
  return col * (intensity/AMBIENT_OCCLUSION_SAMPLES);
}

/* renders a single screen tile */
void renderTileStandard(uniform int taskIndex,
                        uniform int threadIndex,
                        uniform int* uniform pixels,
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

  uniform RayStats& stats = g_stats[threadIndex];

  RTCRay rays[TILE_SIZE_X*TILE_SIZE_Y];

  /* generate stream of primary rays */
  uniform int N = 0;
  foreach_tiled (y = y0 ... y1, x = x0 ... x1)
  {
    /* ISPC workaround for mask == 0 */
    if (all(__mask == 0)) continue;

    RandomSampler sampler;
    RandomSampler_init(sampler, x, y, 0);

    /* initialize ray */
    RTCRay& ray = rays[N++];
    ray.org = make_Vec3f(camera.xfm.p);
    ray.dir = make_Vec3f(normalize((float)x*camera.xfm.l.vx + (float)y*camera.xfm.l.vy + camera.xfm.l.vz));
    bool mask = __mask; unmasked { // invalidates inactive rays
      ray.tnear = mask ? 0.0f         : (float)(pos_inf);
      ray.tfar  = mask ? (float)(inf) : (float)(neg_inf);
    }
    ray.geomID = RTC_INVALID_GEOMETRY_ID;
    ray.primID = RTC_INVALID_GEOMETRY_ID;
    ray.mask = -1;
    ray.time = RandomSampler_get1D(sampler);
    RayStats_addRay(stats);
  }

  uniform RTCIntersectContext context;
  context.flags = g_iflags_coherent;

  /* trace stream of rays */
#if USE_INTERFACE == 0
  rtcIntersectVM(g_scene,&context,rays,N,sizeof(RTCRay));
#elif USE_INTERFACE == 1
  for (uniform size_t i=0; i<N; i++)
    rtcIntersectEx(g_scene,&context,rays[i]);
#else
  for (uniform size_t i=0; i<N; i++)
    rtcIntersectVM(g_scene,&context,&rays[i],1,sizeof(RTCRay));
#endif

#if SHADING == 1
  /* shade stream of rays */
  N = 0;
  foreach_tiled (y = y0 ... y1, x = x0 ... x1)
  {
    /* ISPC workaround for mask == 0 */
    if (all(__mask == 0)) continue;
    RTCRay& ray = rays[N++];

    /* eyelight shading */
    Vec3f color = make_Vec3f(0.0f);
    if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
#if SIMPLE_SHADING == 1
      color = make_Vec3f(abs(dot(ray.dir,normalize(ray.Ng))));
#else
      color = ambientOcclusionShading(x,y,ray,g_stats[threadIndex]);
#endif

    /* write color to framebuffer */
    unsigned int r = (unsigned int) (255.0f * clamp(color.x,0.0f,1.0f));
    unsigned int g = (unsigned int) (255.0f * clamp(color.y,0.0f,1.0f));
    unsigned int b = (unsigned int) (255.0f * clamp(color.z,0.0f,1.0f));
    pixels[y*width+x] = (b << 16) + (g << 8) + r;
  }
#endif
}

const uniform float sampleX[16] = { 0,0.5,0.25,0.75,0.125,0.625,0.375,0.875,0.0625,0.5625,0.3125,0.8125,0.1875,0.6875,0.4375,0.9375 };

const uniform float sampleY[16] = { 0,0.333333,0.666667,0.111111,0.444444,0.777778,0.222222,0.555556,0.888889,0.037037,0.37037,0.703704,0.148148,0.481482,0.814815,0.259259 };

void renderTileMSAA(uniform int taskIndex,
                            uniform int threadIndex,
                            uniform int* uniform pixels,
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
  const uniform float scale = 1.0f / (float)programCount;

  uniform RayStats& stats = g_stats[threadIndex];

  uniform RTCIntersectContext context;
  context.flags = g_iflags_coherent;

#define MAX_RAY_PACKETS TILE_SIZE_X*TILE_SIZE_Y

  unsigned int px[MAX_RAY_PACKETS];
  unsigned int py[MAX_RAY_PACKETS];
  RTCRay rays[MAX_RAY_PACKETS];

  /* generate stream of primary rays */
  uniform int N = 0;
  for (unsigned int y = y0; y < y1; y++)
    for (unsigned int x = x0; x < x1; x++)
    {
     // RandomSampler sampler;
     // RandomSampler_init(sampler, x, y, programIndex);
     px[N] = x;
     py[N] = y;

     varying float sx = sampleX[programIndex] + (float)x;
     varying float sy = sampleY[programIndex] + (float)y;

     /* initialize ray */
     RTCRay& ray = rays[N++];
     ray.org = make_Vec3f(camera.xfm.p);
     ray.dir = make_Vec3f(normalize(sx*camera.xfm.l.vx + sy*camera.xfm.l.vy + camera.xfm.l.vz));
     ray.tnear = 0.0f;
     ray.tfar  = (float)(inf);
     ray.geomID = RTC_INVALID_GEOMETRY_ID;
     ray.primID = RTC_INVALID_GEOMETRY_ID;
     ray.mask = -1;
     ray.time = 0.0f;
     RayStats_addRay(stats);

     if (N == MAX_RAY_PACKETS)
     {
  /* trace stream of rays */
#if USE_INTERFACE == 0
      rtcIntersectVM(g_scene,&context,rays,N,sizeof(RTCRay));
#elif USE_INTERFACE == 1
     for (uniform size_t i=0; i<N; i++)
       rtcIntersectEx(g_scene,&context,rays[i]);
#else
     for (uniform size_t i=0; i<N; i++)
       rtcIntersectVM(g_scene,&context,&rays[i],1,sizeof(RTCRay));
#endif

#if SHADING == 1
     /* shade stream of rays */
     for (uniform size_t i=0; i<N; i++)
       {
         RTCRay& ray = rays[i];
         Vec3f color = make_Vec3f(0.0f);
         if (ray.geomID != RTC_INVALID_GEOMETRY_ID)
          color = make_Vec3f(abs(dot(ray.dir,normalize(ray.Ng))));
         uniform Vec3f c;
         c.x = reduce_add(color.x) * scale;
         c.y = reduce_add(color.y) * scale;
         c.z = reduce_add(color.z) * scale;

         unsigned int r = (unsigned int) (255.0f * clamp(c.x,0.0f,1.0f));
         unsigned int g = (unsigned int) (255.0f * clamp(c.y,0.0f,1.0f));
         unsigned int b = (unsigned int) (255.0f * clamp(c.z,0.0f,1.0f));
         pixels[py[i]*width+px[i]] = (b << 16) + (g << 8) + r;
       }
#endif
     N = 0;
   }
 }
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
  renderTile(taskIndex,threadIndex,pixels,width,height,time,camera,numTilesX,numTilesY);
}

/* called by the C++ code for initialization */
export void device_init (uniform int8* uniform cfg)
{
  /* create new Embree device */
  g_device = rtcNewDevice(cfg);
  error_handler(NULL,rtcDeviceGetError(g_device));

  /* set error handler */
  rtcDeviceSetErrorFunction2(g_device,error_handler,NULL);

  /* create scene */
  g_scene = convertScene(g_ispc_scene);
  rtcCommit (g_scene);

  /* set render tile function to use */
  #if MSAA == 1
  renderTile = renderTileMSAA;
  #else
  renderTile = renderTileStandard;
  #endif

  key_pressed_handler = device_key_pressed_default;
}

/* called by the C++ code to render */
export void device_render (uniform int* uniform pixels,
                           const uniform unsigned int width,
                           const uniform unsigned int height,
                           const uniform float time,
                           const uniform ISPCCamera& camera)
{
  /* render image */
  const uniform int numTilesX = (width +TILE_SIZE_X-1)/TILE_SIZE_X;
  const uniform int numTilesY = (height+TILE_SIZE_Y-1)/TILE_SIZE_Y;
  launch[numTilesX*numTilesY] renderTileTask(pixels,width,height,time,camera,numTilesX,numTilesY); sync;
}

/* called by the C++ code for cleanup */
export void device_cleanup ()
{
  rtcDeleteScene (g_scene); g_scene = NULL;
  rtcDeleteDevice(g_device); g_device = NULL;
}
