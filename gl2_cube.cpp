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

static int xioctl( int fd,int request,void * arg ) 
{ 
  int r; 
  do
  {
    r = ioctl( fd, request, arg ); 
  }while( -1 == r && EINTR == errno ); 
  return r; 
}

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

/*
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
*/

static const char gVertexShader[] = 
    "attribute vec4 a_v4Position;\n"
    "attribute vec4 a_v4FillColor;\n"
    "attribute vec2 a_v2TexCoord;\n"
    "uniform mat4 u_m4Projection;\n"
    "uniform mat4 u_m4Modelview;\n"
    "varying vec4 v_v4FillColor;\n"
    "varying vec2 v_v2TexCoord;\n"
    "void main()\n"
    "{\n"
    "   v_v4FillColor = a_v4FillColor;\n"
    "   v_v2TexCoord = a_v2TexCoord;\n"
    "   gl_Position = u_m4Projection * u_m4Modelview * a_v4Position;\n"
    "}\n";

static const char gFragmentShader[] = 
    "precision mediump float;\n"
    "uniform sampler2D u_s2dTexture;\n"
    "uniform float u_fTex;\n"
    "varying vec4 v_v4FillColor;\n"
    "varying vec2 v_v2TexCoord;\n"
    "void main()\n"
    "{\n"
    "   vec4 v4Texel = texture2D(u_s2dTexture, v_v2TexCoord);\n"
    "   gl_FragColor = mix(v_v4FillColor, v4Texel, u_fTex);\n"
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

const int fbTexWidth    = 640;
const int fbTexHeight   = 240;
const int fbTexUsage    = GraphicBuffer::USAGE_HW_TEXTURE |
                          GraphicBuffer::USAGE_SW_WRITE_RARELY;
const int fbTexFormat   = HAL_PIXEL_FORMAT_RGB_565;
static sp<GraphicBuffer> fbTexBuffer;
static GLuint fbTex;

int                         fd, 
                            scrSize;
struct  fb_var_screeninfo   vInfo;
struct  fb_fix_screeninfo   fInfo; 
char*                       buf;
unsigned char              *pFbBuf;
unsigned char               color;

void fillFbTexture(EGLDisplay dpy, EGLContext context, bool flag )
{
  status_t err = fbTexBuffer->lock( GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)(&buf) );
  if (err != 0) 
  {
    fprintf( stderr, "fbTexBuffer->lock(...) failed: %d\n", err );
    return;
  }

  // Get variable screen information. 
  if( -1 == xioctl( fd, FBIOGET_VSCREENINFO, &vInfo ) ) 
  { 
    fprintf( stderr, "Error reading variable information.\n" ); 
  }
  memcpy( buf,
          pFbBuf + ( vInfo.yoffset * vInfo.xres * vInfo.bits_per_pixel / 8 ),
          vInfo.xres * vInfo.yres * vInfo.bits_per_pixel / 8);

  err = fbTexBuffer->unlock();
  if (err != 0) 
  {
    fprintf( stderr, "fbTexBuffer->unlock() failed: %d\n", err );
    return;
  }

  if( flag )
  {
  EGLClientBuffer clientBuffer = (EGLClientBuffer)fbTexBuffer->getNativeBuffer();
  EGLImageKHR     img = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                          clientBuffer, 0);
  checkEglError("eglCreateImageKHR");
  if (img == EGL_NO_IMAGE_KHR) 
  {
    return;
  }

  glGenTextures(1, &fbTex);
  checkGlError("glGenTextures");
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, fbTex);
  checkGlError("glBindTexture");
  glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);
  checkGlError("glEGLImageTargetTexture2DOES");

  eglDestroyImageKHR(dpy, img);
  checkGlError("eglDestroyImageKHR");
  }
}

void openFbDevice(void)
{
  fd = open( "/dev/graphics/fb0", O_RDONLY );
  if( fd < 0 ) 
  {
    fprintf( stderr, "could not open %s, %s\n", "/dev/graphics/fb0", strerror( errno ) );
    return;
  }

  // Get fixed screen information 
  if( -1 == xioctl( fd, FBIOGET_FSCREENINFO, &fInfo ) ) 
  { 
    printf("Error reading fixed information.\n"); 
  }

  // Get variable screen information. 
  if( -1 == xioctl( fd, FBIOGET_VSCREENINFO, &vInfo ) ) 
  { 
    fprintf( stderr, "Error reading variable information.\n" ); 
  } 
  scrSize = vInfo.xres_virtual * vInfo.yres_virtual * vInfo.bits_per_pixel / 8;
  fprintf( stderr, "Visible res:    %dx%d\n", vInfo.xres, vInfo.yres );
  fprintf( stderr, "Virtual res:    %dx%d\n", vInfo.xres_virtual, vInfo.yres_virtual );
  fprintf( stderr, "Offset  res:    %dx%d\n", vInfo.xoffset, vInfo.yoffset );
  fprintf( stderr, "Bits per pixel: %d\n", vInfo.bits_per_pixel );
  fprintf( stderr, "Red:   %d(%d)\n", vInfo.red.offset, vInfo.red.length );
  fprintf( stderr, "Green: %d(%d)\n", vInfo.green.offset, vInfo.green.length );
  fprintf( stderr, "Blue:  %d(%d)\n", vInfo.blue.offset, vInfo.blue.length );
  fprintf( stderr, "Alpha: %d(%d)\n", vInfo.transp.offset, vInfo.transp.length );

  // Map frame buffer device to memory.
  pFbBuf = ( unsigned char * )mmap( NULL, scrSize, PROT_READ, MAP_SHARED , fd, 0 ); 
  if( (int)pFbBuf == -1 ) 
  { 
    fprintf( stderr, "Error: failed to map framebuffer device to memory.\n" ); 
  }
}

