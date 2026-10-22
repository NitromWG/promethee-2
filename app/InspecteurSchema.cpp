// Prométhée : pages de l'inspecteur pour le schéma (lot L1, tâche 1.9) : composant, alimentation, étiquette, texte,
// fil, feuille. Chaque page crée ses widgets une fois ; « relire » les remplit depuis le modèle, sans toucher au
// champ qui a le focus. Voir InspecteurElectronique.hpp.
// Licence GPL-3.0-only.
#include <algorithm>
#include <optional>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "Icones.hpp"
#include "InspecteurElectronique.hpp"
#include "VueSchema.hpp"
#include "promethee/circuit.hpp"
#include "promethee/electronique.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }

// Une page du schéma : la feuille montrée et l'objet choisi
class PageSchema : public PageInspecteur {
 public:
  using PageInspecteur::PageInspecteur;
  void montrer() override {
    const VueSchema* vs = m_atelier.vueSchema();
    m_feuille = vs->feuille();
    m_objet = vs->selection().size() == 1 ? *vs->selection().begin() : std::string();
    PageInspecteur::montrer();
  }

 protected:
  const ConceptionElec* conception() const { return m_atelier.conceptionCourante(); }
  const Feuille* feuille() const {
    const ConceptionElec* c = conception();
    return c && m_feuille >= 0 && m_feuille < static_cast<int>(c->feuilles.size()) ? &c->feuilles[static_cast<std::size_t>(m_feuille)] : nullptr;
  }
  // Modifie la feuille montrée, en un pas ; « saisie » non nul : les écritures d'un même passage dans un champ font un pas
  void modifierFeuille(const std::function<void(Feuille&)>& f, std::uint64_t saisie = 0) {
    const int fi = m_feuille;
    m_atelier.modifierConception([&](ConceptionElec& c) {
      if (fi >= 0 && fi < static_cast<int>(c.feuilles.size())) f(c.feuilles[static_cast<std::size_t>(fi)]);
    }, {}, saisie ? QStringLiteral("saisie:%1").arg(saisie) : QString());
  }
  QGroupBox* groupe(const QString& titre, QFormLayout*& fl) {
    auto* g = new QGroupBox(titre);
    fl = new QFormLayout(g);
    fl->setRowWrapPolicy(QFormLayout::WrapLongRows);
    colonne()->addWidget(g);
    return g;
  }
  int m_feuille = 0;
  std::string m_objet;
};

