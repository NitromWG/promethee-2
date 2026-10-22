// Prométhée : panneau latéral. Licence GPL-3.0-only.
#include "RemplacerPage.hpp"
#include "Panneau.hpp"
#include "Theme.hpp"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ChampNombre.hpp"
#include "promethee/derive.hpp"
#include "promethee/nomenclature.hpp"

using namespace prom;

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
QString mm(double v) { return qs(fmt(v, 2)) + QStringLiteral(" mm"); }

QIcon pastille(const QColor& c) {
  QPixmap pm(14, 14);
  pm.fill(Qt::transparent);
  QPainter g(&pm);
  g.setRenderHint(QPainter::Antialiasing);
  g.setBrush(c);
  g.setPen(Qt::NoPen);
  g.drawEllipse(1, 1, 12, 12);
  return QIcon(pm);
}

Composant* trouverComposant(Projet& p, const std::string& id) {
  for (auto& k : p.composants) if (k.id == id) return &k;
  return nullptr;
}
const Composant* trouverComposant(const Projet& p, const std::string& id) {
  for (const auto& k : p.composants) if (k.id == id) return &k;
  return nullptr;
}
}  // namespace

Panneau::Panneau(Document* doc, QWidget* parent) : QTabWidget(parent), m_doc(doc) {
  m_zone = new QScrollArea;
  m_zone->setWidgetResizable(true);
  m_zone->setFrameShape(QFrame::NoFrame);
  addTab(m_zone, QStringLiteral("Propriétés"));

  auto* pageVerifs = new QWidget;
  auto* colonne = new QVBoxLayout(pageVerifs);
  m_verifs = new QListWidget;
  m_verifs->setWordWrap(true);
  m_verifs->setSpacing(3);
  m_corriger = new QPushButton(QStringLiteral("Appliquer la correction"));
  m_corriger->setEnabled(false);
  colonne->addWidget(m_verifs);
  colonne->addWidget(m_corriger);
  addTab(pageVerifs, QStringLiteral("Vérifications"));

  m_nomen = new QTableWidget(0, 3);
  m_nomen->setHorizontalHeaderLabels({QStringLiteral("Repère"), QStringLiteral("Désignation"), QStringLiteral("Qté")});
  m_nomen->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_nomen->verticalHeader()->hide();
  m_nomen->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_nomen->setWordWrap(true);
  addTab(m_nomen, QStringLiteral("Nomenclature"));

  connect(m_verifs, &QListWidget::currentRowChanged, this, [this](int i) {
    const auto& pb = m_doc->problemes();
    m_corriger->setEnabled(i >= 0 && i < static_cast<int>(pb.size()) && pb[static_cast<size_t>(i)].fix.has_value());
  });
  connect(m_verifs, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
    const int i = m_verifs->currentRow();
    const auto& pb = m_doc->problemes();
    if (i < 0 || i >= static_cast<int>(pb.size()) || pb[static_cast<size_t>(i)].ids.empty()) return;
    const std::string& id = pb[static_cast<size_t>(i)].ids.front();
    if (trouverComposant(m_doc->projet(), id)) m_doc->selectionner({Cible::Composant, id});
    else m_doc->selectionner({Cible::Trou, id});
  });
  connect(m_corriger, &QPushButton::clicked, this, [this] {
    const int i = m_verifs->currentRow();
    const auto& pb = m_doc->problemes();
    if (i < 0 || i >= static_cast<int>(pb.size()) || !pb[static_cast<size_t>(i)].fix) return;
    const Correction fix = *pb[static_cast<size_t>(i)].fix;
    m_doc->modifier([&](Projet& p) { corriger(p, fix); });
  });
  connect(doc, &Document::selectionChangee, this, [this] { construireProprietes(); });
  // Vérifications et nomenclature : regroupées (au plus une mise à jour toutes les 120 ms) pour rester fluide en glissant.
  m_minuteurListes.setSingleShot(true);
  m_minuteurListes.setInterval(120);
  connect(&m_minuteurListes, &QTimer::timeout, this, [this] { majVerifications(); majNomenclature(); });
  connect(doc, &Document::change, this, [this] {
    if (m_doc->selection() != m_cibleConstruite || m_doc->projet().carte.libre() != m_carteLibre) construireProprietes();
    else synchroniser();
    if (!m_minuteurListes.isActive()) m_minuteurListes.start();
  });
  construireProprietes();
  majVerifications();
  majNomenclature();
}

QFormLayout* Panneau::groupe(QVBoxLayout* colonne, const QString& titre) {
  auto* boite = new QGroupBox(titre);
  auto* f = new QFormLayout(boite);
  f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  colonne->addWidget(boite);
  return f;
}

