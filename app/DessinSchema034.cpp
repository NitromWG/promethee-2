// Prométhée : dessin des objets du schéma de la 0.34 (lot L7) — formes importées ou dessinées (7.19), blocs de la
// hiérarchie et leurs ports (7.2, 7.3), tableaux et images (7.6), zones de règles (7.14), renvois des contacts sous
// les bobines (7.10), textes riches (7.6, 7.11) et fil d'Ariane de la hiérarchie (7.2).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>

#include <QByteArray>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include "DessinSchema.hpp"
#include "SignesCanevas.hpp"
#include "Theme.hpp"
#include "VueSchema.hpp"
#include "promethee/schema_avance.hpp"

using namespace prom::ecad;
using namespace dessin_schema;

namespace {
QColor couleurDe(const std::string& c, const QColor& defaut) {
  if (c.empty()) return defaut;
  const QColor q(QString::fromStdString(c));
  return q.isValid() ? q : defaut;
}
// Pointe de flèche en « b », venant de « a »
void fleche(QPainter& g, QPointF a, QPointF b, double taille) {
  const QPointF d = b - a;
  const double l = std::hypot(d.x(), d.y());
  if (l < 1e-6) return;
  const QPointF u = d / l, n(-u.y(), u.x());
  QPolygonF p;
  p << b << b - u * taille + n * taille * 0.4 << b - u * taille - n * taille * 0.4;
  g.save();
  g.setBrush(g.pen().color());
  g.drawPolygon(p);
  g.restore();
}
}  // namespace

void VueSchema::texteRiche(QPainter& g, QPointF p, const ConceptionElec& c, const std::string& source, double taille, const QColor& couleur, int alignement) {
  if (source.find_first_of("$@^_~[") == std::string::npos) {
    texte(g, p, QString::fromStdString(source), taille, alignement, couleur);
    return;
  }
  if (taille < 3) return;
  const TexteResolu r = resoudreTexte(source, c, m_feuille);
  QFont normal = g.font();
  normal.setPixelSize(std::max(1, static_cast<int>(taille)));
  QFont petit = normal;
  petit.setPixelSize(std::max(1, static_cast<int>(taille * 0.7)));
  // Largeur totale pour l'alignement
  double l = 0;
  for (const auto& m : r.morceaux) {
    const bool reduit = m.style == MorceauTexte::Style::Exposant || m.style == MorceauTexte::Style::Indice;
    l += QFontMetricsF(reduit ? petit : normal).horizontalAdvance(QString::fromStdString(m.texte));
  }
  double x = alignement < 0 ? p.x() : alignement > 0 ? p.x() - l : p.x() - l / 2;
  const QFontMetricsF mn(normal);
  const double base = p.y() + mn.ascent() / 2 - mn.descent() / 2;
  g.save();
  for (const auto& m : r.morceaux) {
    const QString t = QString::fromStdString(m.texte);
    const bool reduit = m.style == MorceauTexte::Style::Exposant || m.style == MorceauTexte::Style::Indice;
    const QFont& f = reduit ? petit : normal;
    g.setFont(f);
    // Une valeur héritée de la conception, que la feuille ne surcharge pas, est montrée un peu estompée
    QColor coul = m.style == MorceauTexte::Style::Lien ? QColor(0x1E, 0x88, 0xE5) : couleur;
    if (m.herite && !m_impression) coul.setAlpha(200);
    g.setPen(coul);
    const double y = m.style == MorceauTexte::Style::Exposant ? base - taille * 0.35 : m.style == MorceauTexte::Style::Indice ? base + taille * 0.2 : base;
    g.drawText(QPointF(x, y), t);
    const double lm = QFontMetricsF(f).horizontalAdvance(t);
    if (m.style == MorceauTexte::Style::Barre) g.drawLine(QPointF(x, base - QFontMetricsF(f).ascent() - 1), QPointF(x + lm, base - QFontMetricsF(f).ascent() - 1));
    if (m.style == MorceauTexte::Style::Lien) g.drawLine(QPointF(x, base + 2), QPointF(x + lm, base + 2));
    x += lm;
  }
  // Texte marqué (ERC_WARNING…) : un petit triangle orange devant
  if (r.avertissement && !m_impression) {
    const double t = taille * 0.8, x0 = (alignement < 0 ? p.x() : alignement > 0 ? p.x() - l : p.x() - l / 2) - t - 2;
    QPolygonF tri;
    tri << QPointF(x0 + t / 2, p.y() - t / 2) << QPointF(x0 + t, p.y() + t / 2) << QPointF(x0, p.y() + t / 2);
    g.setPen(Qt::NoPen);
    g.setBrush(r.avertissement == 2 ? QColor(0xE5, 0x39, 0x35) : QColor(0xFB, 0x8C, 0x00));
    g.drawPolygon(tri);
  }
  g.restore();
}