// ---------- Composant ----------
class PageComposant : public PageSchema {
 public:
  explicit PageComposant(AtelierElectronique& a) : PageSchema(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      const SymbolePlace* s = symbole();
      const DefSymbole* d = definition();
      const ComposantElec* x = composant();
      if (!s || !d || !x) return;
      titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span> <span style='color:%5'>%2</span><br><span style='color:%5'>%3 · %4 broche(s)</span>")
                         .arg(q(x->reference + (d->unites > 1 ? lettreUnite(s->unite) : std::string())).toHtmlEscaped(), q(x->valeur).toHtmlEscaped(), q(d->nom).toHtmlEscaped())
                         .arg(d->broches.size())
                         .arg(teintes::secondaire()));
    });
    construireUnites();
    construireComposant();
    construireApprovisionnement();
    construireBroches();
    colonne()->addStretch(1);
  }
  void resultats() override { relire(); }

 private:
  const SymbolePlace* symbole() const {
    if (const Feuille* f = feuille())
      for (const auto& s : f->symboles)
        if (s.uuid == m_objet) return &s;
    return nullptr;
  }
  const ComposantElec* composant() const {
    const SymbolePlace* s = symbole();
    return s && conception() ? conception()->composant(s->composant) : nullptr;
  }
  const DefSymbole* definition() const {
    const SymbolePlace* s = symbole();
    return s && conception() ? conception()->symbole(s->symbole) : nullptr;
  }
  std::string idComposant() const {
    const SymbolePlace* s = symbole();
    return s ? s->composant : std::string();
  }
  void modifierComposant(const std::function<void(ComposantElec&)>& f) {
    const std::string id = idComposant();
    m_atelier.modifierConception([&](ConceptionElec& c) {
      if (ComposantElec* k = c.composant(id)) f(*k);
    });
  }

  // Symbole à plusieurs unités : unité de ce symbole, unités posées et pose de la suivante
  void construireUnites() {
    QFormLayout* fu = nullptr;
    auto* g = groupe(QStringLiteral("Unités"), fu);
    auto* choix = new QComboBox;
    choix->setObjectName(QStringLiteral("uniteSymbole"));
    connect(choix, &QComboBox::activated, this, [this, choix](int) {
      const int u = choix->currentData().toInt();
      const std::string id = m_objet;
      modifierFeuille([&](Feuille& f) {
        for (auto& y : f.symboles)
          if (y.uuid == id) y.unite = u;
      });
    });
    fu->addRow(QStringLiteral("Unité"), choix);
    lier(choix, [this, choix] {
      const DefSymbole* d = definition();
      const SymbolePlace* s = symbole();
      if (!d || !s) return;
      const std::string signature = d->id + '#' + std::to_string(d->unites);
      if (choix->property("signature").toString().toStdString() != signature) {
        choix->clear();
        for (int u = 1; u <= d->unites; ++u) {
          QStringList bs;
          for (const auto& b : d->broches)
            if (b.unite == u) bs << q(b.numero);
          const auto iu = static_cast<std::size_t>(u - 1);
          const QString nomU = iu < d->nomsUnites.size() && !d->nomsUnites[iu].empty() ? QStringLiteral(" · ") + q(d->nomsUnites[iu]) : QString();
          choix->addItem(QStringLiteral("%1%2 (%3)").arg(q(lettreUnite(u)), nomU, bs.isEmpty() ? QStringLiteral("sans broche") : QStringLiteral("broches ") + bs.join(QStringLiteral(", "))), u);
        }
        choix->setProperty("signature", q(signature));
      }
      choix->setCurrentIndex(std::max(0, choix->findData(s->unite)));
    });
    auto* etat = inspecteur::texte();
    fu->addRow(etat);
    auto* suivante = new QPushButton(QStringLiteral("Poser l’unité suivante"));
    suivante->setObjectName(QStringLiteral("poserUniteSuivante"));
    connect(suivante, &QPushButton::clicked, this, [this] { m_atelier.poserUniteSuivante(m_objet, m_feuille); });
    fu->addRow(suivante);
    action(suivante);
    suivre([this, g, etat, suivante] {
      const DefSymbole* d = definition();
      const ConceptionElec* c = conception();
      g->setVisible(d && d->unites > 1);
      if (!d || !c || d->unites <= 1) return;
      const std::set<int> posees = unitesPosees(*c, idComposant());
      QStringList manquantes;
      for (int u = 1; u <= d->unites; ++u)
        if (!posees.count(u)) manquantes << q(lettreUnite(u));
      etat->setText(manquantes.isEmpty() ? QStringLiteral("Toutes les unités sont posées.")
                                         : QStringLiteral("<span style='color:%1'>Unité(s) non posée(s) : %2</span>").arg(teintes::alerte(), manquantes.join(QStringLiteral(", "))));
      suivante->setEnabled(!manquantes.isEmpty() && !lectureSeule());
    });
  }

  void construireComposant() {
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Composant"), fl);
    auto idc = [this] { return idComposant(); };
    auto* reference = inspecteur::champComposant(m_atelier, idc, "reference");
    fl->addRow(QStringLiteral("Référence"), reference);
    lier(reference, [reference] { reference->relire(); });
    auto* valeur = inspecteur::champComposant(m_atelier, idc, "valeur");
    fl->addRow(QStringLiteral("Valeur"), valeur);
    lier(valeur, [valeur] { valeur->relire(); });

    // Empreinte : celles du catalogue au bon nombre de pastilles, puis celles du projet et de la bibliothèque personnelle
    auto* emp = new QComboBox;
    emp->setObjectName(QStringLiteral("empreinteComposant"));
    emp->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    emp->setMinimumContentsLength(14);
    connect(emp, &QComboBox::activated, this, [this, emp](int) {
      const std::string v = emp->currentData().toString().toStdString();
      // Une empreinte de la bibliothèque personnelle est figée dans le projet au moment du choix
      std::optional<DefEmpreinte> perso;
      if (const ConceptionElec* c0 = conception(); c0 && !c0->empreintes.count(v))
        for (const auto& de : AtelierElectronique::empreintesPerso())
          if (de.id == v) perso = de;
      const std::string id = idComposant();
      m_atelier.modifierConception([&](ConceptionElec& c2) {
        if (perso) c2.empreintes[v] = *perso;
        if (auto* k = c2.composant(id)) k->empreinte = v;
      });
    });
    fl->addRow(QStringLiteral("Empreinte"), emp);
    lier(emp, [this, emp] {
      const ConceptionElec* c = conception();
      const DefSymbole* d = definition();
      const ComposantElec* x = composant();
      if (!c || !d || !x) return;
      std::set<std::string> numeros;
      for (const auto& b : d->broches) numeros.insert(b.numero);
      const std::vector<DefEmpreinte> persos = AtelierElectronique::empreintesPerso();
      std::string signature = d->id + '#' + std::to_string(numeros.size()) + '#' + x->empreinte + '#' + std::to_string(persos.size()) + '#';
      for (const auto& [ide, de] : c->empreintes) signature += ide + ',';
      if (emp->property("signature").toString().toStdString() != signature) {
        emp->clear();
        emp->addItem(QStringLiteral("— aucune —"), QString());
        for (const auto& i : catalogueEmpreintes())
          if (i.pastilles == static_cast<int>(numeros.size()) || i.pastilles == static_cast<int>(numeros.size()) + 1 || i.id == x->empreinte)
            emp->addItem(q(i.id) + QStringLiteral("  ·  ") + q(i.description), q(i.id));
        std::set<std::string> vues;
        auto proposer = [&](const DefEmpreinte& de) {
          std::set<std::string> p;
          for (const auto& pa : de.pastilles) p.insert(pa.numero);
          if (!vues.insert(de.id).second || emp->findData(q(de.id)) >= 0) return;
          if (p.size() == numeros.size() || de.id == x->empreinte) emp->addItem(q(de.id) + QStringLiteral("  ·  ") + q(de.description), q(de.id));
        };
        for (const auto& [ide, de] : c->empreintes)
          if (ide.rfind("perso:", 0) == 0 || ide.rfind("kicad:", 0) == 0) proposer(de);
        for (const auto& de : persos) proposer(de);
        if (!x->empreinte.empty() && emp->findData(q(x->empreinte)) < 0) emp->addItem(q(x->empreinte), q(x->empreinte));
        emp->setProperty("signature", q(signature));
      }
      emp->setCurrentIndex(std::max(0, emp->findData(q(x->empreinte))));
    });

    // Garde-fou du brochage (B17, ERC-09) : une empreinte d'une autre famille de boîtier que celle pour laquelle le
    // symbole est numéroté bloque la publication tant que la correspondance n'est pas vérifiée sur la fiche
    auto* alerte = inspecteur::texte();
    fl->addRow(alerte);
    // Libellés courts : une case ne passe pas à la ligne, et le panneau suit sa largeur (P25)
    auto* verifie = new QCheckBox(QStringLiteral("Brochage vérifié sur la fiche"));
    verifie->setObjectName(QStringLiteral("brochageVerifie"));
    verifie->setToolTip(QStringLiteral("Brochage vérifié sur la fiche technique pour cette empreinte. Coche seulement après avoir vérifié que chaque broche du symbole arrive sur la pastille de même fonction."));
    connect(verifie, &QCheckBox::toggled, this, [this](bool on) { modifierComposant([on](ComposantElec& k) { k.brochageVerifie = on ? k.empreinte : std::string(); }); });
    fl->addRow(verifie);
    lier(verifie, [this, verifie] {
      if (const ComposantElec* x = composant()) verifie->setChecked(!x->brochageVerifie.empty() && x->brochageVerifie == x->empreinte);
    });
    suivre([this, alerte, verifie] {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      const DefSymbole* d = definition();
      const bool aVerifier = c && x && brochageAVerifier(*c, *x);
      alerte->setVisible(aVerifier);
      verifie->setVisible(aVerifier || (x && !x->brochageVerifie.empty() && x->brochageVerifie == x->empreinte));
      if (aVerifier && d)
        alerte->setText(QStringLiteral("<span style='color:%1'><b>Brochage à vérifier</b></span> : le symbole est numéroté pour <b>%2</b>, l’empreinte est d’une autre famille de boîtier. "
                                       "Une broche peut arriver sur la mauvaise pastille (drain et source d’un MOSFET).")
                            .arg(teintes::erreur(), q(d->empreinte).toHtmlEscaped()));
    });
    {
      auto* boutons = new QWidget;
      auto* hb = new QHBoxLayout(boutons);
      hb->setContentsMargins(0, 0, 0, 0);
      auto* creerE = new QPushButton(icone(QStringLiteral("pcb")), QStringLiteral("Créer…"));
      creerE->setToolTip(QStringLiteral("Créer une empreinte par tableau de pastilles (connecteur, barrette, bornier, boîtier DIL)"));
      connect(creerE, &QPushButton::clicked, this, [this] { m_atelier.creerEmpreinte(idComposant()); });
      auto* kicadE = new QPushButton(icone(QStringLiteral("kicad")), QStringLiteral("KiCad…"));
      kicadE->setToolTip(QStringLiteral("Importer une empreinte KiCad (.kicad_mod)"));
      connect(kicadE, &QPushButton::clicked, this, [this] { m_atelier.importerEmpreinteKicad(idComposant()); });
      hb->addWidget(creerE);
      hb->addWidget(kicadE);
      action(creerE);
      action(kicadE);
      fl->addRow(QString(), boutons);
    }
    // Hauteur du corps (boîtier mécanique) : générique tant qu'elle n'est pas vérifiée sur la fiche technique
    auto* h = new QDoubleSpinBox;
    h->setObjectName(QStringLiteral("hauteurComposant"));
    h->setRange(0, 200);
    h->setDecimals(2);
    h->setSuffix(QStringLiteral(" mm"));
    connect(h, &QDoubleSpinBox::editingFinished, this, [this, h] {
      const double v = h->value();
      const ComposantElec* x = composant();
      if (!x || std::abs(x->hauteur - v) < 1e-9) return;
      modifierComposant([v](ComposantElec& k) {
        k.hauteur = v;
        if (v > 0) k.hauteurVerifiee = true;  // une hauteur saisie vient de la fiche technique
      });
    });
    fl->addRow(QStringLiteral("Hauteur de ce composant"), h);  // distincte de la hauteur par défaut de l'empreinte (T2)
    lier(h, [this, h] {
      const ConceptionElec* c = conception();
      const ComposantElec* x = composant();
      if (!c || !x) return;
      const DefEmpreinte* de = x->empreinte.empty() ? nullptr : c->empreinte(x->empreinte);
      const double generique = de ? mm(de->corps.hauteur) : 0;
      h->setSpecialValueText(generique > 0 ? QStringLiteral("%1 mm (générique)").arg(QLocale().toString(generique, 'f', 2)) : QStringLiteral("générique"));
      h->setValue(x->hauteur);
    });
    auto* verif = new QCheckBox(QStringLiteral("Hauteur vérifiée"));
    verif->setObjectName(QStringLiteral("hauteurVerifiee"));
    verif->setToolTip(QStringLiteral("Vérifiée sur la fiche technique ou un modèle exact. Sans vérification, le contrôle du boîtier qui contient cette carte reste « incomplet »."));
    connect(verif, &QCheckBox::toggled, this, [this](bool on) { modifierComposant([on](ComposantElec& k) { k.hauteurVerifiee = on; }); });
    fl->addRow(verif);
    lier(verif, [this, verif] {
      if (const ComposantElec* x = composant()) verif->setChecked(x->hauteurVerifiee);
    });
    auto* edition = new QPushButton(icone(QStringLiteral("nouvelle-piece")), QString());
    edition->setObjectName(QStringLiteral("editionComposant"));
    connect(edition, &QPushButton::clicked, this, [this] {
      const DefSymbole* d = definition();
      if (!d) return;
      const DefSymbole copie = *d;
      m_atelier.creerComposant(&copie, copie.id.rfind("perso:", 0) == 0);
    });
    fl->addRow(edition);
    action(edition);
    suivre([this, edition] {
      const DefSymbole* d = definition();
      if (!d) return;
      const bool perso = d->id.rfind("perso:", 0) == 0;
      edition->setText(perso ? QStringLiteral("Modifier ce composant…") : QStringLiteral("Créer une variante…"));
      edition->setToolTip(perso ? QStringLiteral("Broches, empreinte, fabricant : tous les symboles de ce composant suivent")
                                : QStringLiteral("Un nouveau composant à toi, qui part de celui-ci (broches, empreinte)"));
    });
  }

  void construireApprovisionnement() {
    QFormLayout* f2 = nullptr;
    groupe(QStringLiteral("Approvisionnement"), f2);
    auto idc = [this] { return idComposant(); };
    for (const auto& [libelle, propriete] : std::initializer_list<std::pair<QString, std::string>>{{QStringLiteral("Fabricant"), "fabricant"},
                                                                                                    {QStringLiteral("Réf. fabricant"), "mpn"},
                                                                                                    {QStringLiteral("Fournisseur"), "fournisseur"},
                                                                                                    {QStringLiteral("Description"), "description"},
                                                                                                    {QStringLiteral("Fiche technique"), "fiche"}}) {
      auto* champ = inspecteur::champComposant(m_atelier, idc, propriete);
      f2->addRow(libelle, champ);
      lier(champ, [champ] { champ->relire(); });
    }
    // Fiche technique : le lien qui l'ouvre (EL-42)
    auto* lien = inspecteur::texte();
    lien->setOpenExternalLinks(true);
    f2->addRow(QString(), lien);
    suivre([this, lien] {
      const ComposantElec* x = composant();
      lien->setVisible(x && !x->fiche.empty());
      if (x && !x->fiche.empty()) lien->setText(QStringLiteral("<a href=\"%1\" style='color:%2'>Ouvrir la fiche</a>").arg(q(x->fiche).toHtmlEscaped(), teintes::lien()));
    });
    // Champs libres : nom = valeur, un par ligne ; une ligne vide retire un champ
    auto* champs = new QPlainTextEdit;
    champs->setObjectName(QStringLiteral("champsLibres"));
    champs->setPlaceholderText(QStringLiteral("Tolérance = 1 %\nPuissance = 0,25 W"));
    champs->setMaximumHeight(90);
    champs->setToolTip(QStringLiteral("Un champ par ligne, sous la forme « nom = valeur » ; ils passent dans la nomenclature."));
    lier(champs, [this, champs] {
      const ComposantElec* x = composant();
      if (!x) return;
      QString t;
      for (const auto& [nom, valeur] : x->champs) t += q(nom) + QStringLiteral(" = ") + q(valeur) + QLatin1Char('\n');
      if (champs->toPlainText() != t) champs->setPlainText(t);
    });
    auto* appliquer = new QPushButton(QStringLiteral("Appliquer les champs"));
    connect(appliquer, &QPushButton::clicked, this, [this, champs] {
      std::map<std::string, std::string> m;
      for (const QString& l : champs->toPlainText().split(QLatin1Char('\n'))) {
        const int i = l.indexOf(QLatin1Char('='));
        if (i <= 0) continue;
        const QString nom = l.left(i).trimmed(), valeur = l.mid(i + 1).trimmed();
        if (!nom.isEmpty()) m[nom.toStdString()] = valeur.toStdString();
      }
      modifierComposant([&](ComposantElec& k) { k.champs = m; });
    });
    action(appliquer);
    f2->addRow(QStringLiteral("Champs libres"), champs);
    f2->addRow(QString(), appliquer);
    auto* dnp = new QCheckBox(QStringLiteral("Ne pas monter (variante)"));
    dnp->setObjectName(QStringLiteral("nePasMonter"));
    connect(dnp, &QCheckBox::toggled, this, [this](bool on) { modifierComposant([on](ComposantElec& k) { k.nePasMonter = on; }); });
    lier(dnp, [this, dnp] {
      if (const ComposantElec* x = composant()) dnp->setChecked(x->nePasMonter);
    });
    f2->addRow(dnp);
  }

  // Broches et leurs réseaux (résultat de la connectivité)
  void construireBroches() {
    auto* g = new QGroupBox(QStringLiteral("Broches"));
    auto* l3 = new QVBoxLayout(g);
    auto* lb = inspecteur::texte();
    lb->setObjectName(QStringLiteral("brochesComposant"));
    l3->addWidget(lb);
    colonne()->addWidget(g);
    suivre([this, lb] {
      const DefSymbole* d = definition();
      const std::string id = idComposant();
      if (!d) return;
      const Connectivite& k = m_atelier.calculs().k;
      QString t;
      std::set<std::string> listees;  // une broche commune répétée sur chaque unité n'est listée qu'une fois
      for (const auto& b : d->broches) {
        if (!listees.insert(b.numero).second) continue;
        const std::string r = k.nom(id, b.numero);
        const QString unite = d->unites > 1 && b.unite > 0 ? QStringLiteral(" <span style='color:%1'>(%2)</span>").arg(teintes::secondaire(), q(lettreUnite(b.unite))) : QString();
        t += QStringLiteral("%1 %2%5 — <span style='color:%6'>%3</span> — <b>%4</b><br>")
                 .arg(q(b.numero), b.nom == "~" ? QString() : q(b.nom).toHtmlEscaped(), QString::fromUtf8(nomTypeBroche(b.type)), r.empty() ? QStringLiteral("non connectée") : q(r).toHtmlEscaped(), unite,
                      teintes::secondaire());
      }
      lb->setText(t);
    });
  }
};

