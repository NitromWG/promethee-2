// Prométhée : canevas des folios électriques. Licence GPL-3.0-only.
#include "VueSchemaElectrique.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <tuple>

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolButton>

#include "DessinSchema.hpp"
#include "Document.hpp"
#include "PeintreDxf.hpp"
#include "VueGrafcet.hpp"
#include "promethee/electrique_grafcet.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using namespace dessin_schema;

VueSchemaElectrique::VueSchemaElectrique(Document* doc, QWidget* parent) : VueSchema(doc, parent) {
  // Commandes de vue posées dans le coin : moins, plus, folio entier, dessin
  m_commandesVue = new QWidget(this);
  m_commandesVue->setObjectName(QStringLiteral("commandesVue"));
  m_commandesVue->setStyleSheet(QStringLiteral("#commandesVue { background: rgba(250, 251, 254, 225); border: 1px solid #CDD3E4; border-radius: 8px; }"
                                               "#commandesVue QToolButton { border: none; padding: 3px 7px; color: #232A45; font-weight: 600; }"
                                               "#commandesVue QToolButton:hover { background: #FFF1D6; border-radius: 5px; }"));
  auto* l = new QHBoxLayout(m_commandesVue);
  l->setContentsMargins(3, 2, 3, 2);
  l->setSpacing(1);
  const std::tuple<const char*, const char*, std::function<void()>> boutons[] = {
      {"−", "Zoom arrière (molette)", [this] { zoomer(1 / 1.25); }},
      {"+", "Zoom avant (molette)", [this] { zoomer(1.25); }},
      {"Folio", "Le folio entier (F)", [this] { recadrer(); }},
      {"Dessin", "Le dessin du folio, aussi grand que possible (G)", [this] { ajusterAuDessin(); }}};
  for (const auto& [texteBouton, bulle, action] : boutons) {
    auto* b = new QToolButton;
    b->setText(QString::fromUtf8(texteBouton));
    b->setToolTip(QString::fromUtf8(bulle));
    b->setFocusPolicy(Qt::NoFocus);
    b->setCursor(Qt::PointingHandCursor);
    const auto f = action;
    connect(b, &QToolButton::clicked, this, [f] { f(); });
    l->addWidget(b);
  }
  m_commandesVue->adjustSize();
  m_commandesVue->move(width() - m_commandesVue->width() - 10, height() - m_commandesVue->height() - 10);
  m_commandesVue->show();
}

void VueSchemaElectrique::resizeEvent(QResizeEvent* e) {
  VueSchema::resizeEvent(e);
  if (m_commandesVue) m_commandesVue->move(width() - m_commandesVue->width() - 10, height() - m_commandesVue->height() - 10);
}

int VueSchemaElectrique::margeBas() const { return m_commandesVue ? m_commandesVue->height() + 8 : 0; }

const ConceptionElec* VueSchemaElectrique::conception() const {
  const auto& p = m_doc->projet();
  return m_conception >= 0 && m_conception < static_cast<int>(p.electrique.size()) ? &p.electrique[static_cast<std::size_t>(m_conception)].schema : nullptr;
}

ConceptionElec* VueSchemaElectrique::cible(prom::Projet& p) const {
  return m_conception >= 0 && m_conception < static_cast<int>(p.electrique.size()) ? &p.electrique[static_cast<std::size_t>(m_conception)].schema : nullptr;
}

const prom::elec::ProjetElectrique* VueSchemaElectrique::projetElectrique() const {
  const auto& p = m_doc->projet();
  if (m_conception < 0 || m_conception >= static_cast<int>(p.electrique.size())) return nullptr;
  return &p.electrique[static_cast<std::size_t>(m_conception)];
}

// Le projet électrique entier fait le pas d'annulation (ses rapports, son armoire et ses renvois suivent le schéma)
void VueSchemaElectrique::modifier(const std::function<void(Feuille&, ConceptionElec&)>& f, const QString&, const QString& nom) {
  if (refuseVerrou()) return;
  const int fi = m_feuille;
  m_doc->modifier([&](prom::Projet& p) {
    ConceptionElec* c = cible(p);
    if (!c || fi < 0 || fi >= static_cast<int>(c->feuilles.size())) return;
    f(c->feuilles[static_cast<std::size_t>(fi)], *c);
  }, nom);
}

void VueSchemaElectrique::commencerGeste() { m_doc->debutGeste(); }

std::set<std::string> VueSchemaElectrique::collerDans(const PressePapiersSchema& pp, Pt ou) {
  // Les appareils collés reçoivent aussitôt leur repère
  const int fi = m_feuille, ci = m_conception;
  std::set<std::string> crees;
  m_doc->modifier([&](prom::Projet& p) {
    if (ci < 0 || ci >= static_cast<int>(p.electrique.size())) return;
    auto& pe = p.electrique[static_cast<std::size_t>(ci)];
    crees = collerSchema(pe.schema, static_cast<std::size_t>(fi), pp, ou);
    prom::elec::annoterElectrique(pe, false);
  });
  return crees;
}

QString VueSchemaElectrique::repereFantome(const DefSymbole& d) const {
  const prom::elec::ProjetElectrique* pe = projetElectrique();
  if (!pe) return VueSchema::repereFantome(d);
  return d.prefixe.empty() ? QString() : QString::fromStdString((pe->reglages.tiret ? "-" : "") + prom::elec::prefixeRepere(d.id, pe->reglages.norme) + "?");
}