void closeFbDevice(void)
{
  munmap( pFbBuf, scrSize );
  close(fd);
}


bool setupFbTexSurface(EGLDisplay dpy, EGLContext context) 
{
  fbTexBuffer = new GraphicBuffer( fbTexWidth, 
                                   fbTexHeight, 
                                   fbTexFormat,
                                   fbTexUsage);
  openFbDevice();
  fillFbTexture(dpy, context, true);

  return true;
}

#define FBO_WIDTH    256
#define FBO_HEIGHT   256

/* Shader variables. */
GLuint programID;
GLint iLocPosition = -1;
GLint iLocTextureMix = -1;
GLint iLocTexture = -1;
GLint iLocFillColor = -1;
GLint iLocTexCoord = -1;
GLint iLocProjection = -1;
GLint iLocModelview = -1;

/* Animation variables. */
static float angleX = 0;
static float angleY = 0;
static float angleZ = 0;
Matrix rotationX;
Matrix rotationY;
Matrix rotationZ;
Matrix translation;
Matrix modelView;
Matrix projection;
Matrix projectionFBO;

/* Framebuffer variables. */
GLuint iFBO = 0;
/* Application textures. */
GLuint iFBOTex = 0;

bool setupGraphics(int w, int h) 
{
  projection    = Matrix::matrixPerspective(45.0f, w/(float)h, 0.01f, 100.0f);
  projectionFBO = Matrix::matrixPerspective(45.0f, (FBO_WIDTH / (float)FBO_HEIGHT), 0.01f, 100.0f);
  translation   = Matrix::createTranslation(0.0f, 0.0f, -2.0f);

  /* Initialize OpenGL ES. */
  glEnable(GL_BLEND);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glEnable(GL_DEPTH_TEST);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glGenTextures(1, &iFBOTex);
  glBindTexture(GL_TEXTURE_2D, iFBOTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FBO_WIDTH, FBO_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  /* Initialize FBOs. */
  glGenFramebuffers(1, &iFBO);

  /* Render to framebuffer object. */
  /* Bind our framebuffer for rendering. */
  glBindFramebuffer(GL_FRAMEBUFFER, iFBO);

  /* Attach texture to the framebuffer. */
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, iFBOTex, 0);

  /* Check FBO is OK. */
  GLenum iResult = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if(iResult != GL_FRAMEBUFFER_COMPLETE)
  {
      fprintf(stderr,"Framebuffer incomplete at %s:%i\n", __FILE__, __LINE__);
      return false;
  }

  /* Unbind framebuffer. */
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

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

  /* Vertex positions. */
  iLocPosition = glGetAttribLocation(programID, "a_v4Position");
  if(iLocPosition == -1)
  {
      fprintf(stderr,"Attribute not found at %s:%i\n", __FILE__, __LINE__);
      return false;
  }
  glEnableVertexAttribArray(iLocPosition);

  /* Texture mix. */
  iLocTextureMix = glGetUniformLocation(programID, "u_fTex");
  if(iLocTextureMix == -1)
  {
    fprintf(stderr,"Warning: Uniform not found at %s:%i\n", __FILE__, __LINE__);
  }
  else 
  {
    glUniform1f(iLocTextureMix, 0.0);
  }

  /* Texture. */
  iLocTexture = glGetUniformLocation(programID, "u_s2dTexture");
  if(iLocTexture == -1)
  {
    fprintf(stderr,"Warning: Uniform not found at %s:%i\n", __FILE__, __LINE__);
  }
  else 
  {
    glUniform1i(iLocTexture, 0);
  }

  /* Vertex colors. */
  iLocFillColor = glGetAttribLocation(programID, "a_v4FillColor");
  if(iLocFillColor == -1)
  {
    fprintf(stderr,"Warning: Attribute not found at %s:%i\n", __FILE__, __LINE__);
  }
  else 
  {
    glEnableVertexAttribArray(iLocFillColor);
  }

  /* Texture coords. */
  iLocTexCoord = glGetAttribLocation(programID, "a_v2TexCoord");
  if(iLocTexCoord == -1)
  {
    fprintf(stderr,"Warning: Attribute not found at %s:%i\n", __FILE__, __LINE__);
  }
  else 
  {
    glEnableVertexAttribArray(iLocTexCoord);
  }

  /* Projection matrix. */
  iLocProjection = glGetUniformLocation(programID, "u_m4Projection");
  if(iLocProjection == -1)
  {
    fprintf(stderr,"Warning: Uniform not found at %s:%i\n", __FILE__, __LINE__);
  }
  else 
  {
    glUniformMatrix4fv(iLocProjection, 1, GL_FALSE, projection.getAsArray());
  }

  /* Modelview matrix. */
  iLocModelview = glGetUniformLocation(programID, "u_m4Modelview");
  fprintf(stderr, "glGetUniformLocation(\"u_m4Modelview\") = %d\n", iLocModelview);

  return true;
}