// ---------- Alimentation, drapeau ----------
class PageAlimentation : public PageSchema {
 public:
  explicit PageAlimentation(AtelierElectronique& a) : PageSchema(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      const DefSymbole* d = definition();
      if (d)
        titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span><br><span style='color:%3'>%2</span>")
                           .arg(q(d->nom).toHtmlEscaped(), q(d->description).toHtmlEscaped(), teintes::secondaire()));
    });
    QFormLayout* fl = nullptr;
    auto* g = groupe(QStringLiteral("Alimentation"), fl);
    auto* nom = new ChampTexteLie([this]() -> QString { const SymbolePlace* s = symbole(); return s ? q(s->nomAlim) : QString(); },
                                  [this](const QString& t, std::uint64_t saisie) {
                                    const std::string v = t.trimmed().toStdString(), id = m_objet;
                                    modifierFeuille([&](Feuille& f) { for (auto& y : f.symboles) if (y.uuid == id) y.nomAlim = v; }, saisie);
                                  },
                                  false);
    nom->setObjectName(QStringLiteral("champ:nomAlim"));
    nom->setValidation([](const QString& t) { return t.trimmed().isEmpty() ? QStringLiteral("Le nom du réseau ne peut pas être vide") : QString(); });
    fl->addRow(QStringLiteral("Nom du réseau"), nom);
    lier(nom, [nom] { nom->relire(); });
    suivre([this, g] {
      const DefSymbole* d = definition();
      g->setVisible(d && d->alimentation);
    });
    colonne()->addStretch(1);
  }

 private:
  const SymbolePlace* symbole() const {
    if (const Feuille* f = feuille())
      for (const auto& s : f->symboles)
        if (s.uuid == m_objet) return &s;
    return nullptr;
  }
  const DefSymbole* definition() const {
    const SymbolePlace* s = symbole();
    return s && conception() ? conception()->symbole(s->symbole) : nullptr;
  }
};