QString VueSchemaElectrique::aideOutil(Outil o) const {
  static const char* aides[] = {"", "<b>Fil</b> : clique sur une borne, puis sur l’autre ; l’équerre se fait seule · <b>/</b> la change · Échap",
                                "<b>Étiquette</b> : clique où poser le nom du fil",
                                "<b>Potentiel ou renvoi</b> : clique au bout d’un fil ; le même nom posé ailleurs relie les fils et fait le renvoi",
                                "<b>Potentiel</b> : clique au bout d’un fil", "<b>Jonction</b> : clique sur un croisement de fils pour les relier",
                                "<b>Non connecté</b> : clique sur une borne qui doit rester libre", "<b>Texte</b> : clique où l’écrire (à partir de ce point)",
                                "<b>Pose</b> : clique sur le folio · <b>R</b> tourne · Échap pour finir"};
  return QString::fromUtf8(aides[static_cast<int>(o)]);
}

void VueSchemaElectrique::dessinerFond(QPainter& g, const Feuille& f, const ConceptionElec& c) {
  if (const prom::elec::ProjetElectrique* pe = projetElectrique()) dessinerCadreElectrique(g, f, *pe);
  else VueSchema::dessinerFond(g, f, c);
}

// Numéros de fils : pas sur un unifilaire (un trait y vaut plusieurs conducteurs, la norme ne les y numérote pas)
void VueSchemaElectrique::dessinerApresFils(QPainter& g, const Feuille& f) {
  const prom::elec::ProjetElectrique* pe = projetElectrique();
  if (pe && pe->folio(f.uuid).type != prom::elec::TypeFolio::Unifilaire) dessinerNumerosFils(g, f);
}

void VueSchemaElectrique::dessinerApresSymboles(QPainter& g, const Feuille& f) {
  if (const prom::elec::ProjetElectrique* pe = projetElectrique()) dessinerRenvois(g, f, *pe);
}

void VueSchemaElectrique::textesSymbole(const Feuille& f, const SymbolePlace& s, const DefSymbole& dd, const ComposantElec* x, QString& ref, QString& val) const {
  const prom::elec::ProjetElectrique* pe = projetElectrique();
  const DefSymbole* d = &dd;
    if (!pe || !x) return;
    {
      // Électricien : repère « -KM1 » ; une borne montre son numéro, un contact la position de sa bobine, un
      // appareil ses caractéristiques
      ref = QString::fromStdString(prom::elec::repereAffiche(*pe, *x) + (d->unites > 1 ? lettreUnite(s.unite) : std::string()));
      if (prom::elec::roleSymbole(x->symbole) == prom::elec::RoleSymbole::Borne) {
        val = d->broches.empty() ? QString() : QString::fromStdString(numeroBroche(s, d->broches.front().numero));
        // Bornes côte à côte d'un même bornier : le repère n'est écrit qu'une fois, sur la première de la rangée
        const bool rangee = (s.rotation & 1) == 0;
        for (const auto& t : f.symboles) {
          if (t.uuid == s.uuid || t.composant != s.composant || ((t.rotation ^ s.rotation) & 1)) continue;
          const Nm ecart = rangee ? s.position.x - t.position.x : s.position.y - t.position.y;
          const Nm decalage = rangee ? t.position.y - s.position.y : t.position.x - s.position.x;
          if (ecart > 0 && ecart <= 12'700'000 && std::llabs(decalage) < 600'000) {
            ref.clear();
            break;
          }
        }
      } else if (s.symbole != x->symbole) {
        val.clear();
        if (m_renvois)
          if (const auto it = m_renvois->parComposant.find(x->uuid); it != m_renvois->parComposant.end())
            for (const auto& e : it->second)
              if (e.role == prom::elec::RoleSymbole::Principal) {
                val = QStringLiteral("↑ ") + QString::fromStdString(e.ou.texte());
                break;
              }
      } else
        val = QString::fromStdString(prom::elec::caracteristiques(*x));
    }
}