QDoubleSpinBox* Panneau::nombre(QFormLayout* f, const QString& libelle, double min, double max, std::function<double()> lire,
                                std::function<void(Projet&, double)> ecrire) {
  auto* s = new ChampNombre;
  s->setRange(min, max);
  s->setDecimals(2);
  s->setSingleStep(0.5);
  s->setSuffix(QStringLiteral(" mm"));
  s->setKeyboardTracking(false);
  s->setValue(lire());
  connect(s, &QDoubleSpinBox::valueChanged, this, [this, lire, ecrire](double v) {
    if (std::abs(v - lire()) < 1e-9) return;
    m_doc->modifier([&](Projet& p) { ecrire(p, v); });
  });
  m_synchros.push_back([s, lire] {
    if (s->hasFocus()) return;
    const QSignalBlocker bloque(s);
    s->setValue(lire());
  });
  f->addRow(libelle, s);
  return s;
}

void Panneau::texte(QFormLayout* f, const QString& libelle, std::function<std::string()> lire, std::function<void(Projet&, const std::string&)> ecrire) {
  auto* e = new QLineEdit(qs(lire()));
  connect(e, &QLineEdit::editingFinished, this, [this, e, lire, ecrire] {
    const std::string v = e->text().trimmed().toStdString();
    if (v.empty() || v == lire()) { e->setText(qs(lire())); return; }
    m_doc->modifier([&](Projet& p) { ecrire(p, v); });
  });
  m_synchros.push_back([e, lire] { if (!e->hasFocus()) e->setText(qs(lire())); });
  f->addRow(libelle, e);
}

void Panneau::lecture(QVBoxLayout* colonne, std::function<QString()> lire) {
  auto* l = new QLabel(lire());
  l->setWordWrap(true);
  l->setStyleSheet(QStringLiteral("color: #565E7D; padding: 2px 4px;"));
  colonne->addWidget(l);
  m_synchros.push_back([l, lire] { l->setText(lire()); });
}

