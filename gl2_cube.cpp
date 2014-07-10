/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <fcntl.h>  
#include <unistd.h> 
#include <errno.h> 
#include <sys/resource.h>
#include <sys/ioctl.h> 
#include <sys/mman.h>
#include <linux/fb.h> 

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <utils/Timers.h>
#include <ui/FramebufferNativeWindow.h>
#include <ui/GraphicBuffer.h>
#include "EGLUtils.h"

using namespace android;

/* Simple triangle. */
const float triangleVertices[] =
{
     0.0f,  0.5f, 0.0f,
    -0.5f, -0.5f, 0.0f,
     0.5f, -0.5f, 0.0f,
};

/* Per corner colors for the triangle (Red, Green, Green). */
const float triangleColors[] =
{
    1.0, 0.0, 0.0, 1.0,
    0.0, 1.0, 0.0, 1.0,
    0.0, 1.0, 0.0, 1.0,
};

static void printGLString(const char *name, GLenum s) 
{
  // fprintf(stderr, "printGLString %s, %d\n", name, s);
  const char *v = (const char *) glGetString(s);
  // int error = glGetError();
  // fprintf(stderr, "glGetError() = %d, result of glGetString = %x\n", error,
  //        (unsigned int) v);
  // if ((v < (const char*) 0) || (v > (const char*) 0x10000))
  //    fprintf(stderr, "GL %s = %s\n", name, v);
  // else
  //    fprintf(stderr, "GL %s = (null) 0x%08x\n", name, (unsigned int) v);
  fprintf(stderr, "GL %s = %s\n", name, v);
}

static void checkEglError(const char* op, EGLBoolean returnVal = EGL_TRUE) 
{
  if (returnVal != EGL_TRUE) 
  {
    fprintf(stderr, "%s() returned %d\n", op, returnVal);
  }

  for(EGLint error = eglGetError(); 
      error != EGL_SUCCESS; 
      error = eglGetError()) 
  {
    fprintf(stderr, "after %s() eglError %s (0x%x)\n",op, 
                                                      EGLUtils::strerror(error),
                                                      error);
  }
}

static void checkGlError(const char* op) 
{
  for(GLint error = glGetError(); 
      error; 
      error = glGetError()) 
  {
    fprintf(stderr, "after %s() glError (0x%x)\n", op, error);
  }
}

static const char gVertexShader[] = "attribute vec4 a_v4Position;\n"
    "attribute vec4 a_v4FillColor;\n"
    "varying vec4 v_v4FillColor;\n"
    "void main() {\n"
    "  v_v4FillColor = a_v4FillColor;\n"
    "  gl_Position = a_v4Position;\n"
    "}\n";