// ---------- Dessin électrique ----------
// Folio électrique (CEI 61082) : cadre, bandeaux de colonnes numérotées et de lignes lettrées, cartouche sur toute
// la largeur (société, projet, folio, auteur, date, révision, numéro de folio)
void VueSchemaElectrique::dessinerCadreElectrique(QPainter& g, const Feuille& f, const prom::elec::ProjetElectrique& pe) {
  const prom::elec::CadreFolio cf = prom::elec::cadreFolio(f.format, pe.reglages.colonnes, pe.reglages.lignes);
  const QRectF feuille(ecran({cf.feuille.x0, cf.feuille.y0}), ecran({cf.feuille.x1, cf.feuille.y1}));
  g.fillRect(feuille, PAPIER);
  g.setPen(QPen(CADRE, 1));
  g.setBrush(Qt::NoBrush);
  g.drawRect(feuille);
  const QRectF cadre = feuille.adjusted(10 * m_echelle, 10 * m_echelle, -10 * m_echelle, -10 * m_echelle);
  const QRectF dessin(ecran({cf.dessin.x0, cf.dessin.y0}), ecran({cf.dessin.x1, cf.dessin.y1}));
  const QRectF cartouche(ecran({cf.cartouche.x0, cf.cartouche.y0}), ecran({cf.cartouche.x1, cf.cartouche.y1}));
  // Grille de points, quand elle est lisible
  if (2.54 * m_echelle >= 9 && !m_impression) {
    QPolygonF points;
    for (double x = mm(cf.dessin.x0); x <= mm(cf.dessin.x1); x += 2.54)
      for (double y = mm(cf.dessin.y0); y <= mm(cf.dessin.y1); y += 2.54) {
        const QPointF q = ecran(ptMm(std::round(x / 1.27) * 1.27, std::round(y / 1.27) * 1.27));
        if (q.x() >= 0 && q.y() >= 0 && q.x() <= width() && q.y() <= height()) points << q;
      }
    g.setPen(QPen(GRILLE, 1.2));
    g.drawPoints(points);
  }
  g.setPen(QPen(CADRE, std::max(1.0, 0.5 * m_echelle)));
  g.drawRect(cadre);
  g.setPen(QPen(CADRE, std::max(1.0, 0.25 * m_echelle)));
  g.drawRect(dessin);
  // Bandeaux : colonnes (en haut et en bas), lignes (à gauche et à droite)
  const double lc = mm(cf.largeurColonne) * m_echelle, hl = mm(cf.hauteurLigne) * m_echelle;
  const double bande = 5 * m_echelle, tailleRepere = 2.6 * m_echelle;
  const QRectF bas(dessin.left(), dessin.bottom(), dessin.width(), cartouche.top() - dessin.bottom());
  for (int i = 0; i <= cf.colonnes; ++i) {
    const double x = dessin.left() + i * lc;
    if (i > 0 && i < cf.colonnes) {
      g.drawLine(QPointF(x, cadre.top()), QPointF(x, dessin.top()));
      g.drawLine(QPointF(x, bas.top()), QPointF(x, bas.bottom()));
    }
    if (i < cf.colonnes) {
      const QString n = QString::number(i + 1);
      texte(g, QPointF(x + lc / 2, cadre.top() + bande / 2), n, tailleRepere, 0, CADRE);
      texte(g, QPointF(x + lc / 2, bas.top() + bas.height() / 2), n, tailleRepere, 0, CADRE);
    }
  }
  for (int j = 0; j < cf.lignes; ++j) {
    const double y = dessin.top() + j * hl;
    if (j > 0) {
      g.drawLine(QPointF(cadre.left(), y), QPointF(dessin.left(), y));
      g.drawLine(QPointF(dessin.right(), y), QPointF(cadre.right(), y));
    }
    const QString l(QChar(static_cast<char16_t>(u'A' + j)));
    texte(g, QPointF(cadre.left() + bande / 2, y + hl / 2), l, tailleRepere, 0, CADRE);
    texte(g, QPointF(cadre.right() - bande / 2, y + hl / 2), l, tailleRepere, 0, CADRE);
  }
  // Cartouche
  g.setBrush(CARTOUCHE);
  g.setPen(QPen(CADRE, std::max(1.0, 0.35 * m_echelle)));
  g.drawRect(cartouche);
  g.setBrush(Qt::NoBrush);
  const double L = cartouche.width(), H = cartouche.height();
  const double xs[] = {0, 0.22, 0.52, 0.74, 0.90, 1.0};
  for (int i = 1; i < 5; ++i) g.drawLine(QPointF(cartouche.left() + L * xs[i], cartouche.top()), QPointF(cartouche.left() + L * xs[i], cartouche.bottom()));
  const prom::ecad::Cartouche& t = pe.schema.cartouche;
  const auto& r = pe.reglages;
  const double petit = 1.7 * m_echelle, normal = 2.6 * m_echelle, grand = 4.2 * m_echelle;
  auto cellule = [&](int i, double y, const QString& etiquette, const QString& valeur, double taille) {
    const double x = cartouche.left() + L * xs[i] + 2 * m_echelle;
    if (!etiquette.isEmpty()) texte(g, QPointF(x, cartouche.top() + H * y - 2.2 * m_echelle), etiquette, petit, -1, CADRE);
    texte(g, QPointF(x, cartouche.top() + H * y + 0.6 * m_echelle), valeur, taille, -1, ENCRE);
  };
  const std::string societe = !r.societe.empty() ? r.societe : t.societe;
  const std::string auteur = !r.auteur.empty() ? r.auteur : t.auteur;
  cellule(0, 0.32, QStringLiteral("Société"), QString::fromStdString(societe), normal);
  cellule(0, 0.75, QStringLiteral("Client"), QString::fromStdString(r.client), normal);
  cellule(1, 0.38, QString(), QString::fromStdString(t.titre.empty() ? pe.nom : t.titre), grand);
  cellule(1, 0.78, QStringLiteral("Installation"), QString::fromStdString(r.installation.empty() ? r.emplacement : r.installation), normal);
  cellule(2, 0.38, QString::fromUtf8(prom::elec::nomTypeFolio(pe.folio(f.uuid).type)), QString::fromStdString(f.nom), normal * 1.15);
  cellule(2, 0.78, QStringLiteral("Format"), QString::fromStdString(f.format), petit * 1.3);
  cellule(3, 0.25, QStringLiteral("Auteur"), QString::fromStdString(auteur), petit * 1.4);
  cellule(3, 0.55, QStringLiteral("Date"), QString::fromStdString(t.date), petit * 1.4);
  cellule(3, 0.85, QStringLiteral("Révision"), QString::fromStdString(t.revision), petit * 1.4);
  const double xf = cartouche.left() + L * xs[4] + 2 * m_echelle;
  texte(g, QPointF(xf, cartouche.top() + H * 0.18), QStringLiteral("Folio"), petit, -1, CADRE);
  texte(g, QPointF(xf, cartouche.top() + H * 0.50), QStringLiteral("%1 / %2").arg(m_feuille + 1).arg(pe.schema.feuilles.size()), grand, -1, ENCRE);
  texte(g, QPointF(cartouche.right() - 2 * m_echelle, cartouche.bottom() - 2.2 * m_echelle), QStringLiteral("Prométhée"), 1.7 * m_echelle, 1, MARQUE);
  // Folios dessinés d'office : borniers, implantation, nomenclature ; la page de garde ci-dessous
  const prom::elec::TypeFolio typeFolio = pe.folio(f.uuid).type;
  if (typeFolio == prom::elec::TypeFolio::Borniers || typeFolio == prom::elec::TypeFolio::Implantation || typeFolio == prom::elec::TypeFolio::Nomenclature ||
      typeFolio == prom::elec::TypeFolio::Grafcet) {
    // En DXF, le contenu dessiné d'office a son calque (BORNIERS, IMPLANTATION, NOMENCLATURE, GRAFCET)
    PeintreDxf::calque(g, QString::fromLatin1(prom::elec::codeTypeFolio(typeFolio)).toUpper());
    dessinerFolioDOffice(g, f, pe, dessin);
    PeintreDxf::calque(g, QStringLiteral("CADRE"));
    return;
  }
  // Page de garde : titre, renseignements du projet et sommaire des folios, tenus à jour sans rien saisir
  if (typeFolio != prom::elec::TypeFolio::Garde) return;
  PeintreDxf::calque(g, QStringLiteral("GARDE"));
  const double W = dessin.width(), Hd = dessin.height();
  auto ecrire = [&](QPointF p, const QString& s, double tailleMm, int alignement, const QColor& c, bool gras) {
    g.save();
    QFont fo = g.font();
    fo.setBold(gras);
    g.setFont(fo);
    texte(g, p, s, tailleMm * m_echelle, alignement, c);
    g.restore();
  };
  const QString titre = QString::fromStdString(t.titre.empty() ? pe.nom : t.titre);
  ecrire(QPointF(dessin.center().x(), dessin.top() + 0.20 * Hd), titre, 9, 0, ENCRE, true);
  const QString installation = QString::fromStdString(r.installation.empty() ? r.emplacement : r.installation);
  if (!installation.isEmpty() && installation != titre) ecrire(QPointF(dessin.center().x(), dessin.top() + 0.20 * Hd + 11 * m_echelle), installation, 5, 0, CADRE, false);
  g.setPen(QPen(CADRE, std::max(1.0, 0.3 * m_echelle)));
  const double yTrait = dessin.top() + 0.20 * Hd + 19 * m_echelle;
  g.drawLine(QPointF(dessin.left() + 0.08 * W, yTrait), QPointF(dessin.right() - 0.08 * W, yTrait));
  // Renseignements
  const double xg = dessin.left() + 0.08 * W, pas = 7 * m_echelle;
  double y = yTrait + 14 * m_echelle;
  const QString reseau = QStringLiteral("%1, neutre %2")
                             .arg(r.tension > 300 ? QStringLiteral("400 V triphasé + N") : QStringLiteral("230 V monophasé"))
                             .arg(QString::fromLatin1(r.regime == prom::elec::RegimeNeutre::TT     ? "TT"
                                                      : r.regime == prom::elec::RegimeNeutre::TNS  ? "TN-S"
                                                      : r.regime == prom::elec::RegimeNeutre::TNC  ? "TN-C"
                                                                                                    : "IT"));
  const QString commande = QStringLiteral("%1 V %2").arg(r.tensionCommande).arg(r.commandeContinue ? QStringLiteral("continu") : QStringLiteral("alternatif"));
  const std::pair<QString, QString> lignes[] = {{QStringLiteral("Client"), QString::fromStdString(r.client)},
                                                {QStringLiteral("Société"), QString::fromStdString(societe)},
                                                {QStringLiteral("Auteur"), QString::fromStdString(auteur)},
                                                {QStringLiteral("Date"), QString::fromStdString(t.date)},
                                                {QStringLiteral("Révision"), QString::fromStdString(t.revision)},
                                                {QStringLiteral("Réseau"), reseau},
                                                {QStringLiteral("Commande"), commande},
                                                {QStringLiteral("Repérage"), r.norme == prom::elec::NormeRepere::Cei81346 ? QStringLiteral("CEI 81346") : QStringLiteral("lettres classiques")}};
  for (const auto& [etiquette, valeur] : lignes) {
    ecrire(QPointF(xg, y), etiquette, 3.2, -1, CADRE, false);
    ecrire(QPointF(xg + 32 * m_echelle, y), valeur.isEmpty() ? QStringLiteral("—") : valeur, 3.6, -1, ENCRE, false);
    y += pas;
  }
  // Révisions : les plus récentes, sous les renseignements (indice, date, auteur, ce qui a changé)
  if (!pe.revisions.empty()) {
    y += 5 * m_echelle;
    ecrire(QPointF(xg, y), QStringLiteral("Révisions"), 4.5, -1, ENCRE, true);
    y += 8 * m_echelle;
    const double pasR = 5.4 * m_echelle, largeurTexte = dessin.left() + 0.48 * W - (xg + 60 * m_echelle);
    const int placesR = std::max(1, static_cast<int>((dessin.bottom() - 6 * m_echelle - y) / pasR));
    const std::size_t nr = pe.revisions.size(), debut = nr > static_cast<std::size_t>(placesR) ? nr - static_cast<std::size_t>(placesR) : 0;
    QFont fr = g.font();
    fr.setPixelSize(std::max(1, static_cast<int>(3.2 * m_echelle)));
    const QFontMetricsF mr(fr);
    for (std::size_t i = nr; i-- > debut;) {
      const auto& rv = pe.revisions[i];
      ecrire(QPointF(xg, y), QString::fromStdString(rv.indice), 3.2, -1, ENCRE, true);
      ecrire(QPointF(xg + 10 * m_echelle, y), QString::fromStdString(rv.date), 3.2, -1, ENCRE, false);
      ecrire(QPointF(xg + 32 * m_echelle, y), QString::fromStdString(rv.auteur), 3.2, -1, CADRE, false);
      ecrire(QPointF(xg + 60 * m_echelle, y), mr.elidedText(QString::fromStdString(rv.description), Qt::ElideRight, std::max(10.0, largeurTexte)), 3.2, -1, ENCRE, false);
      y += pasR;
    }
  }
  // Sommaire
  const double xs0 = dessin.left() + 0.50 * W;
  y = yTrait + 14 * m_echelle;
  ecrire(QPointF(xs0, y), QStringLiteral("Sommaire"), 4.5, -1, ENCRE, true);
  y += 8 * m_echelle;
  const double pasS = 5.4 * m_echelle;
  const int places = std::max(1, static_cast<int>((dessin.bottom() - 6 * m_echelle - y) / pasS));
  const int n = static_cast<int>(pe.schema.feuilles.size());
  for (int i = 0; i < n; ++i) {
    if (i == places - 1 && n > places) {
      ecrire(QPointF(xs0, y), QStringLiteral("… et %1 autres folios").arg(n - i), 3, -1, CADRE, false);
      break;
    }
    const auto& fi = pe.schema.feuilles[static_cast<std::size_t>(i)];
    ecrire(QPointF(xs0, y), QString::number(i + 1), 3.2, -1, ENCRE, true);
    ecrire(QPointF(xs0 + 12 * m_echelle, y), QString::fromStdString(fi.nom), 3.2, -1, ENCRE, false);
    ecrire(QPointF(dessin.right() - 0.08 * W, y), QString::fromUtf8(prom::elec::nomTypeFolio(pe.folio(fi.uuid).type)), 2.8, 1, CADRE, false);
    y += pasS;
  }
}