void Panneau::construireProprietes() {
  m_synchros.clear();
  m_cibleConstruite = m_doc->selection();
  m_carteLibre = m_doc->projet().carte.libre();
  auto* page = new QWidget;
  auto* colonne = new QVBoxLayout(page);
  Document* doc = m_doc;
  const Cible sel = m_doc->selection();
  const Composant* k0 = sel.type == Cible::Composant ? m_doc->composantSelectionne() : nullptr;
  const Trou* t0 = sel.type == Cible::Trou ? m_doc->trouSelectionne() : nullptr;

  if (k0) {
    const std::string id = k0->id;
    const TypeComposant& T = *typeComposant(k0->type);
    auto titre = new QLabel;
    titre->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    colonne->addWidget(titre);
    m_synchros.push_back([titre, doc, id, &T] {
      const Composant* k = trouverComposant(doc->projet(), id);
      titre->setText(k ? qs(k->ref) + QStringLiteral(" · ") + qs(T.nom) : QString());
    });
    auto K = [doc, id]() -> const Composant& { static Composant vide; const Composant* k = trouverComposant(doc->projet(), id); return k ? *k : vide; };
    QFormLayout* f = groupe(colonne, QStringLiteral("Identité"));
    texte(f, QStringLiteral("Repère"), [K] { return K().ref; }, [id](Projet& p, const std::string& v) { if (auto* k = trouverComposant(p, id)) k->ref = v.substr(0, 12); });
    texte(f, QStringLiteral("Valeur"), [K] { return K().valeur; }, [id](Projet& p, const std::string& v) { if (auto* k = trouverComposant(p, id)) k->valeur = v.substr(0, 60); });
    f = groupe(colonne, QStringLiteral("Position (depuis le coin inférieur gauche de la carte)"));
    std::vector<QWidget*> champsPosition;
    auto ox = [doc] { return doc->projet().carte.x0 - doc->projet().carte.L / 2; };
    auto oy = [doc] { return doc->projet().carte.y0 - doc->projet().carte.W / 2; };
    if (T.bord) {
      auto* bord = new QComboBox;
      bord->addItem(QStringLiteral("Gauche"), "W");
      bord->addItem(QStringLiteral("Droite"), "E");
      bord->addItem(QStringLiteral("Haut"), "N");
      bord->addItem(QStringLiteral("Bas"), "S");
      bord->setCurrentIndex(bord->findData(QString(QChar(lettre(*k0->bord)))));
      connect(bord, &QComboBox::currentIndexChanged, this, [this, bord, id] {
        const auto b = bordDe(bord->currentData().toString().toStdString());
        m_doc->modifier([&](Projet& p) {
          auto* k = trouverComposant(p, id);
          if (!k || !b || k->bord == b) return;
          const bool change = horizontal(*k->bord) != horizontal(*b);
          k->bord = b;
          if (change) placerLeLong(p, *k, horizontal(*b) ? p.carte.x0 : p.carte.y0);
          contraindreBords(p);
        });
      });
      m_synchros.push_back([bord, K] { const QSignalBlocker b(bord); if (K().bord) bord->setCurrentIndex(bord->findData(QString(QChar(lettre(*K().bord))))); });
      f->addRow(QStringLiteral("Bord"), bord);
      champsPosition.push_back(nombre(f, QStringLiteral("Le long du bord"), -500, 500, [doc, K, ox, oy] { return leLongComposant(doc->projet(), K()) - (K().bord && horizontal(*K().bord) ? ox() : oy()); },
             [id](Projet& p, double v) {
               if (auto* k = trouverComposant(p, id)) placerLeLong(p, *k, v + (horizontal(*k->bord) ? p.carte.x0 - p.carte.L / 2 : p.carte.y0 - p.carte.W / 2));
               contraindreBords(p);
             }));
      champsPosition.push_back(nombre(f, QStringLiteral("Écart à la paroi"), 0, 10, [K] { return K().ecart; }, [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) k->ecart = v; }));
      champsPosition.push_back(bord);
    } else {
      champsPosition.push_back(nombre(f, QStringLiteral("X"), -500, 500, [doc, K, ox] { return posComposant(doc->projet(), K()).x - ox(); },
             [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) placerComposant(p, *k, v + p.carte.x0 - p.carte.L / 2, posComposant(p, *k).y); }));
      champsPosition.push_back(nombre(f, QStringLiteral("Y"), -500, 500, [doc, K, oy] { return posComposant(doc->projet(), K()).y - oy(); },
             [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) placerComposant(p, *k, posComposant(p, *k).x, v + p.carte.y0 - p.carte.W / 2); }));
      auto* rot = new QComboBox;
      for (int r : {0, 90, 180, 270}) rot->addItem(QString::number(r) + QStringLiteral("°"), r);
      rot->setCurrentIndex(rot->findData(k0->rot));
      connect(rot, &QComboBox::currentIndexChanged, this, [this, rot, id] {
        const int r = rot->currentData().toInt();
        m_doc->modifier([&](Projet& p) { if (auto* k = trouverComposant(p, id)) k->rot = r; });
      });
      m_synchros.push_back([rot, K] { const QSignalBlocker b(rot); rot->setCurrentIndex(rot->findData(K().rot)); });
      f->addRow(QStringLiteral("Rotation"), rot);
      champsPosition.push_back(rot);
      auto* face = new QComboBox;
      face->addItem(QStringLiteral("Dessus (face avant)"), false);
      face->addItem(QStringLiteral("Dessous (face arrière)"), true);
      face->setCurrentIndex(k0->dessous ? 1 : 0);
      connect(face, &QComboBox::currentIndexChanged, this, [this, face, id] {
        const bool dessous = face->currentData().toBool();
        m_doc->modifier([&](Projet& p) { if (auto* k = trouverComposant(p, id)) k->dessous = dessous; });
      });
      m_synchros.push_back([face, K] { const QSignalBlocker b(face); face->setCurrentIndex(K().dessous ? 1 : 0); });
      f->addRow(QStringLiteral("Face"), face);
    }
    f = groupe(colonne, QStringLiteral("Contrainte quand la carte change de taille"));
    auto* ancrage = new QComboBox;
    const std::vector<std::pair<const char*, QString>> choix = T.bord
        ? std::vector<std::pair<const char*, QString>>{{"libre", QStringLiteral("Reste à sa place")}, {"debut", QStringLiteral("Suit le début du bord")},
                                                        {"fin", QStringLiteral("Suit la fin du bord")}, {"milieu", QStringLiteral("Suit le milieu du bord")}}
        : std::vector<std::pair<const char*, QString>>{{"libre", QStringLiteral("Reste à sa place")}, {"NW", QStringLiteral("Suit le coin haut gauche")},
                                                        {"NE", QStringLiteral("Suit le coin haut droit")}, {"SW", QStringLiteral("Suit le coin bas gauche")},
                                                        {"SE", QStringLiteral("Suit le coin bas droit")}, {"centre", QStringLiteral("Suit le centre de la carte")}};
    for (const auto& [cle, texte] : choix) ancrage->addItem(texte, QString::fromLatin1(cle));
    ancrage->setCurrentIndex(ancrage->findData(qs(k0->ancrage)));
    connect(ancrage, &QComboBox::currentIndexChanged, this, [this, ancrage, id] {
      const std::string a = ancrage->currentData().toString().toStdString();
      m_doc->modifier([&](Projet& p) { if (auto* k = trouverComposant(p, id)) changerAncrage(p, *k, a); });
    });
    m_synchros.push_back([ancrage, K] { const QSignalBlocker b(ancrage); ancrage->setCurrentIndex(ancrage->findData(qs(K().ancrage))); });
    f->addRow(QStringLiteral("Ancrage"), ancrage);
    auto* verrou = new QCheckBox(QStringLiteral("Verrouiller la position"));
    verrou->setChecked(k0->verrou);
    connect(verrou, &QCheckBox::toggled, this, [this, id](bool on) { m_doc->modifier([&](Projet& p) { if (auto* k = trouverComposant(p, id)) k->verrou = on; }); });
    m_synchros.push_back([verrou, K, champsPosition, ancrage] {
      const QSignalBlocker b(verrou);
      verrou->setChecked(K().verrou);
      for (QWidget* w : champsPosition) w->setEnabled(!K().verrou);
      ancrage->setEnabled(!K().verrou);
    });
    verrou->setText(QStringLiteral("Verrouiller la position (Ctrl+L)"));
    verrou->setStyleSheet(QStringLiteral("font-weight: 600; padding: 4px 0;"));
    colonne->insertWidget(1, verrou);
    lecture(colonne, [doc, K] {
      const Composant& k = K();
      const Projet& p = doc->projet();
      if (k.verrou) return QStringLiteral("Position verrouillée : le composant ne bouge ni à la souris ni au clavier.");
      if (k.ancrage == "libre") return QStringLiteral("Reste à sa place quand la carte change de taille.");
      if (k.ancrage == "centre") return QStringLiteral("Reste à %1 et %2 du centre de la carte.").arg(mm(k.ax), mm(k.ay));
      if (k.bord) return QStringLiteral("Reste à %1 de son repère sur le bord (%2).").arg(mm(std::abs(k.ax)), mm(leLongComposant(p, k)));
      return QStringLiteral("Reste à %1 et %2 du coin.").arg(mm(k.ax), mm(k.ay));
    });
    f = groupe(colonne, QStringLiteral("Dimensions"));
    nombre(f, T.bord ? QStringLiteral("Largeur") : QStringLiteral("Longueur"), 0.5, 150, [K] { return K().w; }, [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) k->w = v; });
    nombre(f, T.bord ? QStringLiteral("Profondeur") : QStringLiteral("Largeur"), 0.5, 150, [K] { return K().d; }, [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) k->d = v; });
    nombre(f, QStringLiteral("Hauteur"), 0.2, 100, [K] { return K().h; }, [id](Projet& p, double v) {
      if (auto* k = trouverComposant(p, id)) {
        k->h = v;
        k->sourceHauteur = "verifiee";  // une hauteur saisie vient de la fiche technique ou d'une mesure
      }
    });
    {
      // Provenance de la hauteur : seule une hauteur vérifiée rend le contrôle du boîtier complet
      auto* verifiee = new QCheckBox(QStringLiteral("Hauteur vérifiée (fiche technique, mesure ou modèle exact)"));
      verifiee->setChecked(K().sourceHauteur == "verifiee");
      connect(verifiee, &QCheckBox::toggled, this, [this, id](bool on) {
        m_doc->modifier([&](Projet& p) {
          Composant* k = trouverComposant(p, id);
          if (!k) return;
          if (k->origine == "pcb") {
            // Composant d'une carte conçue dans Prométhée : la confirmation va au composant électronique
            for (auto& ce : p.electronique)
              if (auto* x = ce.composant(id)) x->hauteurVerifiee = on;
          }
          k->sourceHauteur = on ? "verifiee" : (k->origine == "kicad" ? "estimee" : k->origine == "pcb" ? "nominale" : "type");
        });
      });
      f->addRow(QString(), verifiee);
      m_synchros.push_back([verifiee, K] {
        const QSignalBlocker b(verifiee);
        verifiee->setChecked(K().sourceHauteur == "verifiee");
      });
      lecture(colonne, [K] {
        const std::string& s = K().sourceHauteur;
        if (s == "verifiee") return QString();
        return s == "estimee"    ? QStringLiteral("Hauteur estimée d’après le nom de l’empreinte KiCad : à vérifier, le contrôle du boîtier reste incomplet.")
               : s == "nominale" ? QStringLiteral("Hauteur générique de l’empreinte : à vérifier sur la fiche technique, le contrôle du boîtier reste incomplet.")
                                 : QStringLiteral("Hauteur typique du type de composant : à vérifier, le contrôle du boîtier reste incomplet.");
      });
    }
    lecture(colonne, [doc, id, &T] {
      for (const auto& g : doc->derive().comps) {
        if (g.id != id) continue;
        const double marge = doc->derive().zt - g.top;
        if (T.bouton) return marge <= 0.3 ? QStringLiteral("La tige dépasse de %1 dans le couvercle.").arg(mm(-marge)) : QStringLiteral("Il manque %1 pour atteindre le couvercle.").arg(mm(marge));
        return marge >= 0 ? QStringLiteral("Marge sous le couvercle : %1.").arg(mm(marge)) : QStringLiteral("Dépasse le couvercle de %1.").arg(mm(-marge));
      }
      return QString();
    });
    if (T.bord) {
      f = groupe(colonne, QStringLiteral("Découpe dans la paroi"));
      nombre(f, QStringLiteral("Largeur"), 1, 100, [K] { return K().decoupe.w; }, [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) { k->decoupe.w = v; k->decoupe.r = std::min(k->decoupe.r, std::min(k->decoupe.w, k->decoupe.h) / 2); } });
      nombre(f, QStringLiteral("Hauteur"), 1, 100, [K] { return K().decoupe.h; }, [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) { k->decoupe.h = v; k->decoupe.r = std::min(k->decoupe.r, std::min(k->decoupe.w, k->decoupe.h) / 2); } });
      nombre(f, QStringLiteral("Rayon des angles"), 0, 50, [K] { return K().decoupe.r; }, [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) k->decoupe.r = std::min(v, std::min(k->decoupe.w, k->decoupe.h) / 2); });
      nombre(f, QStringLiteral("Axe au-dessus de la carte"), 0, 100, [K] { return K().zc; }, [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) k->zc = v; });
    }
    if (T.couvercle > 0) {
      f = groupe(colonne, QStringLiteral("Perçage du couvercle"));
      nombre(f, QStringLiteral("Diamètre"), 0.5, 30, [K] { return K().trou_couvercle.value_or(0); }, [id](Projet& p, double v) { if (auto* k = trouverComposant(p, id)) k->trou_couvercle = v; });
    }
    auto* supprimer = new QPushButton(QStringLiteral("Supprimer %1").arg(qs(k0->ref)));
    connect(supprimer, &QPushButton::clicked, this, [this, id] {
      m_doc->modifier([&](Projet& p) { p.composants.erase(std::remove_if(p.composants.begin(), p.composants.end(), [&](const Composant& k) { return k.id == id; }), p.composants.end()); });
    });
    colonne->addWidget(supprimer);
  } else if (t0) {
    const std::string id = t0->id;
    auto titre = new QLabel(qs(t0->ref) + QStringLiteral(" · Trou de fixation"));
    titre->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    colonne->addWidget(titre);
    auto pos = [doc, id]() -> QPointF { for (const auto& t : doc->derive().trous) if (t.id == id) return {t.x, t.y}; return {}; };
    QFormLayout* f = groupe(colonne, QStringLiteral("Position (depuis le coin inférieur gauche de la carte)"));
    nombre(f, QStringLiteral("X"), -500, 500, [doc, pos] { return pos().x() - (doc->projet().carte.x0 - doc->projet().carte.L / 2); },
           [id, pos](Projet& p, double v) { for (auto& t : p.trous) if (t.id == id) ancrerTrou(p, t, v + p.carte.x0 - p.carte.L / 2, pos().y()); });
    nombre(f, QStringLiteral("Y"), -500, 500, [doc, pos] { return pos().y() - (doc->projet().carte.y0 - doc->projet().carte.W / 2); },
           [id, pos](Projet& p, double v) { for (auto& t : p.trous) if (t.id == id) ancrerTrou(p, t, pos().x(), v + p.carte.y0 - p.carte.W / 2); });
    auto Tm = [doc, id]() -> const Trou& { static Trou vide; for (const auto& t : doc->projet().trous) if (t.id == id) return t; return vide; };
    auto TP = [doc, id]() -> const TrouPlace& { static TrouPlace vide; for (const auto& t : doc->derive().trous) if (t.id == id) return t; return vide; };
    auto modifierTrou = [this, id](const std::function<void(Projet&, Trou&)>& fn) {
      m_doc->modifier([&](Projet& p) { for (auto& t : p.trous) if (t.id == id) fn(p, t); });
    };
    auto* ancrage = new QComboBox;
    for (const auto& [cle, texte] : {std::pair{"auto", QStringLiteral("Coin le plus proche (à moins de 15 mm)")}, std::pair{"libre", QStringLiteral("Reste à sa place")},
                                     std::pair{"NW", QStringLiteral("Suit le coin haut gauche")}, std::pair{"NE", QStringLiteral("Suit le coin haut droit")},
                                     std::pair{"SW", QStringLiteral("Suit le coin bas gauche")}, std::pair{"SE", QStringLiteral("Suit le coin bas droit")}})
      ancrage->addItem(texte, QString::fromLatin1(cle));
    ancrage->setCurrentIndex(ancrage->findData(qs(t0->ancrage)));
    connect(ancrage, &QComboBox::currentIndexChanged, this, [ancrage, modifierTrou] {
      const std::string a = ancrage->currentData().toString().toStdString();
      modifierTrou([&](Projet& p, Trou& t) { changerAncrageTrou(p, t, a); });
    });
    m_synchros.push_back([ancrage, Tm] { const QSignalBlocker b(ancrage); ancrage->setCurrentIndex(ancrage->findData(qs(Tm().ancrage))); });
    f->addRow(QStringLiteral("Ancrage"), ancrage);
    lecture(colonne, [Tm] {
      const Trou& t = Tm();
      return t.coin ? QStringLiteral("Suit le coin %1 : reste à %2 et %3 de lui quand la carte change de taille.").arg(qs(*t.coin), mm(t.ox), mm(t.oy))
                    : QStringLiteral("Reste à sa place quand la carte change de taille.");
    });
    f = groupe(colonne, QStringLiteral("Fixation"));
    auto* visTrou = new QComboBox;
    visTrou->addItem(QStringLiteral("Celle du projet (%1)").arg(qs(doc->projet().boitier.vis)), QString());
    for (const auto& n : nomsVis()) visTrou->addItem(qs(n), qs(n));
    visTrou->setCurrentIndex(t0->vis ? visTrou->findData(qs(*t0->vis)) : 0);
    connect(visTrou, &QComboBox::currentIndexChanged, this, [visTrou, modifierTrou] {
      const QString v = visTrou->currentData().toString();
      modifierTrou([&](Projet&, Trou& t) { if (v.isEmpty()) t.vis.reset(); else t.vis = v.toStdString(); });
    });
    m_synchros.push_back([visTrou, Tm] { const QSignalBlocker b(visTrou); visTrou->setCurrentIndex(Tm().vis ? visTrou->findData(qs(*Tm().vis)) : 0); });
    f->addRow(QStringLiteral("Vis"), visTrou);
    auto* fixation = new QComboBox;
    fixation->addItem(QStringLiteral("Vis autotaraudeuse dans le pilier"), "autotaraudeuse");
    fixation->addItem(QStringLiteral("Insert laiton posé à chaud"), "insert");
    fixation->setCurrentIndex(fixation->findData(qs(t0->fixation)));
    connect(fixation, &QComboBox::currentIndexChanged, this, [fixation, modifierTrou] {
      const std::string v = fixation->currentData().toString().toStdString();
      modifierTrou([&](Projet&, Trou& t) { t.fixation = v; });
    });
    m_synchros.push_back([fixation, Tm] { const QSignalBlocker b(fixation); fixation->setCurrentIndex(fixation->findData(qs(Tm().fixation))); });
    f->addRow(QStringLiteral("Montage"), fixation);
    f = groupe(colonne, QStringLiteral("Dimensions (personnalisables)"));
    nombre(f, QStringLiteral("Trou dans la carte (Ø)"), 0.5, 20, [TP] { return TP().vis.trou; }, [id](Projet& p, double v) { for (auto& t : p.trous) if (t.id == id) t.diamTrou = v; });
    nombre(f, QStringLiteral("Pastille de cuivre (Ø)"), 1, 30, [TP] { return TP().vis.tete; }, [id](Projet& p, double v) { for (auto& t : p.trous) if (t.id == id) t.diamPastille = v; });
    nombre(f, QStringLiteral("Pilier (Ø)"), 2, 40, [TP] { return 2 * TP().Rb; }, [id](Projet& p, double v) { for (auto& t : p.trous) if (t.id == id) t.diamPilier = v; });
    nombre(f, QStringLiteral("Avant-trou ou logement (Ø)"), 0.5, 30, [TP] { return 2 * TP().rp; }, [id](Projet& p, double v) { for (auto& t : p.trous) if (t.id == id) t.diamLogement = v; });
    nombre(f, QStringLiteral("Longueur de l’insert"), 1, 30, [TP] { return TP().insert ? TP().longInsert : 0.0; },
           [id](Projet& p, double v) { for (auto& t : p.trous) if (t.id == id) t.longueurInsert = v; });
    auto* standard = new QPushButton(QStringLiteral("Revenir aux dimensions standard"));
    connect(standard, &QPushButton::clicked, this, [modifierTrou] {
      modifierTrou([](Projet&, Trou& t) { t.diamTrou.reset(); t.diamPastille.reset(); t.diamPilier.reset(); t.diamLogement.reset(); t.longueurInsert.reset(); });
    });
    colonne->addWidget(standard);
    lecture(colonne, [doc, TP] {
      const TrouPlace& t = TP();
      const QString base = QStringLiteral("Trou de Ø %1 dans la carte, pastille de Ø %2. Pilier de Ø %3 × %4")
                               .arg(mm(t.vis.trou), mm(t.vis.tete), mm(2 * t.Rb), mm(doc->projet().boitier.entretoise));
      if (t.insert) return base + QStringLiteral(" avec logement de Ø %1 pour un insert %2 de %3, vis %2 × %4.").arg(mm(2 * t.rp), qs(t.nomVis), mm(t.longInsert)).arg(t.longVis);
      return base + QStringLiteral(" avec avant-trou de Ø %1 pour une vis %2 × %3.").arg(mm(2 * t.rp), qs(t.nomVis)).arg(t.longVis);
    });
    auto* supprimer = new QPushButton(QStringLiteral("Supprimer %1").arg(qs(t0->ref)));
    connect(supprimer, &QPushButton::clicked, this, [this, id] {
      m_doc->modifier([&](Projet& p) { p.trous.erase(std::remove_if(p.trous.begin(), p.trous.end(), [&](const Trou& t) { return t.id == id; }), p.trous.end()); });
    });
    colonne->addWidget(supprimer);
  } else {
    auto titre = new QLabel;
    titre->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));
    colonne->addWidget(titre);
    m_synchros.push_back([titre, doc] { titre->setText(qs(doc->projet().nom)); });
    lecture(colonne, [doc] {
      const Derive& d = doc->derive();
      const Carte& c = doc->projet().carte;
      return QStringLiteral("Carte de %1 × %2 dans un boîtier de %3 × %4 × %5, couvercle compris.")
          .arg(mm(c.L), mm(c.W), mm(d.Lo), mm(d.Wo), mm(d.ztop));
    });
    QFormLayout* f = groupe(colonne, QStringLiteral("Projet"));
    texte(f, QStringLiteral("Nom"), [doc] { return doc->projet().nom; }, [](Projet& p, const std::string& v) { p.nom = v.substr(0, 80); });
    f = groupe(colonne, QStringLiteral("Carte"));
    if (doc->projet().carte.libre()) {
      const auto& contours = doc->projet().carte.contours;
      int arcs = 0, elements = 0;
      for (const auto& k : contours) {
        elements += static_cast<int>(k.elements.size());
        for (const auto& e : k.elements) arcs += e.milieu ? 1 : 0;
      }
      auto* info = new QLabel(QStringLiteral("Contour de forme libre : %1 éléments dont %2 arcs%3. Encombrement %4 × %5.")
                                  .arg(elements).arg(arcs)
                                  .arg(contours.size() > 1 ? QStringLiteral(", %1 découpe(s) intérieure(s)").arg(contours.size() - 1) : QString())
                                  .arg(mm(doc->projet().carte.L), mm(doc->projet().carte.W)));
      info->setWordWrap(true);
      f->addRow(info);
    } else {
      auto bornerRayon = [](Projet& p) { p.carte.r = std::min(p.carte.r, std::min(p.carte.L, p.carte.W) / 2 - 0.5); };
      nombre(f, QStringLiteral("Longueur"), 10, 300, [doc] { return doc->projet().carte.L; }, [bornerRayon](Projet& p, double v) { p.carte.L = v; bornerRayon(p); });
      nombre(f, QStringLiteral("Largeur"), 10, 300, [doc] { return doc->projet().carte.W; }, [bornerRayon](Projet& p, double v) { p.carte.W = v; bornerRayon(p); });
      nombre(f, QStringLiteral("Rayon des angles"), 0, 50, [doc] { return doc->projet().carte.r; }, [](Projet& p, double v) { p.carte.r = std::min(v, std::min(p.carte.L, p.carte.W) / 2 - 0.5); });
    }
    auto* ep = new QComboBox;
    for (double e : EPAISSEURS_PCB) ep->addItem(qs(fmt(e, 1)) + QStringLiteral(" mm"), e);
    ep->setCurrentIndex(ep->findData(doc->projet().carte.t));
    connect(ep, &QComboBox::currentIndexChanged, this, [this, ep] {
      const double e = ep->currentData().toDouble();
      m_doc->modifier([&](Projet& p) { p.carte.t = e; p.boitier.hauteur = std::max(p.boitier.hauteur, p.boitier.entretoise + e + 1); });
    });
    m_synchros.push_back([ep, doc] { const QSignalBlocker b(ep); ep->setCurrentIndex(ep->findData(doc->projet().carte.t)); });
    f->addRow(QStringLiteral("Épaisseur"), ep);
    f = groupe(colonne, QStringLiteral("Boîtier"));
    nombre(f, QStringLiteral("Jeu autour de la carte"), 0.2, 10, [doc] { return doc->projet().boitier.jeu; }, [](Projet& p, double v) { p.boitier.jeu = v; });
    nombre(f, QStringLiteral("Épaisseur des parois"), 0.8, 8, [doc] { return doc->projet().boitier.paroi; }, [](Projet& p, double v) { p.boitier.paroi = v; });
    nombre(f, QStringLiteral("Épaisseur du fond"), 0.8, 8, [doc] { return doc->projet().boitier.fond; }, [](Projet& p, double v) { p.boitier.fond = v; });
    if (doc->projet().carte.libre())
      nombre(f, QStringLiteral("Marge en plus autour des composants qui dépassent"), 0, 20, [doc] { return doc->projet().boitier.margeDebord; },
             [](Projet& p, double v) { p.boitier.margeDebord = v; });
    nombre(f, QStringLiteral("Hauteur des entretoises"), 1, 30, [doc] { return doc->projet().boitier.entretoise; },
           [](Projet& p, double v) { p.boitier.entretoise = v; p.boitier.hauteur = std::max(p.boitier.hauteur, v + p.carte.t + 1); });
    nombre(f, QStringLiteral("Hauteur intérieure"), 3, 200, [doc] { return doc->derive().H; },
           [](Projet& p, double v) { p.boitier.hauteur = std::max(v, p.boitier.entretoise + p.carte.t + 1); p.boitier.hauteurAuto = false; });
    auto* suivre = new QCheckBox(QStringLiteral("Suivre les composants"));
    suivre->setChecked(doc->projet().boitier.hauteurAuto);
    connect(suivre, &QCheckBox::toggled, this, [this](bool on) {
      const double H = m_doc->derive().H;
      m_doc->modifier([&](Projet& p) { if (!on) p.boitier.hauteur = H; p.boitier.hauteurAuto = on; });
    });
    m_synchros.push_back([suivre, doc] { const QSignalBlocker b(suivre); suivre->setChecked(doc->projet().boitier.hauteurAuto); });
    f->addRow(QString(), suivre);
    nombre(f, QStringLiteral("Épaisseur du couvercle"), 0.8, 8, [doc] { return doc->projet().boitier.couvercle; }, [](Projet& p, double v) { p.boitier.couvercle = v; });
    f = groupe(colonne, QStringLiteral("Fixation de la carte"));
    auto* v = new QComboBox;
    for (const auto& n : nomsVis()) v->addItem(qs(n), qs(n));
    v->setCurrentIndex(v->findData(qs(doc->projet().boitier.vis)));
    connect(v, &QComboBox::currentIndexChanged, this, [this, v] {
      const std::string n = v->currentData().toString().toStdString();
      m_doc->modifier([&](Projet& p) { p.boitier.vis = n; });
    });
    m_synchros.push_back([v, doc] { const QSignalBlocker b(v); v->setCurrentIndex(v->findData(qs(doc->projet().boitier.vis))); });
    f->addRow(QStringLiteral("Vis autotaraudeuse"), v);
  }
  colonne->addStretch(1);
  remplacerPage(m_zone, page);
  synchroniser();
}

