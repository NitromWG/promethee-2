// Prométhée : atelier électrique — photovoltaïque. L'installation raccordée au réseau, des modules au compteur :
// au centre le calepinage dessiné (chaînes sur les entrées des onduleurs) et les résultats justifiés (tensions au
// froid et au chaud, protection des chaînes, câbles, sectionnement, parafoudres, protections alternatives,
// production), à droite le matériel et le site. Tout se recalcule à chaque modification ; l'unifilaire et la note
// de calcul en sortent d'un clic. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextDocument>
#include <QToolTip>
#include <QVBoxLayout>

#include "AtelierElectrique.hpp"
#include "Document.hpp"
#include "EtiquetteEtat.hpp"
#include "FormulaireLie.hpp"
#include "Icones.hpp"
#include "promethee/electrique_glossaire.hpp"
#include "promethee/modele.hpp"

using namespace prom::elec;

namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.trimmed().toStdString(); }
QString nombre(double v, int decimales = -1) {
  if (decimales < 0) return QLocale().toString(v, 'g', 6);
  return QLocale().toString(v, 'f', decimales);
}
QString aide(const char* cle) { return QStringLiteral("<a href='aide:%1' style='text-decoration:none; color:#5B4BF5'>&nbsp;ⓘ</a>").arg(QString::fromLatin1(cle)); }
QColor couleurEtat(int erreurs, int alertes) { return erreurs ? QColor(0xC2, 0x40, 0x2E) : alertes ? QColor(0xA8, 0x62, 0x0E) : QColor(0x2E, 0x7D, 0x32); }
QString symboleEtat(int erreurs, int alertes) { return erreurs ? QStringLiteral("⛔") : alertes ? QStringLiteral("⚠") : QStringLiteral("✓"); }
const double PRESETS[] = {3, 6, 9, 36, 100, 250};