// Folios dessinés d'office, tenus à jour à chaque modification : plan des borniers et nomenclature en tableaux,
// implantation de l'armoire à l'échelle (plaque de fond à gauche, porte à droite)
void VueSchemaElectrique::dessinerFolioDOffice(QPainter& g, const Feuille& f, const prom::elec::ProjetElectrique& pe, const QRectF& dessin) {
  using prom::elec::TypeFolio;
  const TypeFolio type = pe.folio(f.uuid).type;
  auto ecrire = [&](QPointF p, const QString& s, double tailleMm, int alignement, const QColor& c, bool gras) {
    g.save();
    QFont fo = g.font();
    fo.setBold(gras);
    g.setFont(fo);
    texte(g, p, s, tailleMm * m_echelle, alignement, c);
    g.restore();
  };
  const double marge = 6 * m_echelle;
  const QRectF zone = dessin.adjusted(marge, marge, -marge, -marge);
  const QPen trait(CADRE, std::max(1.0, 0.15 * m_echelle));
  if (type == TypeFolio::Grafcet) {
    // Le grafcet du folio (sinon le premier du projet), à l'échelle 1 s'il tient, réduit sinon ; entrées et sorties en légende
    const prom::elec::InfosFolio& info = pe.folio(f.uuid);
    const prom::elec::Grafcet* gr = nullptr;
    for (const auto& x : pe.grafcets)
      if (x.uuid == info.grafcet) gr = &x;
    if (!gr && !pe.grafcets.empty()) gr = &pe.grafcets.front();
    if (!gr) {
      ecrire(dessin.center(), QStringLiteral("Aucun grafcet : crée-le dans l’onglet Grafcet, il se dessine ici."), 4, 0, CADRE, false);
      return;
    }
    const prom::elec::DispositionGrafcet d = prom::elec::disposerGrafcet(*gr);
    ecrire(QPointF(zone.left(), zone.top() + 3 * m_echelle), QStringLiteral("Grafcet %1").arg(QString::fromStdString(gr->nom)), 5, -1, ENCRE, true);
    QStringList entrees, sorties;
    for (const auto& e : prom::elec::entreesGrafcet(*gr)) entrees << QString::fromStdString(e);
    for (const auto& x : prom::elec::sortiesGrafcet(*gr)) sorties << QString::fromStdString(x);
    ecrire(QPointF(zone.left(), zone.bottom() - 2 * m_echelle),
           QStringLiteral("Entrées : %1 · Sorties : %2 · NF EN CEI 60848")
               .arg(entrees.isEmpty() ? QStringLiteral("aucune") : entrees.join(QStringLiteral(", ")), sorties.isEmpty() ? QStringLiteral("aucune") : sorties.join(QStringLiteral(", "))),
           2.8, -1, CADRE, false);
    const QRectF corps = zone.adjusted(0, 10 * m_echelle, 0, -8 * m_echelle);
    if (d.largeur > 0 && d.hauteur > 0) {
      // Agrandi jusqu'à 1,6 fois pour la lecture quand la place le permet, réduit s'il ne tient pas
      const double r = std::min({1.6, corps.width() / m_echelle / d.largeur, corps.height() / m_echelle / d.hauteur});
      VueGrafcet::peindre(g, d, corps.topLeft(), m_echelle * r, ENCRE, CADRE);
    }
    return;
  }
  if (type == TypeFolio::Implantation) {
    if (pe.armoires.empty()) {
      ecrire(dessin.center(), QStringLiteral("Aucune armoire : crée-la dans l’onglet Armoire, elle se dessine ici à l’échelle."), 4, 0, CADRE, false);
      return;
    }
    const prom::elec::Armoire& a = pe.armoires.front();
    const auto boites = prom::elec::boitesArmoire(pe, 0);
    const bool porte = std::any_of(boites.begin(), boites.end(), [](const prom::elec::BoiteAppareil& b) { return b.porte; });
    ecrire(QPointF(zone.left(), zone.top() + 3 * m_echelle),
           QStringLiteral("%1 — %2 × %3 × %4 mm%5")
               .arg(QString::fromStdString(a.nom))
               .arg(a.largeur)
               .arg(a.hauteur)
               .arg(a.profondeur)
               .arg(pe.armoires.size() > 1 ? QStringLiteral(" (armoire 1 sur %1)").arg(pe.armoires.size()) : QString()),
           5, -1, ENCRE, true);
    const QRectF corps = zone.adjusted(0, 14 * m_echelle, 0, 0);
    auto face = [&](QRectF z, bool facePorte, const QString& legende) {
      const double e = std::min(z.width() / a.largeur, (z.height() - 8 * m_echelle) / a.hauteur);  // pixels par mm de l'armoire
      const QPointF o(z.left() + (z.width() - a.largeur * e) / 2, z.top() + 7 * m_echelle);
      auto R = [&](double x0, double y0, double x1, double y1) { return QRectF(o + QPointF(x0 * e, y0 * e), o + QPointF(x1 * e, y1 * e)); };
      ecrire(QPointF(o.x(), o.y() - 3.5 * m_echelle), legende, 3.2, -1, ENCRE, true);
      g.setPen(QPen(ENCRE, std::max(1.0, 0.3 * m_echelle)));
      g.setBrush(Qt::NoBrush);
      g.drawRect(R(0, 0, a.largeur, a.hauteur));
      if (!facePorte) {
        g.setPen(trait);
        g.setBrush(alpha(CADRE, 35));
        for (const auto& go : a.goulottes) g.drawRect(R(mm(go.b.x0), mm(go.b.y0), mm(go.b.x1), mm(go.b.y1)));
        g.setBrush(alpha(CADRE, 70));
        for (const auto& rl : a.rails) g.drawRect(R(mm(rl.x0), mm(rl.y) - 17.5, mm(rl.x1), mm(rl.y) + 17.5));
      }
      for (const auto& b : boites) {
        if (b.porte != facePorte) continue;
        const QRectF rb = R(mm(b.b.x0), mm(b.b.y0), mm(b.b.x1), mm(b.b.y1));
        g.setPen(QPen(ENCRE, std::max(1.0, 0.22 * m_echelle)));
        g.setBrush(PAPIER);
        g.drawRect(rb);
        const prom::ecad::ComposantElec* c = pe.schema.composant(b.composant);
        if (!c) continue;
        const QString ref = QString::fromStdString(prom::elec::repereAffiche(pe, *c));
        // Repère, et dessous l'usage du circuit (repérage des circuits du tableau, NF C 15-100)
        const QString usage = QString::fromStdString(c->champs.count("fonction") ? c->champs.at("fonction") : std::string());
        const bool debout = rb.height() > rb.width() * 1.6;  // module étroit : textes écrits de bas en haut
        const double longueur = debout ? rb.height() : rb.width(), epaisseur = debout ? rb.width() : rb.height();
        g.save();
        g.translate(rb.center());
        if (debout) {
          // Module étroit, écrit de bas en haut : le repère en tête, l'usage à la suite
          g.rotate(-90);
          const double t = std::clamp(std::min(longueur * 0.3 / std::max<qsizetype>(1, ref.size()) * 1.5, epaisseur * 0.45) / m_echelle, 0.9, 3.5);
          const double tu = std::clamp(std::min(epaisseur * 0.32, t * m_echelle * 0.8) / m_echelle, 0.7, 2.2);
          QFont fu = g.font();
          fu.setPixelSize(std::max(1, static_cast<int>(tu * m_echelle)));
          const QString usageCourt = QFontMetricsF(fu).elidedText(usage, Qt::ElideRight, longueur * 0.62);
          ecrire(QPointF(usage.isEmpty() ? 0 : -longueur / 2 + longueur * 0.17, 0), ref, t, 0, ENCRE, true);
          if (!usage.isEmpty()) ecrire(QPointF(-longueur / 2 + longueur * 0.32, 0), usageCourt, tu, -1, CADRE, false);
        } else {
          // Appareil large : le repère, et l'usage dessous
          const double t = std::clamp(std::min(longueur * 0.8 / std::max<qsizetype>(1, ref.size()) * 1.5, epaisseur * (usage.isEmpty() ? 0.45 : 0.3)) / m_echelle, 0.9, 3.5);
          const double tu = std::clamp(std::min(epaisseur * 0.2, t * m_echelle * 0.75) / m_echelle, 0.6, 2.2);
          QFont fu = g.font();
          fu.setPixelSize(std::max(1, static_cast<int>(tu * m_echelle)));
          const QString usageCourt = QFontMetricsF(fu).elidedText(usage, Qt::ElideRight, longueur * 0.9);
          ecrire(QPointF(0, usage.isEmpty() ? 0 : -epaisseur * 0.16), ref, t, 0, ENCRE, true);
          if (!usage.isEmpty()) ecrire(QPointF(0, epaisseur * 0.2), usageCourt, tu, 0, CADRE, false);
        }
        g.restore();
      }
    };
    if (porte) {
      face(corps.adjusted(0, 0, -corps.width() * 0.42, 0), false, QStringLiteral("Plaque de fond"));
      face(corps.adjusted(corps.width() * 0.62, 0, 0, 0), true, QStringLiteral("Porte"));
    } else
      face(corps, false, a.tableauModulaire ? QStringLiteral("Coffret, rangées de %1 modules").arg(a.modulesParRangee) : QStringLiteral("Plaque de fond"));
    return;
  }
  // Tableaux : plan des borniers ou nomenclature, depuis les rapports du projet
  if (m_generationRapports != m_generation || m_projetRapports != m_conception) {
    m_generationRapports = m_generation;
    m_projetRapports = m_conception;
    m_cacheBorniers = prom::elec::planBorniers(pe);
    m_cacheNomenclature = prom::elec::nomenclatureElectrique(pe);
  }
  QString titre, note;
  std::vector<std::pair<QString, double>> colonnes;
  std::vector<QStringList> lignes;
  if (type == TypeFolio::Borniers) {
    titre = QStringLiteral("Plan des borniers");
    note = QStringLiteral("Chaque borne : ce qui arrive côté armoire, ce qui part côté machine, son fil et son câble.");
    colonnes = {{QStringLiteral("Bornier"), 0.8}, {QStringLiteral("Borne"), 0.6}, {QStringLiteral("Côté armoire"), 2.2}, {QStringLiteral("Côté extérieur"), 2.2},
                {QStringLiteral("Fil"), 0.8},     {QStringLiteral("Câble"), 1.0}, {QStringLiteral("Folio"), 0.7},        {QStringLiteral("PE"), 0.5}};
    for (const auto& b : m_cacheBorniers)
      lignes.push_back({QString::fromStdString(b.bornier), QString::fromStdString(b.borne), QString::fromStdString(b.interieur), QString::fromStdString(b.exterieur),
                        QString::fromStdString(b.fil), QString::fromStdString(b.cable), QString::fromStdString(b.folio), b.protection ? QStringLiteral("PE") : QString()});
  } else {
    titre = QStringLiteral("Nomenclature");
    note = QStringLiteral("Le matériel du projet, regroupé par référence, avec les quantités ; les borniers sont comptés borne par borne.");
    colonnes = {{QStringLiteral("Qté"), 0.5}, {QStringLiteral("Repères"), 2.0}, {QStringLiteral("Désignation"), 3.4}, {QStringLiteral("Fabricant"), 1.2}, {QStringLiteral("Référence"), 1.5}};
    for (const auto& n : m_cacheNomenclature) {
      QString reperes;
      for (const auto& r : n.reperes) reperes += (reperes.isEmpty() ? QString() : QStringLiteral(", ")) + QString::fromStdString(r);
      lignes.push_back({QString::number(n.quantite), reperes, QString::fromStdString(n.designation), QString::fromStdString(n.fabricant), QString::fromStdString(n.reference)});
    }
  }
  ecrire(QPointF(zone.left(), zone.top() + 3 * m_echelle), titre, 5, -1, ENCRE, true);
  ecrire(QPointF(zone.left(), zone.top() + 9.5 * m_echelle), note, 2.8, -1, CADRE, false);
  double y = zone.top() + 14 * m_echelle;
  const double h = 5.2 * m_echelle, taille = 2.7;
  double somme = 0;
  for (const auto& c : colonnes) somme += c.second;
  std::vector<double> xs;
  double x = zone.left();
  for (const auto& c : colonnes) {
    xs.push_back(x);
    x += zone.width() * c.second / somme;
  }
  xs.push_back(zone.right());
  QFont fo = g.font();
  fo.setPixelSize(std::max(1, static_cast<int>(taille * m_echelle)));
  const QFontMetricsF fm(fo);
  const double haut = y;
  auto ligne = [&](const QStringList& cellules, bool entete, bool fond) {
    const QRectF r(zone.left(), y, zone.width(), h);
    if (entete) g.fillRect(r, CARTOUCHE);
    else if (fond) g.fillRect(r, alpha(CARTOUCHE, 110));
    g.setPen(trait);
    g.drawLine(r.bottomLeft(), r.bottomRight());
    for (int k = 0; k < cellules.size() && k + 1 < static_cast<int>(xs.size()); ++k) {
      const double l = xs[static_cast<std::size_t>(k) + 1] - xs[static_cast<std::size_t>(k)] - 2 * m_echelle;
      ecrire(QPointF(xs[static_cast<std::size_t>(k)] + m_echelle, y + h / 2), fm.elidedText(cellules[k], Qt::ElideRight, l), taille, -1, ENCRE, entete);
    }
    y += h;
  };
  QStringList entetes;
  for (const auto& c : colonnes) entetes << c.first;
  ligne(entetes, true, false);
  const int places = std::max(1, static_cast<int>((zone.bottom() - y) / h));
  const int n = static_cast<int>(lignes.size());
  for (int i = 0; i < n; ++i) {
    if (i == places - 1 && n > places) {
      ecrire(QPointF(zone.left() + m_echelle, y + h / 2), QStringLiteral("… et %1 autres lignes : la liste complète est dans l’onglet Rapports (et en CSV).").arg(n - i), taille, -1, CADRE, false);
      y += h;
      break;
    }
    ligne(lignes[static_cast<std::size_t>(i)], false, i % 2 == 1);
  }
  if (n == 0) {
    ecrire(QPointF(zone.left() + m_echelle, y + h / 2), QStringLiteral("Rien à lister pour l’instant : ce folio se remplit tout seul au fil du schéma."), taille, -1, CADRE, false);
    y += h;
  }
  g.setPen(trait);
  for (double xc : xs) g.drawLine(QPointF(xc, haut), QPointF(xc, y));
  g.drawLine(QPointF(zone.left(), haut), QPointF(zone.right(), haut));
}