void Panneau::synchroniser() {
  for (const auto& s : m_synchros) s();
}

void Panneau::majVerifications() {
  const auto& pb = m_doc->problemes();
  std::string sig;
  for (const auto& q : pb) sig += q.code + q.msg + "|";
  if (sig == m_sigVerifs && m_verifs->count() > 0) return;  // rien n'a changé : on ne refait pas la liste
  m_sigVerifs = sig;
  const int courant = m_verifs->currentRow();
  m_verifs->clear();
  int erreurs = 0;
  for (const auto& q : pb) {
    if (q.sev == Severite::Erreur) ++erreurs;
    auto* item = new QListWidgetItem(pastille(q.sev == Severite::Erreur ? QColor(0xC2, 0x40, 0x2E) : QColor(0xA8, 0x74, 0x10)), qs(q.msg));
    if (q.fix) item->setToolTip(QStringLiteral("Correction possible : %1").arg(qs(q.fix->libelle)));
    m_verifs->addItem(item);
  }
  const ControleBoitier controle = controleBoitier(m_doc->projet());
  if (!controle.complet) {
    auto* item = new QListWidgetItem(pastille(QColor(0xA8, 0x74, 0x10)), qs(controle.resume()));
    item->setToolTip(QStringLiteral("Coche « Hauteur vérifiée » dans les propriétés de chaque composant, après contrôle sur sa fiche technique."));
    item->setFlags(Qt::ItemIsEnabled);
    m_verifs->addItem(item);
  }
  if (pb.empty()) {
    auto* item = new QListWidgetItem(pastille(theme().texte(controle.complet ? QColor(0x2F, 0x7D, 0x53) : QColor(0x6B, 0x73, 0x93))),
                                     controle.complet ? QStringLiteral("Tout est cohérent : la carte entre dans le boîtier, piliers, découpes et perçages tombent juste.")
                                                      : QStringLiteral("Aucun problème détecté avec les hauteurs connues : le contrôle reste à compléter."));
    item->setFlags(Qt::ItemIsEnabled);
    m_verifs->addItem(item);
  }
  if (courant >= 0 && courant < m_verifs->count()) m_verifs->setCurrentRow(courant);
  setTabText(1, pb.empty() ? QStringLiteral("Vérifications") : QStringLiteral("Vérifications (%1)").arg(erreurs ? erreurs : static_cast<int>(pb.size())));
}