void VueSchema::dessinerObjets034(QPainter& g, const Feuille& f, const ConceptionElec& c) {
  const double trait = std::max(1.0, 0.15 * m_echelle);
  // Zones de règles et masques (7.14) : contour tireté, nom et propriétés
  for (const auto& z : f.zonesRegles) {
    if (z.contour.size() < 3) continue;
    QPolygonF poly;
    for (const Pt& p : z.contour) poly << ecran(p);
    g.save();
    g.setOpacity(opaciteObjet(z.uuid));
    g.setPen(QPen(z.masque ? QColor(0x75, 0x75, 0x75) : GLOBALE, trait, Qt::DashLine));
    g.setBrush(z.masque ? QBrush(alpha(QColor(0x75, 0x75, 0x75), 40), Qt::BDiagPattern) : QBrush(Qt::NoBrush));
    g.drawPolygon(poly);
    QString t = QString::fromStdString(z.nom.empty() ? std::string("Zone") : z.nom);
    for (const auto& [k, v] : z.proprietes) t += QStringLiteral(" · %1=%2").arg(QString::fromStdString(k), QString::fromStdString(v));
    if (z.masque) t += QStringLiteral(" · masque");
    texte(g, poly.boundingRect().topLeft() + QPointF(2, -0.9 * m_echelle), t, 1.2 * m_echelle, -1, z.masque ? QColor(0x75, 0x75, 0x75) : GLOBALE);
    if (!m_impression && m_selection.count(z.uuid)) signes::selectionBoite(g, poly.boundingRect(), theme().signesSchema().sel);
    g.restore();
  }
  // Formes, par calque puis par rang ; les calques masqués ne sont dessinés ni à l'écran ni au PDF
  for (const FormeSchema* x : formesRangees(f, m_impression)) {
    const Trace& t = x->trace;
    const QColor coul = couleurDe(t.couleur, ENCRE);
    const double ep = std::max(1.0, (t.epaisseur > 0 ? mm(t.epaisseur) : 0.15) * m_echelle);
    QPen pen(coul, ep, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (t.style == "tirets") pen.setStyle(Qt::DashLine);
    else if (t.style == "points") pen.setStyle(Qt::DotLine);
    else if (t.style == "mixte") pen.setStyle(Qt::DashDotLine);
    g.save();
    g.setOpacity(opaciteObjet(x->uuid));
    g.setPen(pen);
    g.setBrush(t.rempli ? QBrush(coul) : QBrush(Qt::NoBrush));
    QRectF boite;
    if (t.forme == Trace::Forme::Texte) {
      const QPointF p = ecran(t.points.empty() ? Pt{} : t.points[0]);
      texteRiche(g, p, c, t.texte, mm(t.taille) * m_echelle, coul, -1);
      boite = QRectF(p - QPointF(2, mm(t.taille) * m_echelle), QSizeF(40, mm(t.taille) * m_echelle * 2));
    } else if (t.forme == Trace::Forme::CadreTexte && t.points.size() >= 2) {
      boite = QRectF(ecran(t.points[0]), ecran(t.points[1])).normalized();
      g.drawRect(boite);
      QFont fo = g.font();
      fo.setPixelSize(std::max(1, static_cast<int>(mm(t.taille) * m_echelle)));
      g.setFont(fo);
      g.setPen(coul);
      g.drawText(boite.adjusted(3, 3, -3, -3), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, QString::fromStdString(texteAffiche(t.texte, c, m_feuille)));
    } else if (t.forme == Trace::Forme::Image) {
      const QPointF p = ecran(t.points.empty() ? Pt{} : t.points[0]);
      boite = QRectF(p, QSizeF(20 * m_echelle, 15 * m_echelle));
      g.setPen(QPen(coul, 1, Qt::DashLine));
      g.drawRect(boite);
      texte(g, boite.center(), QString::fromStdString(t.texte), 1.2 * m_echelle, 0, coul);
    } else {
      const std::vector<Pt> pts = contourTrace(t, 48);
      QPolygonF poly;
      for (const Pt& p : pts) poly << ecran(p);
      if (t.rempli && poly.size() >= 3) g.drawPolygon(poly);
      else g.drawPolyline(poly);
      if (poly.size() >= 2) {
        if (t.fleches & 1) fleche(g, poly[1], poly[0], 4 * ep);
        if (t.fleches & 2) fleche(g, poly[poly.size() - 2], poly.back(), 4 * ep);
      }
      boite = poly.boundingRect();
    }
    if (!m_impression && m_selection.count(x->uuid)) signes::selectionBoite(g, boite.adjusted(-3, -3, 3, 3), theme().signesSchema().sel);
    g.restore();
  }
  // Tableaux (7.6) : cellules en lignes, bordure au choix
  for (const auto& tab : f.tableaux) {
    g.save();
    g.setOpacity(opaciteObjet(tab.uuid));
    const double h = mm(tab.hauteurLigne) * m_echelle;
    const QPointF o = ecran(tab.p);
    double largeurTotale = 0;
    std::size_t colonnes = 0;
    for (const auto& l : tab.cellules) colonnes = std::max(colonnes, l.size());
    std::vector<double> largeurs(colonnes, 20 * m_echelle);
    for (std::size_t j = 0; j < colonnes && j < tab.largeurs.size(); ++j) largeurs[j] = mm(tab.largeurs[j]) * m_echelle;
    for (double w : largeurs) largeurTotale += w;
    g.setPen(QPen(ENCRE, trait));
    for (std::size_t i = 0; i < tab.cellules.size(); ++i) {
      double x = o.x();
      for (std::size_t j = 0; j < colonnes; ++j) {
        const QRectF r(x, o.y() + h * static_cast<double>(i), largeurs[j], h);
        if (tab.bordure) g.drawRect(r);
        if (j < tab.cellules[i].size()) {
          QFont fo = g.font();
          fo.setPixelSize(std::max(1, static_cast<int>(mm(tab.taille) * m_echelle)));
          fo.setBold(i == 0);
          g.setFont(fo);
          g.drawText(r.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(texteAffiche(tab.cellules[i][j], c, m_feuille)));
        }
        x += largeurs[j];
      }
    }
    if (!m_impression && m_selection.count(tab.uuid))
      signes::selectionBoite(g, QRectF(o, QSizeF(largeurTotale, h * static_cast<double>(tab.cellules.size()))).adjusted(-3, -3, 3, 3), theme().signesSchema().sel);
    g.restore();
  }
  // Images (7.6) : données PNG en base 64, à 96 points par pouce et à leur échelle
  for (const auto& im : f.images) {
    QImage image;
    if (!im.donnees.empty()) image.loadFromData(QByteArray::fromBase64(QByteArray::fromStdString(im.donnees)));
    const QPointF p = ecran(im.p);
    const double k = 25.4 / 96 * im.echelle * m_echelle;
    const QRectF r = image.isNull() ? QRectF(p, QSizeF(20 * m_echelle, 15 * m_echelle)) : QRectF(p, QSizeF(image.width() * k, image.height() * k));
    g.save();
    g.setOpacity(opaciteObjet(im.uuid));
    if (image.isNull()) {
      g.setPen(QPen(ENCRE, 1, Qt::DashLine));
      g.drawRect(r);
      texte(g, r.center(), QString::fromStdString(im.fichier.empty() ? std::string("image") : im.fichier), 1.2 * m_echelle, 0, ENCRE);
    } else
      g.drawImage(r, image);
    if (!m_impression && m_selection.count(im.uuid)) signes::selectionBoite(g, r.adjusted(-3, -3, 3, 3), theme().signesSchema().sel);
    g.restore();
  }
  // Blocs de la hiérarchie (7.2, 7.3) : cadre, nom de l'instance au-dessus, feuille ou fichier dessous, ports au bord
  for (const auto& b : f.blocs) {
    const QRectF r(ecran(b.position), ecran({b.position.x + b.largeur, b.position.y + b.hauteur}));
    g.save();
    g.setOpacity(opaciteObjet(b.uuid));
    g.setPen(QPen(couleurDe(b.couleur, ENCRE), std::max(1.0, 0.25 * m_echelle)));
    g.setBrush(alpha(couleurDe(b.couleur, FOND_CORPS), 60));
    g.drawRect(r.normalized());
    texte(g, r.normalized().topLeft() + QPointF(0, -1.0 * m_echelle), QString::fromStdString(b.nom), 1.5 * m_echelle, -1, ENCRE);
    std::string sous;
    for (const auto& x : c.feuilles)
      if (x.uuid == b.feuille) sous = x.nom;
    if (!b.fichier.empty()) sous = b.fichier;
    texte(g, r.normalized().bottomLeft() + QPointF(0, 1.0 * m_echelle), QStringLiteral("Feuille : %1").arg(QString::fromStdString(sous)), 1.1 * m_echelle, -1, GLOBALE);
    for (const auto& p : b.ports) {
      const QPointF q = ecran(p.p);
      const double h = 0.8 * m_echelle;
      QPolygonF forme;
      // Cartouche pointu vers l'intérieur pour une entrée, vers l'extérieur pour une sortie
      const double sens = p.cote == 1 ? -1.0 : 1.0;
      forme << q << q + QPointF(sens * h, -h) << q + QPointF(sens * 3 * h, -h) << q + QPointF(sens * 3 * h, h) << q + QPointF(sens * h, h);
      g.setPen(QPen(ENCRE, trait));
      g.setBrush(FOND_ETIQUETTE);
      g.drawPolygon(forme);
      texte(g, q + QPointF(sens * 3.6 * h, 0), QString::fromStdString(p.nom), 1.2 * m_echelle, sens > 0 ? -1 : 1, ENCRE);
    }
    if (!m_impression && m_selection.count(b.uuid)) signes::selectionBoite(g, r.normalized().adjusted(-3, -3, 3, 3), theme().signesSchema().sel);
    g.restore();
  }
}

void VueSchema::dessinerRenvois034(QPainter& g, const Feuille& f, const ConceptionElec& c) {
  // Renvois d'un composant à plusieurs unités réparties sur plusieurs pages (7.10) : sous la bobine, les contacts ;
  // sous chaque contact, la bobine. Rien quand toutes les unités sont sur cette page.
  bool plusieurs = false;
  std::map<std::string, std::set<std::size_t>> pages;
  for (std::size_t i = 0; i < c.feuilles.size(); ++i)
    for (const auto& s : c.feuilles[i].symboles)
      if (!s.composant.empty()) pages[s.composant].insert(i);
  for (const auto& s : f.symboles)
    if (!s.composant.empty() && pages[s.composant].size() > 1) plusieurs = true;
  if (!plusieurs) return;
  const auto renvois = renvoisComposants(c);
  for (const auto& s : f.symboles) {
    if (s.composant.empty() || pages[s.composant].size() < 2) continue;
    const auto it = renvois.find(s.uuid);
    if (it == renvois.end() || it->second.empty()) continue;
    const DefSymbole* d = c.symbole(s.symbole);
    if (!d) continue;
    const Boite b = boiteSymbole(s, *d);
    texte(g, ecran({(b.x0 + b.x1) / 2, b.y1}) + QPointF(0, 1.4 * m_echelle), QString::fromStdString(it->second), 1.2 * m_echelle, 0, GLOBALE);
  }
}

void VueSchema::dessinerFilAriane(QPainter& g) {
  m_zonesAriane.clear();
  const ConceptionElec* c = conception();
  if (!c || m_feuille < 0 || static_cast<std::size_t>(m_feuille) >= c->feuilles.size()) return;
  const auto chemin = filAriane(*c, static_cast<std::size_t>(m_feuille));
  if (chemin.size() < 2 && c->feuilles[static_cast<std::size_t>(m_feuille)].blocs.empty()) return;
  QFont fo = font();
  fo.setPixelSize(12);
  g.save();
  g.setFont(fo);
  const QFontMetricsF m(fo);
  double x = 10;
  const double y = c->verrouillee ? 42 : 10, h = 24;
  // Fond commun, puis un segment par niveau
  std::vector<std::pair<QString, int>> niveaux;
  for (std::size_t i = 0; i < chemin.size(); ++i) {
    std::string nom = c->feuilles[chemin[i]].nom;
    if (i > 0)
      if (const auto p = blocParent(*c, chemin[i]))
        for (const auto& b : c->feuilles[p->first].blocs)
          if (b.uuid == p->second && !b.nom.empty()) nom = b.nom;
    niveaux.push_back({QString::fromStdString(nom.empty() ? "Feuille " + std::to_string(chemin[i] + 1) : nom), static_cast<int>(chemin[i])});
  }
  double l = 20;
  for (std::size_t i = 0; i < niveaux.size(); ++i) l += m.horizontalAdvance(niveaux[i].first) + (i + 1 < niveaux.size() ? m.horizontalAdvance(QStringLiteral(" › ")) : 0);
  g.setPen(Qt::NoPen);
  g.setBrush(alpha(theme().c().surface, 230));
  g.drawRoundedRect(QRectF(x, y, l, h), h / 2, h / 2);
  x += 10;
  for (std::size_t i = 0; i < niveaux.size(); ++i) {
    const bool courant = i + 1 == niveaux.size();
    const double w = m.horizontalAdvance(niveaux[i].first);
    g.setPen(courant ? theme().c().texte : theme().c().accentTexte);
    g.drawText(QRectF(x, y, w, h), Qt::AlignVCenter | Qt::AlignLeft, niveaux[i].first);
    if (!courant) m_zonesAriane.push_back({QRectF(x, y, w, h), niveaux[i].second});
    x += w;
    if (!courant) {
      g.setPen(theme().c().texte);
      const double ws = m.horizontalAdvance(QStringLiteral(" › "));
      g.drawText(QRectF(x, y, ws, h), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral(" › "));
      x += ws;
    }
  }
  g.restore();
}

bool VueSchema::ouvrirAriane(QPointF q) {
  for (const auto& [zone, feuille] : m_zonesAriane)
    if (zone.contains(q)) {
      setConception(m_conception, feuille);
      emit vueChangee();
      return true;
    }
  return false;
}

bool VueSchema::entrerDansBloc(QPointF q) {
  const ConceptionElec* c = conception();
  const Feuille* f = feuilleCourante();
  if (!c || !f) return false;
  const Pt p = monde(q);
  for (const auto& b : f->blocs) {
    if (p.x < b.position.x || p.y < b.position.y || p.x > b.position.x + b.largeur || p.y > b.position.y + b.hauteur) continue;
    for (std::size_t i = 0; i < c->feuilles.size(); ++i)
      if (c->feuilles[i].uuid == b.feuille) {
        setConception(m_conception, static_cast<int>(i));
        emit vueChangee();
        emit message(QStringLiteral("Feuille fille <b>%1</b> : le fil d’Ariane, en haut à gauche, ramène à la feuille mère.").arg(QString::fromStdString(b.nom).toHtmlEscaped()));
        return true;
      }
  }
  return false;
}