// ---------- Étiquette ----------
class PageEtiquette : public PageSchema {
 public:
  explicit PageEtiquette(AtelierElectronique& a) : PageSchema(a) {
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Étiquette"), fl);
    auto* nom = new ChampTexteLie([this]() -> QString { const Etiquette* e = etiquette(); return e ? q(e->texte) : QString(); },
                                  [this](const QString& t, std::uint64_t saisie) {
                                    const std::string v = t.trimmed().toStdString(), id = m_objet;
                                    modifierFeuille([&](Feuille& f) { for (auto& y : f.etiquettes) if (y.uuid == id) y.texte = v; }, saisie);
                                  },
                                  false);
    nom->setObjectName(QStringLiteral("champ:etiquette"));
    nom->setValidation([](const QString& t) { return t.trimmed().isEmpty() ? QStringLiteral("Le nom du réseau ne peut pas être vide") : QString(); });
    fl->addRow(QStringLiteral("Nom du réseau"), nom);
    lier(nom, [nom] { nom->relire(); });
    auto* glob = new QCheckBox(QStringLiteral("Globale (toutes les feuilles)"));
    connect(glob, &QCheckBox::toggled, this, [this](bool on) {
      const std::string id = m_objet;
      modifierFeuille([&](Feuille& f) { for (auto& y : f.etiquettes) if (y.uuid == id) y.type = on ? TypeEtiquette::Globale : TypeEtiquette::Locale; });
    });
    lier(glob, [this, glob] {
      if (const Etiquette* e = etiquette()) glob->setChecked(e->type == TypeEtiquette::Globale);
    });
    fl->addRow(glob);
    colonne()->addStretch(1);
  }

