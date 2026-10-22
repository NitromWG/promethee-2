// Adapté de « occt-samples-qopenglwidget » (OcctGlTools), sous licence MIT :
// Copyright (c) 2023 Kirill Gavrilov
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Prométhée : intégration du visualiseur OpenCascade dans un QOpenGLWidget.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "OcctGlTools.hpp"

#include <cmath>

#include <Graphic3d_Camera.hxx>
#include <Message.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GlCore20.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_View.hxx>
#include <OpenGl_Window.hxx>

namespace occtqt {

namespace {
// Tampon d'image de Qt, enveloppé pour qu'OpenCascade dessine dedans (en sRGB).
class TamponQt : public OpenGl_FrameBuffer {
  DEFINE_STANDARD_RTTI_INLINE(TamponQt, OpenGl_FrameBuffer)
public:
  void BindBuffer(const Handle(OpenGl_Context)& ctx) override {
    OpenGl_FrameBuffer::BindBuffer(ctx);
    ctx->SetFrameBufferSRGB(true, false);
  }
  void BindDrawBuffer(const Handle(OpenGl_Context)& ctx) override {
    OpenGl_FrameBuffer::BindDrawBuffer(ctx);
    ctx->SetFrameBufferSRGB(true, false);
  }
  void BindReadBuffer(const Handle(OpenGl_Context)& ctx) override { OpenGl_FrameBuffer::BindReadBuffer(ctx); }
};
}  // namespace

Handle(OpenGl_Context) contexteGl(const Handle(V3d_View)& vue) {
  Handle(OpenGl_View) vueGl = Handle(OpenGl_View)::DownCast(vue->View());
  return vueGl->GlWindow()->GetGlContext();
}

Aspect_Drawable fenetreNativeGl([[maybe_unused]] Aspect_Drawable fenetre) {
#ifdef _WIN32
  // QOpenGLWidget dessine dans une fenêtre cachée : on reprend celle du contexte courant.
  return (Aspect_Drawable)WindowFromDC(wglGetCurrentDC());
#else
  return fenetre;
#endif
}

bool initialiserFenetre(const Handle(V3d_View)& vue, Aspect_Drawable fenetre, const NCollection_Vec2<int>& taille, double ratio) {
  const Aspect_Drawable native = fenetreNativeGl(fenetre);
  Handle(OpenGl_GraphicDriver) pilote = Handle(OpenGl_GraphicDriver)::DownCast(vue->Viewer()->Driver());
  Handle(OpenGl_Context) ctx = new OpenGl_Context();
  if (!ctx->Init(!pilote->Options().contextCompatible)) {
    Message::SendFail() << "OpenGl_Context ne parvient pas à reprendre le contexte OpenGL de Qt";
    return false;
  }
  Handle(FenetreNeutre) f = Handle(FenetreNeutre)::DownCast(vue->Window());
  if (f.IsNull()) {
    f = new FenetreNeutre();
    f->SetVirtual(true);
  }
  f->SetNativeHandle(native);
  f->SetSize(taille.x(), taille.y());
  f->SetDevicePixelRatio(ratio);
  vue->SetWindow(f, ctx->RenderingContext());
  vue->MustBeResized();
  vue->Invalidate();
  return true;
}

bool initialiserTamponQt(const Handle(V3d_View)& vue) {
  Handle(OpenGl_Context) ctx = contexteGl(vue);
  Handle(TamponQt) tampon = Handle(TamponQt)::DownCast(ctx->DefaultFrameBuffer());
  if (tampon.IsNull()) tampon = new TamponQt();
  if (!tampon->InitWrapper(ctx)) {
    Message::SendFail() << "Impossible d'envelopper le tampon d'image de Qt";
    return false;
  }
  ctx->SetDefaultFrameBuffer(Handle(OpenGl_FrameBuffer)());
  NCollection_Vec2<int> ancienne;
  const NCollection_Vec2<int> nouvelle = tampon->GetVPSize();
  Handle(FenetreNeutre) f = Handle(FenetreNeutre)::DownCast(vue->Window());
  f->Size(ancienne.x(), ancienne.y());
  if (nouvelle != ancienne) {
    f->SetSize(nouvelle.x(), nouvelle.y());
    vue->MustBeResized();
    vue->Invalidate();
  }
  // Une caméra recopiée (animation vers une vue, caméra gardée puis rendue) emporte le rapport largeur/hauteur
  // de la vue à son époque. Si la vue a changé de taille entre-temps (panneau de l'esquisse ouvert pendant
  // l'animation), l'image restait étirée jusqu'au prochain redimensionnement : on le remet à celui du tampon.
  if (nouvelle.x() > 0 && nouvelle.y() > 0) {
    const double rapport = static_cast<double>(nouvelle.x()) / nouvelle.y();
    if (std::abs(vue->Camera()->Aspect() - rapport) > 1e-9 * rapport) {
      vue->Camera()->SetAspect(rapport);
      vue->Invalidate();
    }
  }
  ctx->SetDefaultFrameBuffer(tampon);
  return true;
}

void etatGlAvantOcct(const Handle(V3d_View)& vue) {
  Handle(OpenGl_Context) ctx = contexteGl(vue);
  if (ctx.IsNull()) return;
  if (ctx->core20fwd != nullptr) ctx->core20fwd->glUseProgram(0);
  ctx->core11fwd->glBindTexture(GL_TEXTURE_2D, 0);
  ctx->core11fwd->glDisable(GL_BLEND);
  if (ctx->core11ffp != nullptr) {
    ctx->core11fwd->glDisable(GL_ALPHA_TEST);
    ctx->core11fwd->glDisable(GL_TEXTURE_2D);
  }
}

void etatGlApresOcct(const Handle(V3d_View)& vue) {
  Handle(OpenGl_Context) ctx = contexteGl(vue);
  if (ctx.IsNull()) return;
  ctx->core11fwd->glPixelStorei(GL_PACK_ALIGNMENT, 4);
  ctx->core11fwd->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  if (ctx->core15fwd != nullptr) ctx->core15fwd->glActiveTexture(GL_TEXTURE0);
}

}  // namespace occtqt