// Calepinage dessiné : chaque onduleur, ses entrées MPPT et leurs chaînes, un rectangle par module
class VueCalepinagePV : public QWidget {
 public:
  VueCalepinagePV() {
    setObjectName(QStringLiteral("vueCalepinagePV"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
  void setDonnees(const InstallationPV& i, const ResultatPV& x) {
    m_onduleurs.clear();
    m_modules = x.modulesParChaine;
    const int mppt = std::max(1, i.onduleur.mppt), n = std::max(1, i.onduleurs);
    m_nomOnduleur = QStringLiteral("%1 VA %2").arg(nombre(i.onduleur.puissanceAC, 0), i.onduleur.triphase ? QStringLiteral("triphasé") : QStringLiteral("monophasé"));
    for (int k = 0; k < n && !x.repartition.empty(); ++k) {
      std::vector<int> e;
      for (int j = 0; j < mppt; ++j) e.push_back(x.repartition[static_cast<std::size_t>(k * mppt + j)]);
      m_onduleurs.push_back(e);
    }
    // Au-delà de quatre onduleurs, les suivants sont résumés
    m_resume = static_cast<int>(m_onduleurs.size()) > 4 ? static_cast<int>(m_onduleurs.size()) - 4 : 0;
    if (m_resume > 0) m_onduleurs.resize(4);
    setFixedHeight(hauteur());
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.base());
    if (m_onduleurs.empty() || m_modules <= 0) {
      p.setPen(pal.color(QPalette::PlaceholderText));
      p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Pas de calepinage possible : vois les avis à droite."));
      return;
    }
    const int marge = 10, etiquette = 64, largeurUtile = std::max(80, width() - 2 * marge - etiquette);
    const double pasModule = std::clamp(static_cast<double>(largeurUtile) / m_modules, 3.0, 13.0);
    const double l = std::max(2.0, pasModule - 2), h = std::clamp(l * 1.5, 6.0, 15.0);
    int y = marge;
    QFont f = font();
    f.setPointSizeF(std::max(7.5, f.pointSizeF() - 1));
    p.setFont(f);
    for (std::size_t k = 0; k < m_onduleurs.size(); ++k) {
      const auto& e = m_onduleurs[k];
      p.setPen(pal.color(QPalette::Text));
      p.drawText(marge, y + 12, QStringLiteral("Onduleur %1 · %2").arg(k + 1).arg(m_nomOnduleur));
      y += 18;
      for (std::size_t j = 0; j < e.size(); ++j) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(marge + 6, y + static_cast<int>(h) - 2, QStringLiteral("MPPT %1").arg(j + 1));
        if (e[j] == 0) {
          p.drawText(marge + etiquette, y + static_cast<int>(h) - 2, QStringLiteral("libre"));
          y += static_cast<int>(h) + 6;
          continue;
        }
        for (int c = 0; c < e[j]; ++c) {
          for (int m = 0; m < m_modules; ++m) {
            const QRectF r(marge + etiquette + m * pasModule, y, l, h);
            p.setPen(QPen(QColor(0x1E, 0x3A, 0x8A), 0.8));
            p.setBrush(QColor(0x3B, 0x5F, 0xC8));
            p.drawRect(r);
          }
          y += static_cast<int>(h) + 4;
        }
        y += 2;
      }
      y += 6;
    }
    if (m_resume > 0) {
      p.setPen(pal.color(QPalette::Text));
      p.drawText(marge, y + 12, QStringLiteral("… et %1 onduleur(s) de plus, câblés de la même façon").arg(m_resume));
    }
  }

 private:
  int hauteur() const {
    if (m_onduleurs.empty() || m_modules <= 0) return 60;
    int y = 10;
    for (const auto& e : m_onduleurs) {
      y += 18;
      for (int c : e) y += c == 0 ? 21 : c * 19 + 2;
      y += 6;
    }
    return y + (m_resume > 0 ? 24 : 6);
  }
  std::vector<std::vector<int>> m_onduleurs;
  int m_modules = 0, m_resume = 0;
  QString m_nomOnduleur;
};

// Résultats justifiés, par partie, chaque grandeur avec sa règle
QString resultatsPV(const InstallationPV& i, const ResultatPV& x) {
  const ModulePV& m = i.module;
  const OnduleurPV& o = i.onduleur;
  QString h = QStringLiteral("<table width='100%' cellspacing='0' cellpadding='4'>");
  int rang = 0;
  auto partie = [&h, &rang](const QString& t) {
    h += QStringLiteral("<tr><td colspan='3' style='padding-top:10px'><span style='font-size:13px; font-weight:700; color:#232A45'>%1</span></td></tr>").arg(t);
    rang = 0;
  };
  auto ligne = [&h, &rang](const QString& grandeur, const QString& valeur, const QString& pourquoi, bool alerte = false) {
    const QString fond = rang++ % 2 ? QStringLiteral("#F6F7FB") : QStringLiteral("transparent");
    h += QStringLiteral("<tr style='background:%1'><td style='color:#4A5270'>%2</td><td><b%3>%4</b></td><td style='color:#6B7393'>%5</td></tr>")
             .arg(fond, grandeur, alerte ? QStringLiteral(" style='color:#C2402E'") : QString(), valeur, pourquoi);
  };
  if (x.modulesParChaine <= 0) {
    h += QStringLiteral("</table>");
    return h;
  }
  const double tensionAdmise = std::min(o.tensionDCMax, m.tensionMax);
  partie(QStringLiteral("Calepinage"));
  ligne(QStringLiteral("Modules en série admis") + aide("chaine"), QStringLiteral("%1 à %2").arg(x.seriesMin).arg(x.seriesMax),
        QStringLiteral("plage MPPT par %1 °C, %2 V à vide par %3 °C").arg(nombre(i.tMax, 0), nombre(tensionAdmise, 0), nombre(i.tMin, 0)));
  ligne(QStringLiteral("Chaînes"), QStringLiteral("%1 × %2 modules = %3 kWc").arg(x.chaines).arg(x.modulesParChaine).arg(nombre(x.puissanceCrete, 2)),
        x.modulesPoses < i.nombreModules ? QStringLiteral("%1 module(s) restent hors chaîne").arg(i.nombreModules - x.modulesPoses) : QStringLiteral("tous les modules sont posés"),
        x.modulesPoses < i.nombreModules);
  ligne(QStringLiteral("Tension à vide au froid") + aide("uoc"), QStringLiteral("%1 V").arg(nombre(x.uocChaine, 0)),
        QStringLiteral("%1 × %2 V × k %3 ≤ %4 V").arg(x.modulesParChaine).arg(nombre(m.voc, 2)).arg(nombre(x.kTemperature, 3)).arg(nombre(tensionAdmise, 0)),
        x.uocChaine > tensionAdmise + 1e-9);
  ligne(QStringLiteral("Tension de fonctionnement") + aide("mppt"), QStringLiteral("%1 à %2 V").arg(nombre(x.vmpChaineMin, 0), nombre(x.vmpChaineMax, 0)),
        QStringLiteral("plage MPPT de l’onduleur : %1 à %2 V").arg(nombre(o.mpptMin, 0), nombre(o.mpptMax, 0)), x.vmpChaineMin < o.mpptMin - 1e-9);
  ligne(QStringLiteral("Courant par entrée") + aide("isc"), QStringLiteral("%1 A").arg(nombre(x.chainesParMppt * m.isc, 1)),
        QStringLiteral("%1 chaîne(s) × Isc %2 A ≤ %3 A admis").arg(x.chainesParMppt).arg(nombre(m.isc, 1)).arg(nombre(o.iscMppt, 0)), x.chainesParMppt * m.isc > o.iscMppt + 1e-9);
  ligne(QStringLiteral("Puissance crête / onduleur") + aide("kwc"), nombre(x.ratioDCAC, 2), QStringLiteral("0,8 à 1,3 : l’onduleur travaille bien, écrête peu"), x.ratioDCAC > 1.3 + 1e-9);
  partie(QStringLiteral("Côté continu"));
  ligne(QStringLiteral("Protection des chaînes") + aide("gpv"), x.fusibles ? QStringLiteral("fusibles gPV %1 A").arg(nombre(x.calibreFusible, 0)) : QStringLiteral("aucune"),
        QStringLiteral("%1 chaîne(s) en parallèle, %2 au plus sans protection (1 + IRM / Isc)").arg(x.chainesParMppt).arg(x.chainesMaxSansProtection));
  ligne(QStringLiteral("Câble des chaînes"), QStringLiteral("H1Z2Z2-K %1 mm²").arg(nombre(x.sectionDC)),
        QStringLiteral("Iz %1 A à %2 °C ≥ %3 A ; ΔU %4 % sur %5 m").arg(nombre(x.izDC, 1), nombre(i.temperatureCables, 0), nombre(x.izRequis, 1), nombre(x.chuteDC, 2), nombre(i.longueurDC, 0)),
        x.chuteDC > 1 + 1e-9 || x.izDC + 1e-9 < x.izRequis);
  if (x.boiteJonction)
    ligne(QStringLiteral("Câble principal"), QStringLiteral("H1Z2Z2-K %1 mm²").arg(nombre(x.sectionPrincipal)),
          QStringLiteral("boîte de jonction : %1 chaînes réunies, Iz %2 A").arg(x.chainesParMppt).arg(nombre(x.izPrincipal, 1)));
  ligne(QStringLiteral("Interrupteur-sectionneur"), QStringLiteral("%1 V · %2 A").arg(nombre(x.sectionneurTension, 0), nombre(x.sectionneurCourant, 1)),
        QStringLiteral("bipolaire, DC21B, près de l’onduleur (« Coupure d’urgence entrée onduleur »)"));
  ligne(QStringLiteral("Parafoudre continu") + aide("lcrit"), x.parafoudreDC ? QStringLiteral("obligatoire") : QStringLiteral("non obligatoire"),
        QStringLiteral("%1 m de câbles pour Lcrit %2 m").arg(nombre(i.longueurDC, 0), nombre(x.longueurCritique, 0)));
  partie(QStringLiteral("Côté alternatif"));
  ligne(QStringLiteral("Courant de sortie") + aide("onduleur"), QStringLiteral("%1 A").arg(nombre(x.courantAC, 1)),
        o.courantACMax > 0 ? QStringLiteral("notice de l’onduleur") : QStringLiteral("1,1 fois le courant nominal, faute de notice"));
  ligne(QStringLiteral("Protection") + aide("ddr"), QStringLiteral("%1 A · %2 kA").arg(nombre(x.calibreAC, 0), nombre(x.pdcAC, 0)), q(x.differentielAC).toHtmlEscaped());
  ligne(QStringLiteral("Câble"), QStringLiteral("U1000 R2V %1 mm²").arg(nombre(x.sectionAC)), QStringLiteral("ΔU %1 % sur %2 m (1 % conseillé, 3 % au plus)").arg(nombre(x.chuteAC, 2), nombre(i.longueurAC, 0)),
        x.chuteAC > 1 + 1e-9);
  if (i.onduleurs > 1)
    ligne(QStringLiteral("Protection générale"), QStringLiteral("%1 A").arg(nombre(x.calibreGeneral, 0)), QStringLiteral("%1 onduleurs : %2 A").arg(i.onduleurs).arg(nombre(x.courantACTotal, 1)));
  ligne(QStringLiteral("Parafoudre alternatif") + aide("ng"), x.parafoudreAC ? q(x.typeParafoudreAC) : QStringLiteral("non requis"),
        QStringLiteral("Ng %1, onduleur à %2 m du tableau").arg(nombre(i.ng, 1), nombre(i.longueurAC, 0)));
  ligne(QStringLiteral("Raccordement") + aide("agcp"), i.venteTotale ? QStringLiteral("vente totale") : QStringLiteral("autoconsommation"),
        i.venteTotale ? (x.reglageBranchement > 0 ? QStringLiteral("disjoncteur de branchement 500 mA S réglé à %1 A, compteur de production").arg(nombre(x.reglageBranchement, 0))
                                                  : QStringLiteral("branchement à puissance surveillée, compteur de production"))
                      : QStringLiteral("circuit dédié du tableau, réservé à la production"));
  partie(QStringLiteral("Production"));
  ligne(QStringLiteral("Orientation et inclinaison"), QStringLiteral("coefficient %1").arg(nombre(x.coefOrientation, 2)),
        QStringLiteral("écart au sud %1°, inclinaison %2°").arg(nombre(i.orientation, 0), nombre(i.inclinaison, 0)));
  ligne(QStringLiteral("Production annuelle") + aide("productible"), QStringLiteral("≈ %1 kWh").arg(nombre(x.production, 0)),
        QStringLiteral("%1 kWc × %2 kWh/kWc × %3").arg(nombre(x.puissanceCrete, 2), nombre(i.productible, 0), nombre(x.coefOrientation, 2)));
  h += QStringLiteral("</table>");
  // Les informations à retenir (les erreurs et alertes sont dans le panneau)
  QString infos;
  for (const auto& a : x.avis)
    if (a.niveau == NiveauAvis::Info) infos += QStringLiteral("<li>%1 <span style='color:#8A91AD'>(%2)</span></li>").arg(q(a.message).toHtmlEscaped(), q(a.regle).toHtmlEscaped());
  if (!infos.isEmpty()) h += QStringLiteral("<p style='margin-top:10px'><span style='font-size:13px; font-weight:700; color:#232A45'>À retenir</span></p><ul>%1</ul>").arg(infos);
  return h;
}

}  // namespace

