#ifndef XR_VIEW_GLSL
#define XR_VIEW_GLSL

#ifdef ANDROID_XR_MONO
#define XR_VIEW_INDEX 0
#else
#extension GL_EXT_multiview : enable
#define XR_VIEW_INDEX gl_ViewIndex
#endif

#endif