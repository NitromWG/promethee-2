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
#pragma once

#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <NCollection_Vec2.hxx>
#include <Standard_Version.hxx>
#include <V3d_View.hxx>

// Sous Linux, OpenCascade a besoin d'une connexion au serveur X : Xw_DisplayConnection depuis 7.7,
// Aspect_DisplayConnection auparavant.
#if !defined(__APPLE__) && !defined(_WIN32)
#define PROMETHEE_X11
#if defined(__has_include)
#if __has_include(<Xw_DisplayConnection.hxx>)
#include <Xw_DisplayConnection.hxx>
#define PROMETHEE_XW_DISPLAY
#endif
#endif
#endif
#ifndef PROMETHEE_XW_DISPLAY
typedef Aspect_DisplayConnection Xw_DisplayConnection;
#endif

class OpenGl_Context;

namespace occtqt {

// Fenêtre neutre qui connaît le facteur d'échelle de l'écran (écrans haute densité).
class FenetreNeutre : public Aspect_NeutralWindow {
public:
  double DevicePixelRatio() const override { return m_ratio; }
  void SetDevicePixelRatio(double r) { m_ratio = r; }
private:
  double m_ratio = 1.0;
};

Handle(OpenGl_Context) contexteGl(const Handle(V3d_View)& vue);
Aspect_Drawable fenetreNativeGl(Aspect_Drawable fenetre);
bool initialiserFenetre(const Handle(V3d_View)& vue, Aspect_Drawable fenetre, const NCollection_Vec2<int>& taille, double ratio);
bool initialiserTamponQt(const Handle(V3d_View)& vue);
void etatGlAvantOcct(const Handle(V3d_View)& vue);
void etatGlApresOcct(const Handle(V3d_View)& vue);

}  // namespace occtqt