// ---------- Actions ----------
void AtelierElectrique::modifierPV(const std::function<void(InstallationPV&)>& f) {
  modifierProjet([&](ProjetElectrique& p) {
    if (!p.photovoltaique) p.photovoltaique = installationPVType(3);
    f(*p.photovoltaique);
  });
}

void AtelierElectrique::appliquerPV(double kWc) {
  if (!projetCourant()) return;
  const bool remplace = projetCourant()->photovoltaique.has_value();
  modifierProjet([kWc](ProjetElectrique& p) { p.photovoltaique = installationPVType(kWc); });
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !pe->photovoltaique) return;
  const ResultatPV x = calculerPV(*pe->photovoltaique);
  m_etat->setText(QStringLiteral("%1 : %2 modules, %3 chaîne(s) de %4, %5. Remplace le matériel générique par le tien à droite%6.")
                      .arg(q(pe->photovoltaique->nom).toHtmlEscaped())
                      .arg(x.modulesPoses)
                      .arg(x.chaines)
                      .arg(x.modulesParChaine)
                      .arg(q(pe->photovoltaique->onduleur.nom).toHtmlEscaped())
                      .arg(remplace ? QStringLiteral(" ; Ctrl+Z revient à l’installation précédente") : QString()));
}

int AtelierElectrique::genererUnifilairePV() {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !pe->photovoltaique) {
    m_etat->setText(QStringLiteral("Rien à dessiner : choisis d’abord une installation photovoltaïque (onglet Photovoltaïque)."));
    return -1;
  }
  std::size_t f = 0;
  modifierProjet([&](ProjetElectrique& p) { f = prom::elec::genererUnifilairePV(p); });
  recalculer();
  majFolios();
  m_etat->setText(QStringLiteral("Unifilaire photovoltaïque dessiné au folio %1, annoté des résultats du calcul ; il se redessine à chaque fois.").arg(f + 1));
  return static_cast<int>(f);
}