// Numéros de fils, sur chaque tronçon assez long : au-dessus d'un tronçon horizontal, à gauche d'un vertical
void VueSchemaElectrique::dessinerNumerosFils(QPainter& g, const Feuille& f) {
  if (!m_k) return;
  const double taille = 1.2 * m_echelle;
  if (taille < 4) return;
  // Numéro de fil dans la couleur du fil, en gras : il ne se confond pas avec les numéros de bornes (13, 14…)
  const QColor couleur = FIL.darker(150);
  g.save();
  QFont gras = g.font();
  gras.setBold(true);
  g.setFont(gras);
  for (const auto& w : f.fils) {
    const auto it = m_k->parFil.find(w.uuid);
    if (it == m_k->parFil.end()) continue;
    const Reseau& r = m_k->reseaux[static_cast<std::size_t>(it->second)];
    if (r.nomme || r.nom.rfind("N-", 0) == 0) continue;
    if (mm(static_cast<Nm>(distance(w.a, w.b))) < 5) continue;
    const QPointF a = ecran(w.a), b = ecran(w.b), m = (a + b) / 2;
    const QString t = QString::fromStdString(r.nom);
    if (std::abs(a.y() - b.y()) <= std::abs(a.x() - b.x())) texte(g, m + QPointF(0, -0.9 * m_echelle), t, taille, 0, couleur);
    else {
      g.save();
      g.translate(m + QPointF(-0.9 * m_echelle, 0));
      g.rotate(-90);
      texte(g, QPointF(0, 0), t, taille, 0, couleur);
      g.restore();
    }
  }
  g.restore();
}