void renderFrame(int w, int h) 
{
  glUseProgram(programID);
  checkGlError("glUseProgram");

  glEnableVertexAttribArray(iLocPosition);
  checkGlError("glEnableVertexAttribArray: iLocPosition");
  glVertexAttribPointer(iLocPosition, 3, GL_FLOAT, GL_FALSE, 0, cubeVertices);
  checkGlError("glVertexAttribPointer: iLocPosition");

  glEnableVertexAttribArray(iLocFillColor);
  checkGlError("glEnableVertexAttribArray: iLocFillColor");
  glVertexAttribPointer(iLocFillColor, 4, GL_FLOAT, GL_FALSE, 0, cubeColors);
  checkGlError("glVertexAttribPointer: iLocFillColor");

  glEnableVertexAttribArray(iLocTexCoord);
  checkGlError("glEnableVertexAttribArray: iLocTexCoord");
  glVertexAttribPointer(iLocTexCoord, 2, GL_FLOAT, GL_FALSE, 0, cubeTextureCoordinates);
  checkGlError("glVertexAttribPointer: iLocTexCoord");

  /* Bind the FrameBuffer Object. */
  glBindFramebuffer(GL_FRAMEBUFFER, iFBO);

  /* Set the viewport according to the FBO's texture. */
  glViewport(0, 0, FBO_WIDTH, FBO_HEIGHT);

  /* Clear screen on FBO. */
  glClearColor(0.5f, 0.5f, 0.5f, 1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  /* Create rotation matrix specific to the FBO's cube. */
  rotationX = Matrix::createRotationX(-angleZ);
  rotationY = Matrix::createRotationY(-angleY);
  rotationZ = Matrix::createRotationZ(-angleX);

  /* Rotate about origin, then translate away from camera. */
  modelView = translation * rotationX;
  modelView = modelView * rotationY;
  modelView = modelView * rotationZ;

  /* Load FBO-specific projection and modelview matrices. */
  glUniformMatrix4fv(iLocModelview, 1, GL_FALSE, modelView.getAsArray());
  glUniformMatrix4fv(iLocProjection, 1, GL_FALSE, projectionFBO.getAsArray());

  /* The FBO cube doesn't get textured so zero the texture mix factor. */
  if(iLocTextureMix != -1)
  {
    glUniform1f(iLocTextureMix, 0.0);
  }

  /* Now draw the colored cube to the FrameBuffer Object. */
  glDrawElements(GL_TRIANGLE_STRIP, sizeof(cubeIndices) / sizeof(GLubyte), GL_UNSIGNED_BYTE, cubeIndices);
  checkGlError("glDrawElements: FBO");

  /* And unbind the FrameBuffer Object so subsequent drawing calls are to the EGL window surface. */
  glBindFramebuffer(GL_FRAMEBUFFER,0);

  /* Reset viewport to the EGL window surface's dimensions. */
  glViewport(0, 0, w, h);

  /* Clear the screen on the EGL surface. */
  glClearColor(0.0f, 0.0f, 1.0f, 1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  /* Construct different rotation for main cube. */
  rotationX = Matrix::createRotationX(angleX);
  rotationY = Matrix::createRotationY(angleY);
  rotationZ = Matrix::createRotationZ(angleZ);

  /* Rotate about origin, then translate away from camera. */
  modelView = translation * rotationX;
  modelView = modelView * rotationY;
  modelView = modelView * rotationZ;

  /* Load EGL window-specific projection and modelview matrices. */
  glUniformMatrix4fv(iLocModelview, 1, GL_FALSE, modelView.getAsArray());
  glUniformMatrix4fv(iLocProjection, 1, GL_FALSE, projection.getAsArray());

  /* For the main cube, we use texturing so set the texture mix factor to 1. */
  if(iLocTextureMix != -1)
  {
    glUniform1f(iLocTextureMix, 1.0);
  }

  /* Ensure the correct texture is bound to texture unit 0. */
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, fbTex);

  /* And draw the cube. */
  glDrawElements(GL_TRIANGLE_STRIP, sizeof(cubeIndices) / sizeof(GLubyte), GL_UNSIGNED_BYTE, cubeIndices);
  checkGlError("glDrawElements");

  /* Update cube's rotation angles for animating. */
  angleX += 0.15;
  angleY += 0.1;
  angleZ += 0.05;

  if(angleX >= 360) angleX -= 360;
  if(angleY >= 360) angleY -= 360;
  if(angleZ >= 360) angleZ -= 360;
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

  if(!setupFbTexSurface(dpy, context)) 
  {
    fprintf(stderr, "Could not set up texture surface.\n");
    return 1;
  }

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
    fillFbTexture(dpy, context, false);
  }
  return 0;
}