bool AtelierElectrique::exporterNotePV(const QString& chemin) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !pe->photovoltaique) return false;
  const ResultatPV x = calculerPV(*pe->photovoltaique);
  const std::string html = htmlNotePV(*pe->photovoltaique, x, pe->nom);
  const QString ext = QFileInfo(chemin).suffix().toLower();
  if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
    QFile f(chemin);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(QByteArray::fromStdString(html)) == static_cast<qint64>(html.size());
  }
  QTextDocument doc;
  doc.setHtml(q(html));
  QPdfWriter pdf(chemin);
  pdf.setPageSize(QPageSize(QPageSize::A4));
  pdf.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
  pdf.setTitle(QStringLiteral("Note de calcul photovoltaïque — %1").arg(q(pe->photovoltaique->nom)));
  pdf.setCreator(QStringLiteral("Prométhée"));
  doc.print(&pdf);
  return QFileInfo(chemin).size() > 0;
}

// ---------- Page centrale : installations types ou résultats ----------
QWidget* AtelierElectrique::pagePV() {
  auto* racine = new QWidget;
  racine->setObjectName(QStringLiteral("pagePV"));
  auto* pile = new QVBoxLayout(racine);
  pile->setContentsMargins(0, 0, 0, 0);
  m_pilePV = new QStackedWidget;
  pile->addWidget(m_pilePV);
  auto lienGlossaire = [this](QLabel* l) {
    l->setTextFormat(Qt::RichText);
    l->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(l, &QLabel::linkActivated, this, [this](const QString& lien) {
      if (const TermeGlossaire* t = termeGlossaire(s(lien.mid(5)))) {
        QToolTip::showText(QCursor::pos(), QStringLiteral("<b>%1</b><br>%2<br><i>%3</i><br><span style='color:#6B7393'>%4</span>")
                                               .arg(q(t->terme).toHtmlEscaped(), q(t->definition).toHtmlEscaped(), q(t->exemple).toHtmlEscaped(), q(t->regle).toHtmlEscaped()));
        m_etat->setText(QStringLiteral("<b>%1</b> : %2").arg(q(t->terme).toHtmlEscaped(), q(t->definition).toHtmlEscaped()));
      }
    });
  };
  // Pas encore d'installation : on part d'une taille type
  {
    auto* defil = new QScrollArea;
    defil->setWidgetResizable(true);
    defil->setFrameShape(QFrame::NoFrame);
    auto* w = new QWidget;
    w->setObjectName(QStringLiteral("pvVide"));
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(28, 22, 28, 22);
    col->setSpacing(10);
    auto* titre = new QLabel(
        QStringLiteral("<span style='font-size:20px; font-weight:700; color:#10142A'>Calcule une installation photovoltaïque</span><br>"
                       "<span style='color:#4A5270'>Choisis une taille : Prométhée forme les chaînes de modules d’après les températures du site, vérifie "
                       "l’onduleur, choisit les fusibles, les câbles, l’interrupteur-sectionneur, les parafoudres et les protections du côté alternatif "
                       "(guide UTE C 15-712-1), estime la production, puis dessine l’unifilaire et rédige la note de calcul. Le matériel générique se "
                       "remplace ensuite par le tien, fiche technique en main.</span>%1")
            .arg(aide("photovoltaique")));
    titre->setWordWrap(true);
    lienGlossaire(titre);
    col->addWidget(titre);
    auto* grille = new QGridLayout;
    grille->setSpacing(10);
    int k = 0;
    for (double kWc : PRESETS) {
      const InstallationPV i = installationPVType(kWc);
      const ResultatPV x = calculerPV(i);
      auto* b = new QPushButton;
      b->setObjectName(QStringLiteral("pv-%1").arg(static_cast<int>(kWc)));
      b->setStyleSheet(QStringLiteral("QPushButton#%1 { border: 1px solid #CDD3E4; border-radius: 10px; background: palette(base); }"
                                      "QPushButton#%1:hover { border-color: #E0A040; }")
                           .arg(b->objectName()));
      auto* ligneCarte = new QHBoxLayout(b);
      ligneCarte->setContentsMargins(14, 10, 14, 10);
      ligneCarte->setSpacing(12);
      auto* image = new QLabel;
      image->setPixmap(icone(QStringLiteral("photovoltaique")).pixmap(QSize(34, 34)));
      image->setAttribute(Qt::WA_TransparentForMouseEvents);
      ligneCarte->addWidget(image, 0, Qt::AlignTop);
      const QString usage = kWc <= 9 ? QStringLiteral("Maison") : kWc <= 36 ? QStringLiteral("Toiture") : kWc <= 100 ? QStringLiteral("Bâtiment") : QStringLiteral("Grande toiture");
      auto* texte = new QLabel(QStringLiteral("<b>%1 · %2 kWc</b><br><span style='color:#6B7393'>%3 modules de %4 Wc, %5%6 ; ≈ %7 kWh par an</span>")
                                   .arg(usage, nombre(kWc, 0))
                                   .arg(i.nombreModules)
                                   .arg(nombre(i.module.puissance, 0))
                                   .arg(i.onduleurs > 1 ? QStringLiteral("%1 × ").arg(i.onduleurs) : QString(), q(i.onduleur.nom))
                                   .arg(nombre(x.production, 0)));
      texte->setWordWrap(true);
      texte->setAttribute(Qt::WA_TransparentForMouseEvents);
      ligneCarte->addWidget(texte, 1);
      b->setMinimumHeight(82);
      b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
      connect(b, &QPushButton::clicked, this, [this, kWc] { appliquerPV(kWc); });
      grille->addWidget(b, k / 2, k % 2);
      ++k;
    }
    col->addLayout(grille);
    col->addStretch(1);
    defil->setWidget(w);
    m_pilePV->addWidget(defil);
  }
  // Résultats
  {
    auto* defil = new QScrollArea;
    defil->setWidgetResizable(true);
    defil->setFrameShape(QFrame::NoFrame);
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(12, 10, 12, 8);
    col->setSpacing(6);
    m_bandeauPV = new QLabel;
    m_bandeauPV->setObjectName(QStringLiteral("bandeauPV"));
    m_bandeauPV->setWordWrap(true);
    m_bandeauPV->setStyleSheet(QStringLiteral("QLabel { background: #F3F5FB; border: 1px solid #E1E5F0; border-radius: 9px; padding: 8px 10px; }"));
    lienGlossaire(m_bandeauPV);
    col->addWidget(m_bandeauPV);
    auto* boutons = new QHBoxLayout;
    boutons->setSpacing(4);
    auto compact = [](QPushButton* b) {
      b->setStyleSheet(QStringLiteral("QPushButton { padding: 4px 8px; }"));
      b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    };
    auto* uni = new QPushButton(icone(QStringLiteral("schema")), QStringLiteral("Dessiner l’unifilaire"));
    uni->setObjectName(QStringLiteral("dessinerUnifilairePV"));
    uni->setToolTip(QStringLiteral("Chaînes, protections, onduleurs, branchement, annotés des résultats ; redessiné à chaque fois"));
    connect(uni, &QPushButton::clicked, this, [this] {
      const int f = genererUnifilairePV();
      if (f >= 0) {
        choisirMode(Schema);
        allerAuFolio(f);
      }
    });
    compact(uni);
    boutons->addWidget(uni);
    auto* note = new QPushButton(icone(QStringLiteral("exporter")), QStringLiteral("Note de calcul…"));
    note->setToolTip(QStringLiteral("Matériel, calepinage, côtés continu et alternatif, production, avis, hypothèses"));
    connect(note, &QPushButton::clicked, this, [this] {
      const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Note de calcul photovoltaïque"),
                                                     q(projetCourant() ? projetCourant()->nom : std::string("photovoltaique")) + QStringLiteral(" - note de calcul.pdf"),
                                                     QStringLiteral("PDF (*.pdf);;Page web (*.html)"));
      if (!c.isEmpty()) m_etat->setText(exporterNotePV(c) ? QStringLiteral("Note de calcul écrite : %1").arg(c) : QStringLiteral("<span style='color:#C2402E'>Écriture impossible.</span>"));
    });
    compact(note);
    boutons->addWidget(note);
    boutons->addStretch(1);
    col->addLayout(boutons);
    auto* vue = new VueCalepinagePV;
    m_vueCalepinage = vue;
    col->addWidget(vue);
    m_detailPV = new QLabel;
    m_detailPV->setObjectName(QStringLiteral("resultatsPV"));
    m_detailPV->setWordWrap(true);
    m_detailPV->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lienGlossaire(m_detailPV);
    col->addWidget(m_detailPV);
    col->addStretch(1);
    defil->setWidget(w);
    m_pilePV->addWidget(defil);
  }
  return racine;
}