void Panneau::majNomenclature() {
  const auto lignes = nomenclature(m_doc->projet(), m_doc->derive());
  std::string sig;
  for (const auto& l : lignes) sig += l.groupe + l.ref + l.designation + std::to_string(l.qte) + "|";
  if (sig == m_sigNomen) return;
  m_sigNomen = sig;
  m_nomen->setRowCount(0);
  std::string groupe;
  for (const auto& l : lignes) {
    if (l.groupe != groupe) {
      groupe = l.groupe;
      const int r = m_nomen->rowCount();
      m_nomen->insertRow(r);
      auto* titre = new QTableWidgetItem(qs(groupe));
      QFont f = titre->font();
      f.setBold(true);
      titre->setFont(f);
      m_nomen->setItem(r, 0, titre);
      m_nomen->setSpan(r, 0, 1, 3);
    }
    const int r = m_nomen->rowCount();
    m_nomen->insertRow(r);
    m_nomen->setItem(r, 0, new QTableWidgetItem(qs(l.ref)));
    auto* des = new QTableWidgetItem(qs(l.designation));
    des->setToolTip(qs(l.appro));
    m_nomen->setItem(r, 1, des);
    auto* q = new QTableWidgetItem(QString::number(l.qte));
    q->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_nomen->setItem(r, 2, q);
  }
  m_nomen->resizeRowsToContents();
}