static const char gFragmentShader[] = "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "varying vec4 v_v4FillColor;\n"
    "void main() {\n"
    "  gl_FragColor = v_v4FillColor;\n"
    "}\n";

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    fprintf(stderr, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
            } else {
                fprintf(stderr, "Guessing at GL_INFO_LOG_LENGTH size\n");
                char* buf = (char*) malloc(0x1000);
                if (buf) {
                    glGetShaderInfoLog(shader, 0x1000, NULL, buf);
                    fprintf(stderr, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
            }
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}


/* Shader variables. */
GLuint programID;
GLint  iLocPosition  = -1;
GLint  iLocFillColor = -1;

bool setupGraphics(int w, int h) 
{
  glEnable(GL_DEPTH_TEST);
  checkGlError("glEnable");
  glDepthFunc(GL_LEQUAL);
  checkGlError("glDepthFunc");

  /* Initialize OpenGL ES. */
  glEnable(GL_BLEND);
  checkGlError("glEnable");
  /* Should do: src * (src alpha) + dest * (1-src alpha). */
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  checkGlError("glBlendFunc");


  programID = glCreateProgram();
  if(programID == 0)
  {
    fprintf(stderr, "glCreateProgram error.\n");
    return false;
  }

  GLuint vertexShaderID = loadShader( GL_VERTEX_SHADER, gVertexShader );
  if( !vertexShaderID ) 
  {
    fprintf(stderr, "vertexShader load error.\n");
    return false;
  }

  GLuint pixelShaderID = loadShader( GL_FRAGMENT_SHADER, gFragmentShader );
  if( !pixelShaderID )
  {
    fprintf(stderr, "pixelShader load error.\n");
    return false;
  }

  glAttachShader( programID, vertexShaderID );
  checkGlError("glAttachShader");
  glAttachShader( programID, pixelShaderID );
  checkGlError("glAttachShader");
  glLinkProgram( programID );
  checkGlError("glLinkProgram");
  glUseProgram( programID );
  checkGlError("glUseProgram");

  /* Positions. */
  iLocPosition = glGetAttribLocation(programID, "a_v4Position");
  fprintf(stderr, "glGetAttribLocation(\"a_v4Position\") = %d\n", iLocPosition);

  /* Fill colors. */
  iLocFillColor = glGetAttribLocation(programID, "a_v4FillColor");
  fprintf(stderr, "glGetAttribLocation(\"a_v4FillColor\") = %d\n", iLocFillColor);

  glViewport(0, 0, w, h);
  checkGlError("glViewport");

  /* Set clear screen color. */
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  checkGlError("glClearColor");
  glClearDepthf(1.0f);
  checkGlError("glClearDepthf");
  return true;
}

void renderFrame(int w, int h) 
{
  glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT );
  checkGlError("glClear");

  glUseProgram(programID);
  checkGlError("glUseProgram");

  /* Pass the triangle vertex positions to the shader */
  glVertexAttribPointer(iLocPosition, 3, GL_FLOAT, GL_FALSE, 0, triangleVertices);
  checkGlError("glVertexAttribPointer");
  glEnableVertexAttribArray(iLocPosition);
  checkGlError("glEnableVertexAttribArray");

  if(iLocFillColor != -1)
  {
      /* Pass the vertex colours to the shader */
      glVertexAttribPointer(iLocFillColor, 4, GL_FLOAT, GL_FALSE, 0, triangleColors);
      checkGlError("glVertexAttribPointer");
      glEnableVertexAttribArray(iLocFillColor);
      checkGlError("glEnableVertexAttribArray");
  }

  glDrawArrays(GL_TRIANGLES, 0, 3);
  checkGlError("glDrawArrays");
}

void printEGLConfiguration(EGLDisplay dpy, EGLConfig config) {

#define X(VAL) {VAL, #VAL}
  struct {EGLint attribute; const char* name;} names[] = {
  X(EGL_BUFFER_SIZE),
  X(EGL_ALPHA_SIZE),
  X(EGL_BLUE_SIZE),
  X(EGL_GREEN_SIZE),
  X(EGL_RED_SIZE),
  X(EGL_DEPTH_SIZE),
  X(EGL_STENCIL_SIZE),
  X(EGL_CONFIG_CAVEAT),
  X(EGL_CONFIG_ID),
  X(EGL_LEVEL),
  X(EGL_MAX_PBUFFER_HEIGHT),
  X(EGL_MAX_PBUFFER_PIXELS),
  X(EGL_MAX_PBUFFER_WIDTH),
  X(EGL_NATIVE_RENDERABLE),
  X(EGL_NATIVE_VISUAL_ID),
  X(EGL_NATIVE_VISUAL_TYPE),
  X(EGL_SAMPLES),
  X(EGL_SAMPLE_BUFFERS),
  X(EGL_SURFACE_TYPE),
  X(EGL_TRANSPARENT_TYPE),
  X(EGL_TRANSPARENT_RED_VALUE),
  X(EGL_TRANSPARENT_GREEN_VALUE),
  X(EGL_TRANSPARENT_BLUE_VALUE),
  X(EGL_BIND_TO_TEXTURE_RGB),
  X(EGL_BIND_TO_TEXTURE_RGBA),
  X(EGL_MIN_SWAP_INTERVAL),
  X(EGL_MAX_SWAP_INTERVAL),
  X(EGL_LUMINANCE_SIZE),
  X(EGL_ALPHA_MASK_SIZE),
  X(EGL_COLOR_BUFFER_TYPE),
  X(EGL_RENDERABLE_TYPE),
  X(EGL_CONFORMANT),
   };
#undef X

  for (size_t j = 0; j < sizeof(names) / sizeof(names[0]); j++) {
    EGLint value = -1;
    EGLint returnVal = eglGetConfigAttrib(dpy, config, names[j].attribute, &value);
    EGLint error = eglGetError();
    if (returnVal && error == EGL_SUCCESS) {
      fprintf(stderr," %s: ", names[j].name);
      fprintf(stderr,"%d (0x%x)\n", value, value);
    }
  }
  fprintf(stderr,"\n");
}