void AtelierElectrique::majPV() {
  if (!m_pilePV) return;
  const ProjetElectrique* pe = projetCourant();
  const bool existe = pe && pe->photovoltaique;
  m_pilePV->setCurrentIndex(existe ? 1 : 0);
  if (!existe) {
    m_resultatPV = {};
    return;
  }
  const InstallationPV& i = *pe->photovoltaique;
  m_resultatPV = calculerPV(i);
  const ResultatPV& x = m_resultatPV;
  const QColor c = couleurEtat(x.erreurs(), x.alertes());
  m_bandeauPV->setText(QStringLiteral("<b>%1</b> · <b>%2 kWc</b>%3 · %4 modules de %5 Wc en %6 chaîne(s) de %7 · %8%9 · production ≈ <b>%10 kWh/an</b>%11<br>"
                                      "<span style='color:%12; font-weight:600'>%13 %14 erreur(s) · %15 alerte(s)</span>")
                           .arg(q(i.nom).toHtmlEscaped(), nombre(x.puissanceCrete, 2), aide("kwc"))
                           .arg(x.modulesPoses)
                           .arg(nombre(i.module.puissance, 0))
                           .arg(x.chaines)
                           .arg(x.modulesParChaine)
                           .arg(i.onduleurs > 1 ? QStringLiteral("%1 × ").arg(i.onduleurs) : QString(), q(i.onduleur.nom).toHtmlEscaped(), nombre(x.production, 0), aide("productible"))
                           .arg(c.name(), symboleEtat(x.erreurs(), x.alertes()))
                           .arg(x.erreurs())
                           .arg(x.alertes()));
  static_cast<VueCalepinagePV*>(m_vueCalepinage)->setDonnees(i, x);
  m_detailPV->setText(resultatsPV(i, x));
}

