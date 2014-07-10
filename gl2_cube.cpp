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

#include "Cube.h"
#include "Matrix.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <utils/Timers.h>
#include <ui/FramebufferNativeWindow.h>
#include <ui/GraphicBuffer.h>
#include "EGLUtils.h"

using namespace android;

static void printGLString(const char *name, GLenum s) 
{
  const char *v = (const char *) glGetString(s);
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

static const char gVertexShader[] = "attribute vec4 av4position;\n"
    "attribute vec3 av3colour;\n"
    "uniform mat4 mvp;\n"
    "varying vec3 vv3colour;\n"
    "void main() {\n"
    "  vv3colour = av3colour;\n"
    "  gl_Position = mvp * av4position;\n"
    "}\n";

static const char gFragmentShader[] = "#extension GL_OES_EGL_image_external : require\n"
    "precision lowp float;\n"
    "varying vec3 vv3colour;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(vv3colour, 1.0);\n"
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
GLint  iLocPosition;
GLint  iLocColor;
GLint  iLocMVP;

bool setupGraphics(int w, int h) 
{
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
  iLocPosition = glGetAttribLocation(programID, "av4position");
  fprintf(stderr, "glGetAttribLocation(\"av4position\") = %d\n", iLocPosition);

  /* Fill colors. */
  iLocColor = glGetAttribLocation(programID, "av3colour");
  fprintf(stderr, "glGetAttribLocation(\"av3colour\") = %d\n", iLocColor);

  iLocMVP = glGetUniformLocation(programID, "mvp");
  fprintf(stderr, "glGetUniformLocation(\"mvp\") = %d\n", iLocMVP);

  glEnable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST);

  /* Set clear screen color. */
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  checkGlError("glClearColor");
  return true;
}

void renderFrame(int w, int h) 
{
  glUseProgram(programID);
  checkGlError("glUseProgram");

  glEnableVertexAttribArray(iLocPosition);
  checkGlError("glEnableVertexAttribArray");
  glEnableVertexAttribArray(iLocColor);
  checkGlError("glEnableVertexAttribArray");

  /* Populate attributes for position, color and texture coordinates etc. */
  glVertexAttribPointer(iLocPosition, 3, GL_FLOAT, GL_FALSE, 0, vertices);
  checkGlError("glVertexAttribPointer");
  glVertexAttribPointer(iLocColor, 3, GL_FLOAT, GL_FALSE, 0, colors);
  checkGlError("glVertexAttribPointer");

  static float angleX = 0, angleY = 0, angleZ = 0;
  /*
   * Do some rotation with Euler angles. It is not a fixed axis as
   * quaternions would be, but the effect is nice.
   */
  Matrix modelView = Matrix::createRotationX(angleX);
  Matrix rotation = Matrix::createRotationY(angleY);

  modelView = rotation * modelView;

  rotation = Matrix::createRotationZ(angleZ);

  modelView = rotation * modelView;

  /* Pull the camera back from the cube */
  modelView[14] -= 2.5;

  Matrix perspective = Matrix::matrixPerspective(45.0f, w/(float)h, 0.01f, 100.0f);
  Matrix modelViewPerspective = perspective * modelView;

  glUniformMatrix4fv(iLocMVP, 1, GL_FALSE, modelViewPerspective.getAsArray());
  checkGlError("glUniformMatrix4fv");

  /* Update cube's rotation angles for animating. */
  angleX += 3;
  angleY += 2;
  angleZ += 1;

  if(angleX >= 360) angleX -= 360;
  if(angleX < 0) angleX += 360;
  if(angleY >= 360) angleY -= 360;
  if(angleY < 0) angleY += 360;
  if(angleZ >= 360) angleZ -= 360;
  if(angleZ < 0) angleZ += 360;

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  checkGlError("glClear");
  glDrawArrays(GL_TRIANGLES, 0, 36);
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