int main(int argc, char** argv) 
{
  EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  EGLint s_configAttribs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                               EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
                               EGL_NONE };
  EGLBoolean  returnValue;
  EGLConfig   myConfig = {0};
  EGLint      majorVersion;
  EGLint      minorVersion;
  EGLContext  context;
  EGLSurface  surface;
  EGLint      w, 
              h;
  EGLDisplay  dpy;

  checkEglError("<init>");
  dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  checkEglError("eglGetDisplay");
  if (dpy == EGL_NO_DISPLAY) 
  {
    fprintf(stderr,"eglGetDisplay returned EGL_NO_DISPLAY.\n");
    return 0;
  }

  returnValue = eglInitialize(dpy, &majorVersion, &minorVersion);
  checkEglError("eglInitialize", returnValue);
  fprintf(stderr, "EGL version %d.%d\n", majorVersion, minorVersion);
  if (returnValue != EGL_TRUE) 
  {
    fprintf(stderr,"eglInitialize failed\n");
    return 0;
  }

  EGLNativeWindowType window = android_createDisplaySurfaceEx("fb4");
  returnValue = EGLUtils::selectConfigForNativeWindow(dpy, s_configAttribs, window, &myConfig);
  if (returnValue) 
  {
    fprintf(stderr,"EGLUtils::selectConfigForNativeWindow() returned %d", returnValue);
    return 1;
  }

  checkEglError("EGLUtils::selectConfigForNativeWindow");

  fprintf(stderr,"Chose this configuration:\n");
  printEGLConfiguration(dpy, myConfig);

  surface = eglCreateWindowSurface(dpy, myConfig, window, NULL);
  checkEglError("eglCreateWindowSurface");
  if (surface == EGL_NO_SURFACE) 
  {
    fprintf(stderr,"gelCreateWindowSurface failed.\n");
    return 1;
  }

  context = eglCreateContext(dpy, myConfig, EGL_NO_CONTEXT, context_attribs);
  checkEglError("eglCreateContext");
  if (context == EGL_NO_CONTEXT) 
  {
    fprintf(stderr,"eglCreateContext failed\n");
    return 1;
  }
  returnValue = eglMakeCurrent(dpy, surface, surface, context);
  checkEglError("eglMakeCurrent", returnValue);
  if (returnValue != EGL_TRUE) 
  {
    return 1;
  }

  eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
  checkEglError("eglQuerySurface");
  eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
  checkEglError("eglQuerySurface");
  fprintf(stderr, "Window dimensions: %d x %d\n", w, h);

  printGLString("Version",    GL_VERSION);
  printGLString("Vendor",     GL_VENDOR);
  printGLString("Renderer",   GL_RENDERER);
  printGLString("Extensions", GL_EXTENSIONS);

  if(!setupGraphics(w, h)) 
  {
    fprintf(stderr, "Could not set up graphics.\n");
    return 1;
  }

  for (;;)
  {
    renderFrame(w, h);
    eglSwapBuffers(dpy, surface);
    checkEglError("eglSwapBuffers");
  }
  return 0;
}