 private:
  const Etiquette* etiquette() const {
    if (const Feuille* f = feuille())
      for (const auto& e : f->etiquettes)
        if (e.uuid == m_objet) return &e;
    return nullptr;
  }
};

// ---------- Texte de la feuille : contenu, taille et orientation (EL-21) ----------
class PageTexte : public PageSchema {
 public:
  explicit PageTexte(AtelierElectronique& a) : PageSchema(a) {
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Texte"), fl);
    auto* contenu = new ChampTexteLie([this]() -> QString { const TexteSchema* t = texteChoisi(); return t ? q(t->texte) : QString(); },
                                      [this](const QString& t, std::uint64_t saisie) {
                                        const std::string v = t.toStdString(), id = m_objet;
                                        modifierFeuille([&](Feuille& f) { for (auto& y : f.textes) if (y.uuid == id) y.texte = v; }, saisie);
                                      });
    contenu->setObjectName(QStringLiteral("champ:texte"));
    contenu->setValidation([](const QString& t) { return t.trimmed().isEmpty() ? QStringLiteral("Un texte vide ne se verrait plus : supprime-le plutôt") : QString(); });
    fl->addRow(QStringLiteral("Contenu"), contenu);
    lier(contenu, [contenu] { contenu->relire(); });
    auto* taille = new QDoubleSpinBox;
    taille->setRange(0.5, 20);
    taille->setDecimals(2);
    taille->setSuffix(QStringLiteral(" mm"));
    connect(taille, &QDoubleSpinBox::editingFinished, this, [this, taille] {
      const Nm v = static_cast<Nm>(std::llround(taille->value() * 1e6));
      const TexteSchema* t = texteChoisi();
      if (!t || t->taille == v) return;
      const std::string id = m_objet;
      modifierFeuille([&](Feuille& f) { for (auto& y : f.textes) if (y.uuid == id) y.taille = v; });
    });
    lier(taille, [this, taille] {
      if (const TexteSchema* t = texteChoisi()) taille->setValue(mm(t->taille));
    });
    fl->addRow(QStringLiteral("Hauteur"), taille);
    auto* rotation = new QComboBox;
    rotation->addItems({QStringLiteral("0°"), QStringLiteral("90°"), QStringLiteral("180°"), QStringLiteral("270°")});
    connect(rotation, &QComboBox::activated, this, [this](int r) {
      const std::string id = m_objet;
      modifierFeuille([&](Feuille& f) { for (auto& y : f.textes) if (y.uuid == id) y.rotation = r; });
    });
    lier(rotation, [this, rotation] {
      if (const TexteSchema* t = texteChoisi()) rotation->setCurrentIndex(((t->rotation % 4) + 4) % 4);
    });
    fl->addRow(QStringLiteral("Orientation"), rotation);
    colonne()->addStretch(1);
  }