// Renvois croisés : sous chaque bobine (ou corps d'appareil), la liste de ses contacts et pôles avec leur place
// (folio.colonne) ; à côté de chaque potentiel, les autres endroits où il est posé
void VueSchemaElectrique::dessinerRenvois(QPainter& g, const Feuille& f, const prom::elec::ProjetElectrique& pe) {
  if (!m_renvois) return;
  using prom::elec::RoleSymbole;
  const ConceptionElec& c = pe.schema;
  const double taille = 1.15 * m_echelle;
  if (taille < 3) return;
  for (const auto& s : f.symboles) {
    if (s.composant.empty()) continue;
    const ComposantElec* x = c.composant(s.composant);
    if (!x || s.symbole != x->symbole || prom::elec::roleSymbole(x->symbole) == RoleSymbole::Borne) continue;
    const auto it = m_renvois->parComposant.find(x->uuid);
    if (it == m_renvois->parComposant.end()) continue;
    std::vector<std::pair<QString, QString>> lignes;
    for (const auto& e : it->second) {
      if (e.symbole == s.uuid || e.role == RoleSymbole::Principal) continue;
      QString genre;
      switch (e.role) {
        case RoleSymbole::ContactNO:
        case RoleSymbole::ContactNOTempo:
        case RoleSymbole::ContactThermiqueNO: genre = QStringLiteral("NO"); break;
        case RoleSymbole::ContactNF:
        case RoleSymbole::ContactNFTempo:
        case RoleSymbole::ContactThermiqueNF: genre = QStringLiteral("NF"); break;
        case RoleSymbole::ContactInverseur: genre = QStringLiteral("INV"); break;
        case RoleSymbole::PolesPuissance: genre = QStringLiteral("P"); break;
        default: break;
      }
      lignes.push_back({(genre.isEmpty() ? QString() : genre + QStringLiteral(" ")) + QString::fromStdString(e.bornes), QString::fromStdString(e.ou.texte())});
    }
    if (lignes.empty()) continue;
    const DefSymbole* d = c.symbole(s.symbole);
    if (!d) continue;
    const Boite b = boiteSymbole(s, *d);
    const QPointF haut = ecran({s.position.x, b.y1}) + QPointF(0, 1.4 * m_echelle);
    const double pas = 1.6 * m_echelle, largeur = 11 * m_echelle;
    g.setPen(QPen(CADRE, std::max(1.0, 0.12 * m_echelle)));
    g.drawLine(haut - QPointF(largeur / 2, 0), haut + QPointF(largeur / 2, 0));
    for (std::size_t i = 0; i < lignes.size(); ++i) {
      const QPointF y = haut + QPointF(0, pas * (static_cast<double>(i) + 0.75));
      texte(g, y - QPointF(largeur / 2, 0), lignes[i].first, taille, -1, ENCRE);
      texte(g, y + QPointF(largeur / 2, 0), lignes[i].second, taille, 1, GLOBALE);
    }
  }
  // Potentiels et renvois de folio
  QFont fo = g.font();
  fo.setPixelSize(std::max(1, static_cast<int>(1.4 * m_echelle)));
  const QFontMetricsF fm(fo);
  for (const auto& e : f.etiquettes) {
    if (e.type != TypeEtiquette::Globale) continue;
    const auto it = m_renvois->parPotentiel.find(e.texte);
    if (it == m_renvois->parPotentiel.end() || it->second.size() < 2) continue;
    std::vector<std::string> autres;
    for (const auto& x : it->second)
      if (x.symbole != e.uuid && std::find(autres.begin(), autres.end(), x.ou.texte()) == autres.end()) autres.push_back(x.ou.texte());
    if (autres.empty()) continue;
    QString t;
    for (std::size_t i = 0; i < autres.size() && i < 3; ++i) t += (i ? QStringLiteral(" ") : QString()) + QString::fromStdString(autres[i]);
    if (autres.size() > 3) t += QStringLiteral(" …");
    const double l = fm.horizontalAdvance(QString::fromStdString(e.texte)) + 1.1 * m_echelle + 0.6 * m_echelle + 0.8 * m_echelle;
    const QPointF p = ecran(e.p);
    switch (e.rotation & 3) {
      case 0: texte(g, p + QPointF(l, 0), t, taille, -1, GLOBALE); break;
      case 2: texte(g, p - QPointF(l, 0), t, taille, 1, GLOBALE); break;
      case 1:
        g.save();
        g.translate(p + QPointF(0, -l));
        g.rotate(-90);
        texte(g, QPointF(0, 0), t, taille, -1, GLOBALE);
        g.restore();
        break;
      default:
        g.save();
        g.translate(p + QPointF(0, l));
        g.rotate(90);
        texte(g, QPointF(0, 0), t, taille, -1, GLOBALE);
        g.restore();
        break;
    }
  }
}