// ---------- Panneau de droite : matériel, site, production ----------
void AtelierElectrique::panneauPV(QVBoxLayout* col) {
  const ProjetElectrique& pe = *projetCourant();
  if (!pe.photovoltaique) {
    auto* l = new QLabel(QStringLiteral("<b>Pas encore d’installation photovoltaïque.</b><br>Choisis une taille au centre : modules, onduleur et site arrivent avec "
                                        "des valeurs courantes, que tu remplaces ensuite par celles de ton matériel."));
    l->setWordWrap(true);
    col->addWidget(l);
    return;
  }
  const InstallationPV& i = *pe.photovoltaique;
  const ResultatPV x = calculerPV(i);
  const FormulaireLie F(this, col);
  auto modifier = [this](const std::function<void(InstallationPV&)>& f) { modifierPV(f); };
  const QColor ce = couleurEtat(x.erreurs(), x.alertes());
  auto* tete = new QLabel(QStringLiteral("<span style='font-size:14px; font-weight:700'>%1</span><br><span style='color:%2'>%3 %4</span>")
                              .arg(q(i.nom).toHtmlEscaped(), ce.name(), symboleEtat(x.erreurs(), x.alertes()),
                                   x.erreurs() ? QStringLiteral("%1 erreur(s) à corriger").arg(x.erreurs())
                                   : x.alertes() ? QStringLiteral("%1 point(s) à surveiller").arg(x.alertes())
                                                 : QStringLiteral("conforme")));
  tete->setObjectName(QStringLiteral("tetePV"));
  tete->setWordWrap(true);
  col->addWidget(tete);
  // Ce qui demande une action d'abord ; les informations sont au centre
  std::vector<AvisCalcul> aCorriger;
  for (const auto& a : x.avis)
    if (a.niveau != NiveauAvis::Info) aCorriger.push_back(a);
  F.avis(aCorriger, QStringLiteral("avisPV"));
  // ----- Champ -----
  {
    QFormLayout* f = F.groupe(QStringLiteral("Champ photovoltaïque"), QStringLiteral("groupeChampPV"));
    F.texte(f, QStringLiteral("Nom"), i.nom, QStringLiteral("nomPV"), [modifier](const std::string& v) { modifier([v](InstallationPV& z) { z.nom = v; }); });
    F.entier(f, QStringLiteral("Modules posés"), i.nombreModules, 1, 100000, QStringLiteral("modulesPV"), [modifier](int v) { modifier([v](InstallationPV& z) { z.nombreModules = v; }); });
    F.entier(f, QStringLiteral("Modules par chaîne"), i.modulesParChaine, 0, 80, QStringLiteral("serieePV"), [modifier](int v) { modifier([v](InstallationPV& z) { z.modulesParChaine = v; }); },
             QStringLiteral("« auto » : la plus longue chaîne admise qui pose tous les modules"), QStringLiteral("auto"));
    F.entier(f, QStringLiteral("Onduleurs"), i.onduleurs, 1, 100, QStringLiteral("onduleursPV"), [modifier](int v) { modifier([v](InstallationPV& z) { z.onduleurs = v; }); });
  }
  // ----- Module -----
  {
    QFormLayout* f = F.groupe(QStringLiteral("Module"), QStringLiteral("groupeModulePV"));
    QStringList noms;
    int courant = static_cast<int>(modulesPV().size());
    for (std::size_t k = 0; k < modulesPV().size(); ++k) {
      noms << q(modulesPV()[k].nom);
      if (modulesPV()[k].nom == i.module.nom) courant = static_cast<int>(k);
    }
    noms << QStringLiteral("Mon module (valeurs saisies)");
    F.choix(f, QStringLiteral("Modèle"), noms, courant, QStringLiteral("modelePV"), [modifier](int k) {
      if (k < static_cast<int>(modulesPV().size())) modifier([k](InstallationPV& z) { z.module = modulesPV()[static_cast<std::size_t>(k)]; });
    }, QStringLiteral("un module générique, ou les valeurs de ta fiche technique"));
    auto champModule = [modifier](double ModulePV::*membre) {
      return [modifier, membre](double v) {
        modifier([membre, v](InstallationPV& z) {
          z.module.*membre = v;
          if (z.module.nom.rfind("Mon module", 0) != 0) z.module.nom = "Mon module";
        });
      };
    };
    F.reel(f, QStringLiteral("Puissance"), i.module.puissance, 10, 1000, 0, QStringLiteral(" Wc"), QStringLiteral("puissanceModulePV"), champModule(&ModulePV::puissance));
    F.reel(f, QStringLiteral("Tension à vide Voc"), i.module.voc, 1, 200, 2, QStringLiteral(" V"), QStringLiteral("vocPV"), champModule(&ModulePV::voc));
    F.reel(f, QStringLiteral("Tension Vmp"), i.module.vmp, 1, 200, 2, QStringLiteral(" V"), QStringLiteral("vmpPV"), champModule(&ModulePV::vmp));
    F.reel(f, QStringLiteral("Courant Isc"), i.module.isc, 0.1, 50, 2, QStringLiteral(" A"), QStringLiteral("iscPV"), champModule(&ModulePV::isc));
    F.reel(f, QStringLiteral("Courant Imp"), i.module.imp, 0.1, 50, 2, QStringLiteral(" A"), QStringLiteral("impPV"), champModule(&ModulePV::imp));
    F.reel(f, QStringLiteral("Coefficient de Voc"), i.module.coefVoc, -1, 0, 3, QStringLiteral(" %/°C"), QStringLiteral("coefVocPV"), champModule(&ModulePV::coefVoc),
           QStringLiteral("fiche technique, souvent −0,25 à −0,30 ; 0 : inconnu (tableau selon la température minimale)"));
    F.reel(f, QStringLiteral("Tension maximale du système"), i.module.tensionMax, 100, 1500, 0, QStringLiteral(" V"), QStringLiteral("tensionMaxPV"), champModule(&ModulePV::tensionMax));
    F.reel(f, QStringLiteral("Courant inverse IRM"), i.module.courantInverse, 1, 100, 0, QStringLiteral(" A"), QStringLiteral("irmPV"), champModule(&ModulePV::courantInverse),
           QStringLiteral("« calibre maximal du fusible de série » de la fiche technique"));
  }
  // ----- Onduleur -----
  {
    QFormLayout* f = F.groupe(QStringLiteral("Onduleur"), QStringLiteral("groupeOnduleurPV"));
    QStringList noms;
    int courant = static_cast<int>(onduleursPV().size());
    for (std::size_t k = 0; k < onduleursPV().size(); ++k) {
      noms << q(onduleursPV()[k].nom);
      if (onduleursPV()[k].nom == i.onduleur.nom) courant = static_cast<int>(k);
    }
    noms << QStringLiteral("Mon onduleur (valeurs saisies)");
    F.choix(f, QStringLiteral("Modèle"), noms, courant, QStringLiteral("modeleOnduleurPV"), [modifier](int k) {
      if (k < static_cast<int>(onduleursPV().size())) modifier([k](InstallationPV& z) { z.onduleur = onduleursPV()[static_cast<std::size_t>(k)]; });
    });
    auto champ = [modifier](double OnduleurPV::*membre) {
      return [modifier, membre](double v) {
        modifier([membre, v](InstallationPV& z) {
          z.onduleur.*membre = v;
          if (z.onduleur.nom.rfind("Mon onduleur", 0) != 0) z.onduleur.nom = "Mon onduleur";
        });
      };
    };
    F.reel(f, QStringLiteral("Puissance de sortie"), i.onduleur.puissanceAC, 100, 5e6, 0, QStringLiteral(" VA"), QStringLiteral("puissanceOnduleurPV"), champ(&OnduleurPV::puissanceAC));
    F.reel(f, QStringLiteral("Puissance continue maximale"), i.onduleur.puissanceDCMax, 100, 1e7, 0, QStringLiteral(" W"), QStringLiteral("puissanceDCPV"), champ(&OnduleurPV::puissanceDCMax));
    F.reel(f, QStringLiteral("Tension continue maximale"), i.onduleur.tensionDCMax, 50, 1500, 0, QStringLiteral(" V"), QStringLiteral("tensionDCPV"), champ(&OnduleurPV::tensionDCMax));
    F.reel(f, QStringLiteral("Plage MPPT, de"), i.onduleur.mpptMin, 0, 1500, 0, QStringLiteral(" V"), QStringLiteral("mpptMinPV"), champ(&OnduleurPV::mpptMin));
    F.reel(f, QStringLiteral("à"), i.onduleur.mpptMax, 0, 1500, 0, QStringLiteral(" V"), QStringLiteral("mpptMaxPV"), champ(&OnduleurPV::mpptMax));
    F.reel(f, QStringLiteral("Courant exploité par entrée"), i.onduleur.courantMppt, 1, 1000, 1, QStringLiteral(" A"), QStringLiteral("courantMpptPV"), champ(&OnduleurPV::courantMppt));
    F.reel(f, QStringLiteral("Isc maximal par entrée"), i.onduleur.iscMppt, 1, 1000, 1, QStringLiteral(" A"), QStringLiteral("iscMpptPV"), champ(&OnduleurPV::iscMppt));
    F.entier(f, QStringLiteral("Entrées MPPT"), i.onduleur.mppt, 1, 32, QStringLiteral("mpptPV"), [modifier](int v) {
      modifier([v](InstallationPV& z) {
        z.onduleur.mppt = v;
        if (z.onduleur.nom.rfind("Mon onduleur", 0) != 0) z.onduleur.nom = "Mon onduleur";
      });
    });
    F.entier(f, QStringLiteral("Connecteurs par entrée"), i.onduleur.entreesParMppt, 1, 16, QStringLiteral("connecteursPV"), [modifier](int v) {
      modifier([v](InstallationPV& z) {
        z.onduleur.entreesParMppt = v;
        if (z.onduleur.nom.rfind("Mon onduleur", 0) != 0) z.onduleur.nom = "Mon onduleur";
      });
    });
    F.coche(f, QStringLiteral("Sortie triphasée"), i.onduleur.triphase, QStringLiteral("triphasePV"), [modifier](bool v) { modifier([v](InstallationPV& z) { z.onduleur.triphase = v; }); });
    F.reel(f, QStringLiteral("Courant de sortie maximal"), i.onduleur.courantACMax, 0, 1e4, 1, QStringLiteral(" A"), QStringLiteral("courantACPV"), champ(&OnduleurPV::courantACMax),
           QStringLiteral("notice de l’onduleur ; « auto » : 1,1 fois le courant nominal"), QStringLiteral("auto"));
  }
  // ----- Site -----
  {
    QFormLayout* f = F.groupe(QStringLiteral("Site"), QStringLiteral("groupeSitePV"));
    F.reel(f, QStringLiteral("Température minimale"), i.tMin, -50, 40, 0, QStringLiteral(" °C"), QStringLiteral("tMinPV"), [modifier](double v) { modifier([v](InstallationPV& z) { z.tMin = v; }); },
           QStringLiteral("la plus froide du site : −10 °C en plaine, −15 à −25 °C en montagne"));
    F.reel(f, QStringLiteral("Cellules au plus chaud"), i.tMax, 20, 100, 0, QStringLiteral(" °C"), QStringLiteral("tMaxPV"), [modifier](double v) { modifier([v](InstallationPV& z) { z.tMax = v; }); },
           QStringLiteral("70 °C en toiture ventilée, 80 °C intégrée au bâti"));
    F.reel(f, QStringLiteral("Autour des câbles continus"), i.temperatureCables, -20, 110, 0, QStringLiteral(" °C"), QStringLiteral("tCablesPV"),
           [modifier](double v) { modifier([v](InstallationPV& z) { z.temperatureCables = v; }); }, QStringLiteral("70 °C sous les modules"));
    QStringList batiments;
    for (BatimentPV b : {BatimentPV::Habitation, BatimentPV::Tertiaire, BatimentPV::CentraleSol}) batiments << QString::fromUtf8(nomBatimentPV(b));
    F.choix(f, QStringLiteral("Bâtiment"), batiments, static_cast<int>(i.batiment), QStringLiteral("batimentPV"),
            [modifier](int k) { modifier([k](InstallationPV& z) { z.batiment = static_cast<BatimentPV>(k); }); }, QStringLiteral("fixe la longueur critique des parafoudres et le différentiel"));
    F.reel(f, QStringLiteral("Foudroiement Ng"), i.ng, 0.1, 20, 1, QStringLiteral(" /km²/an"), QStringLiteral("ngPV"), [modifier](double v) { modifier([v](InstallationPV& z) { z.ng = v; }); },
           QStringLiteral("1 à 2 sur la plus grande partie de la France, 3 et plus dans le Sud-Est et en montagne"));
    F.coche(f, QStringLiteral("Bâtiment équipé d’un paratonnerre"), i.paratonnerre, QStringLiteral("paratonnerrePV"),
            [modifier](bool v) { modifier([v](InstallationPV& z) { z.paratonnerre = v; }); });
    F.reel(f, QStringLiteral("Câbles continus"), i.longueurDC, 0, 10000, 0, QStringLiteral(" m"), QStringLiteral("longueurDCPV"), [modifier](double v) { modifier([v](InstallationPV& z) { z.longueurDC = v; }); },
           QStringLiteral("de la chaîne la plus éloignée à l’onduleur"));
    F.reel(f, QStringLiteral("Câble alternatif"), i.longueurAC, 0, 10000, 0, QStringLiteral(" m"), QStringLiteral("longueurACPV"), [modifier](double v) { modifier([v](InstallationPV& z) { z.longueurAC = v; }); },
           QStringLiteral("de l’onduleur au tableau"));
    const std::vector<double> sectionsDC = {0, 2.5, 4, 6, 10, 16, 25, 35, 50, 70, 95, 120};
    QStringList listeDC;
    int courantDC = 0;
    for (std::size_t k = 0; k < sectionsDC.size(); ++k) {
      listeDC << (k == 0 ? QStringLiteral("auto") : QStringLiteral("%1 mm²").arg(nombre(sectionsDC[k])));
      if (std::abs(sectionsDC[k] - i.sectionDC) < 1e-9) courantDC = static_cast<int>(k);
    }
    F.choix(f, QStringLiteral("Section continue"), listeDC, courantDC, QStringLiteral("sectionDCPV"), [modifier, sectionsDC](int k) {
      const double v = sectionsDC[static_cast<std::size_t>(k)];
      modifier([v](InstallationPV& z) { z.sectionDC = v; });
    });
    const std::vector<double>& sn = sectionsNormalisees();
    QStringList listeAC{QStringLiteral("auto")};
    int courantAC = 0;
    for (std::size_t k = 0; k < sn.size(); ++k) {
      listeAC << QStringLiteral("%1 mm²").arg(nombre(sn[k]));
      if (std::abs(sn[k] - i.sectionAC) < 1e-9) courantAC = static_cast<int>(k) + 1;
    }
    F.choix(f, QStringLiteral("Section alternative"), listeAC, courantAC, QStringLiteral("sectionACPV"), [modifier, sn](int k) {
      const double v = k == 0 ? 0.0 : sn[static_cast<std::size_t>(k - 1)];
      modifier([v](InstallationPV& z) { z.sectionAC = v; });
    });
  }
  // ----- Production et raccordement -----
  {
    QFormLayout* f = F.groupe(QStringLiteral("Production"), QStringLiteral("groupeProductionPV"));
    const auto& regions = productiblesIndicatifs();
    QStringList noms;
    int courant = static_cast<int>(regions.size());
    for (std::size_t k = 0; k < regions.size(); ++k) {
      noms << q(regions[k].first);
      if (std::abs(regions[k].second - i.productible) < 1e-9) courant = static_cast<int>(k);
    }
    noms << QStringLiteral("Valeur de PVGIS (saisie)");
    F.choix(f, QStringLiteral("Région"), noms, courant, QStringLiteral("regionPV"), [modifier](int k) {
      if (k < static_cast<int>(productiblesIndicatifs().size())) {
        const double v = productiblesIndicatifs()[static_cast<std::size_t>(k)].second;
        modifier([v](InstallationPV& z) { z.productible = v; });
      }
    }, QStringLiteral("ordre de grandeur ; PVGIS (Commission européenne) donne le productible d’une adresse"));
    F.reel(f, QStringLiteral("Productible"), i.productible, 0, 3000, 0, QStringLiteral(" kWh/kWc"), QStringLiteral("productiblePV"),
           [modifier](double v) { modifier([v](InstallationPV& z) { z.productible = v; }); }, QStringLiteral("par an, plein sud incliné à 30°, pertes comprises"));
    F.reel(f, QStringLiteral("Écart au sud"), i.orientation, -180, 180, 0, QStringLiteral("°"), QStringLiteral("orientationPV"),
           [modifier](double v) { modifier([v](InstallationPV& z) { z.orientation = v; }); }, QStringLiteral("0 plein sud, −90 est, +90 ouest"));
    F.reel(f, QStringLiteral("Inclinaison"), i.inclinaison, 0, 90, 0, QStringLiteral("°"), QStringLiteral("inclinaisonPV"),
           [modifier](double v) { modifier([v](InstallationPV& z) { z.inclinaison = v; }); }, QStringLiteral("0 à plat, 30 pour un toit courant, 90 en façade"));
    QFormLayout* r = F.groupe(QStringLiteral("Raccordement"), QStringLiteral("groupeRaccordementPV"));
    F.coche(r, QStringLiteral("Vente totale (branchement et compteur propres)"), i.venteTotale, QStringLiteral("venteTotalePV"),
            [modifier](bool v) { modifier([v](InstallationPV& z) { z.venteTotale = v; }); }, QStringLiteral("sinon : autoconsommation, circuit dédié du tableau"));
  }
  // ----- Actions -----
  {
    auto* g = new QGroupBox(QStringLiteral("Actions"));
    auto* v = new QVBoxLayout(g);
    auto* uni = new QPushButton(icone(QStringLiteral("schema")), QStringLiteral("Dessiner l’unifilaire"));
    uni->setObjectName(QStringLiteral("unifilairePVPanneau"));
    connect(uni, &QPushButton::clicked, this, [this] {
      const int f = genererUnifilairePV();
      if (f >= 0) {
        choisirMode(Schema);
        allerAuFolio(f);
      }
    });
    v->addWidget(uni);
    auto* sup = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Retirer le photovoltaïque"));
    sup->setObjectName(QStringLiteral("supprimerPV"));
    sup->setToolTip(QStringLiteral("le calcul part ; l’unifilaire déjà dessiné reste (Ctrl+Z revient)"));
    connect(sup, &QPushButton::clicked, this, [this] {
      modifierProjet([](ProjetElectrique& p) { p.photovoltaique.reset(); });
      m_etat->setText(QStringLiteral("Installation photovoltaïque retirée ; Ctrl+Z la rétablit."));
    });
    v->addWidget(sup);
    col->addWidget(g);
  }
}