 private:
  const TexteSchema* texteChoisi() const {
    if (const Feuille* f = feuille())
      for (const auto& t : f->textes)
        if (t.uuid == m_objet) return &t;
    return nullptr;
  }
};

// ---------- Fil ----------
class PageFil : public PageSchema {
 public:
  explicit PageFil(AtelierElectronique& a) : PageSchema(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] { titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>Fil</span><br>Réseau <b>%1</b>").arg(reseau().toHtmlEscaped())); });
    auto* b = new QPushButton(icone(QStringLiteral("afficher")), QStringLiteral("Surligner ce réseau"));
    b->setObjectName(QStringLiteral("surlignerReseau"));
    connect(b, &QPushButton::clicked, this, [this] { m_atelier.surligner(reseau()); });
    colonne()->addWidget(b);
    colonne()->addStretch(1);
  }

 private:
  QString reseau() const {
    const Connectivite& k = m_atelier.calculs().k;
    if (const auto it = k.parFil.find(m_objet); it != k.parFil.end()) return q(k.reseaux[static_cast<std::size_t>(it->second)].nom);
    return QStringLiteral("—");
  }
};

// ---------- Feuille (rien de choisi) : nom, format, feuilles, cartouche, vérification électrique ----------
class PageFeuille : public PageSchema {
 public:
  explicit PageFeuille(AtelierElectronique& a) : PageSchema(a) {
    auto* titre = entete();
    colonne()->addWidget(titre);
    suivre([this, titre] {
      const ConceptionElec* c = conception();
      if (c)
        titre->setText(QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span><br><span style='color:%4'>Feuille %2 / %3</span>")
                           .arg(q(c->nom).toHtmlEscaped())
                           .arg(m_feuille + 1)
                           .arg(c->feuilles.size())
                           .arg(teintes::secondaire()));
    });
    QFormLayout* fl = nullptr;
    groupe(QStringLiteral("Feuille"), fl);
    auto* nom = new ChampTexteLie([this]() -> QString { const Feuille* f = feuille(); return f ? q(f->nom) : QString(); },
                                  [this](const QString& t, std::uint64_t saisie) {
                                    const std::string v = t.trimmed().toStdString();
                                    modifierFeuille([&](Feuille& f) { f.nom = v; }, saisie);
                                  });
    nom->setObjectName(QStringLiteral("champ:nomFeuille"));
    nom->setValidation([](const QString& t) { return t.trimmed().isEmpty() ? QStringLiteral("Une feuille a besoin d’un nom") : QString(); });
    fl->addRow(QStringLiteral("Nom"), nom);
    lier(nom, [nom] { nom->relire(); });
    auto* format = new QComboBox;
    format->addItems({QStringLiteral("A4"), QStringLiteral("A3"), QStringLiteral("A2")});
    connect(format, &QComboBox::activated, this, [this, format](int) {
      const std::string t = format->currentText().toStdString();
      modifierFeuille([&](Feuille& f) { f.format = t; });
      m_atelier.vueSchema()->recadrer();
    });
    lier(format, [this, format] {
      if (const Feuille* f = feuille()) format->setCurrentText(q(f->format));
    });
    fl->addRow(QStringLiteral("Format"), format);

    // Feuilles de la conception : changer de feuille, réordonner, supprimer
    auto* gf = new QGroupBox(QStringLiteral("Feuilles"));
    auto* lf = new QVBoxLayout(gf);
    auto* liste = new QListWidget;
    liste->setObjectName(QStringLiteral("listeFeuilles"));
    liste->setMaximumHeight(120);
    connect(liste, &QListWidget::currentRowChanged, this, [this](int r) {
      if (r >= 0 && r != m_atelier.vueSchema()->feuille()) {
        m_atelier.vueSchema()->setConception(m_atelier.conception(), r);
        m_atelier.montrerInspecteur();
      }
    });
    lier(liste, [this, liste] {
      const ConceptionElec* c = conception();
      if (!c) return;
      QStringList noms;
      for (const auto& x : c->feuilles) noms << q(x.nom);
      QStringList actuels;
      for (int i = 0; i < liste->count(); ++i) actuels << liste->item(i)->text();
      if (actuels != noms) {
        liste->clear();
        liste->addItems(noms);
      }
      liste->setCurrentRow(m_feuille);
    }, false);
    lf->addWidget(liste);
    auto* boutons = new QWidget;
    auto* hb = new QHBoxLayout(boutons);
    hb->setContentsMargins(0, 0, 0, 0);
    auto bouton = [this, hb](const QString& t, const QString& bulle, std::function<void()> fn) {
      auto* b = new QToolButton;
      b->setText(t);
      b->setToolTip(bulle);
      connect(b, &QToolButton::clicked, this, [fn] { fn(); });
      hb->addWidget(b);
      action(b);
    };
    bouton(QStringLiteral("↑"), QStringLiteral("Monter la feuille"), [this] { m_atelier.deplacerFeuille(m_feuille, -1); });
    bouton(QStringLiteral("↓"), QStringLiteral("Descendre la feuille"), [this] { m_atelier.deplacerFeuille(m_feuille, +1); });
    bouton(QStringLiteral("Supprimer…"), QStringLiteral("Supprimer cette feuille et ce qu’elle contient"), [this] { m_atelier.supprimerFeuille(m_feuille); });
    hb->addStretch(1);
    lf->addWidget(boutons);
    colonne()->addWidget(gf);

    // Cartouche
    QFormLayout* fc = nullptr;
    groupe(QStringLiteral("Cartouche"), fc);
    using Champ = std::string Cartouche::*;
    for (const auto& [libelle, champ] : std::initializer_list<std::pair<QString, Champ>>{{QStringLiteral("Titre"), &Cartouche::titre},
                                                                                          {QStringLiteral("Société"), &Cartouche::societe},
                                                                                          {QStringLiteral("Auteur"), &Cartouche::auteur},
                                                                                          {QStringLiteral("Révision"), &Cartouche::revision},
                                                                                          {QStringLiteral("Date"), &Cartouche::date},
                                                                                          {QStringLiteral("Commentaire"), &Cartouche::commentaire}}) {
      const Champ m = champ;
      auto* e = new ChampTexteLie([this, m]() -> QString { const ConceptionElec* c = conception(); return c ? q(c->cartouche.*m) : QString(); },
                                  [this, m](const QString& t, std::uint64_t saisie) {
                                    const std::string v = t.trimmed().toStdString();
                                    m_atelier.modifierConception([&](ConceptionElec& c) { c.cartouche.*m = v; }, {}, QStringLiteral("saisie:%1").arg(saisie));
                                  });
      fc->addRow(libelle, e);
      lier(e, [e] { e->relire(); });
    }

    // Vérification électrique : la liste se refait quand l'ERC change, le reste de la page ne bouge pas
    auto* ge = new QGroupBox(QStringLiteral("Vérification électrique"));
    auto* le = new QVBoxLayout(ge);
    auto* bilan = inspecteur::texte();
    le->addWidget(bilan);
    auto* liste2 = new SectionRefaite([this](QVBoxLayout* col) { inspecteur::listeProblemes(col, m_atelier.erc(), true, m_atelier); });
    le->addWidget(liste2);
    colonne()->addWidget(ge);
    suivre([this, bilan, liste2] {
      const auto& erc = m_atelier.erc();
      const int erreurs = static_cast<int>(std::count_if(erc.begin(), erc.end(), [](const Violation& v) { return v.gravite == Gravite::Erreur; }));
      bilan->setText(erc.empty() ? QStringLiteral("<span style='color:%1'><b>✓ Rien à signaler</b></span>").arg(teintes::succes())
                                 : QStringLiteral("<b>%1 erreur(s), %2 avertissement(s)</b> — clique pour y aller").arg(erreurs).arg(erc.size() - static_cast<std::size_t>(erreurs)));
      liste2->refaire(inspecteur::signature(erc));
    });
    colonne()->addStretch(1);
  }
};
}  // namespace

PageInspecteur* pageSchema(const QString& cle, AtelierElectronique& a) {
  if (cle == QLatin1String("schema.composant")) return new PageComposant(a);
  if (cle == QLatin1String("schema.alimentation")) return new PageAlimentation(a);
  if (cle == QLatin1String("schema.etiquette")) return new PageEtiquette(a);
  if (cle == QLatin1String("schema.texte")) return new PageTexte(a);
  if (cle == QLatin1String("schema.fil")) return new PageFil(a);
  return new PageFeuille(a);
}
