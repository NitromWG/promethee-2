// Prométhée : description du ruban de l'atelier électronique. Voir DescriptionRuban.hpp. Licence GPL-3.0-only.
#include "DescriptionRuban.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include "RegistreActions.hpp"

namespace {
const QString CLE = QStringLiteral("ruban/electronique");

DescriptionRuban::Forme forme(const QString& s) {
  if (s == QLatin1String("grille")) return DescriptionRuban::Forme::Grille;
  if (s == QLatin1String("controle")) return DescriptionRuban::Forme::Controle;
  if (s == QLatin1String("separateur")) return DescriptionRuban::Forme::Separateur;
  return DescriptionRuban::Forme::Grand;
}
QString nomForme(DescriptionRuban::Forme f) {
  switch (f) {
    case DescriptionRuban::Forme::Grille: return QStringLiteral("grille");
    case DescriptionRuban::Forme::Controle: return QStringLiteral("controle");
    case DescriptionRuban::Forme::Separateur: return QStringLiteral("separateur");
    case DescriptionRuban::Forme::Grand: break;
  }
  return QStringLiteral("grand");
}
}  // namespace

DescriptionRuban DescriptionRuban::depuisJson(const QByteArray& json, QString* erreur) {
  DescriptionRuban d;
  QJsonParseError e{};
  const QJsonDocument doc = QJsonDocument::fromJson(json, &e);
  if (e.error != QJsonParseError::NoError || !doc.isObject() || doc.object().value(QStringLiteral("format")).toString() != QLatin1String("promethee-ruban")) {
    if (erreur) *erreur = e.error != QJsonParseError::NoError ? e.errorString() : QStringLiteral("ce n’est pas une description de ruban");
    return d;
  }
  for (const QJsonValue& o : doc.object().value(QStringLiteral("onglets")).toArray()) {
    Onglet onglet;
    onglet.id = o.toObject().value(QStringLiteral("id")).toString().toStdString();
    onglet.titre = o.toObject().value(QStringLiteral("titre")).toString();
    for (const QJsonValue& g : o.toObject().value(QStringLiteral("groupes")).toArray()) {
      Groupe groupe;
      groupe.titre = g.toObject().value(QStringLiteral("titre")).toString();
      groupe.forme = forme(g.toObject().value(QStringLiteral("forme")).toString());
      for (const QJsonValue& a : g.toObject().value(QStringLiteral("actions")).toArray()) groupe.actions.push_back(a.toString().toStdString());
      onglet.groupes.push_back(std::move(groupe));
    }
    d.onglets.push_back(std::move(onglet));
  }
  return d;
}

QByteArray DescriptionRuban::versJson() const {
  QJsonArray onglets;
  for (const Onglet& o : this->onglets) {
    QJsonArray groupes;
    for (const Groupe& g : o.groupes) {
      QJsonObject x;
      if (!g.titre.isEmpty()) x.insert(QStringLiteral("titre"), g.titre);
      x.insert(QStringLiteral("forme"), nomForme(g.forme));
      QJsonArray actions;
      for (const std::string& a : g.actions) actions.append(QString::fromStdString(a));
      if (g.forme != Forme::Separateur) x.insert(QStringLiteral("actions"), actions);
      groupes.append(x);
    }
    onglets.append(QJsonObject{{QStringLiteral("id"), QString::fromStdString(o.id)}, {QStringLiteral("titre"), o.titre}, {QStringLiteral("groupes"), groupes}});
  }
  return QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("promethee-ruban")}, {QStringLiteral("version"), 1}, {QStringLiteral("onglets"), onglets}})
      .toJson(QJsonDocument::Indented);
}

QStringList DescriptionRuban::inconnues(const RegistreActions& r) const {
  QStringList l;
  for (const Onglet& o : onglets)
    for (const Groupe& g : o.groupes)
      for (const std::string& a : g.actions)
        if (!r.action(a)) l << QString::fromStdString(a);
  return l;
}

bool DescriptionRuban::contient(const std::string& id) const {
  for (const Onglet& o : onglets)
    for (const Groupe& g : o.groupes)
      for (const std::string& a : g.actions)
        if (a == id) return true;
  return false;
}

// ---------- Ruban en cours ----------
RubanEnCours& RubanEnCours::instance() {
  static RubanEnCours r;
  return r;
}

RubanEnCours::RubanEnCours() { relire(); }

DescriptionRuban RubanEnCours::original() {
  QFile f(QStringLiteral(":/ruban/electronique.json"));
  return f.open(QIODevice::ReadOnly) ? DescriptionRuban::depuisJson(f.readAll()) : DescriptionRuban{};
}

void RubanEnCours::relire() {
  const QByteArray perso = QSettings().value(CLE).toByteArray();
  DescriptionRuban d = perso.isEmpty() ? DescriptionRuban{} : DescriptionRuban::depuisJson(perso);
  // Une version de l'utilisateur illisible, ou qui n'a plus les quatre onglets, laisse la place à l'original
  if (d.onglets.size() != 4) d = original();
  if (d == m_description) return;
  m_description = std::move(d);
  emit change();  // après une relecture des réglages (2.32) : le ruban se refait
}

void RubanEnCours::remplacer(const DescriptionRuban& d) {
  if (d == m_description) return;
  m_description = d;
  QSettings().setValue(CLE, d.versJson());
  emit change();
}

void RubanEnCours::retablir() {
  QSettings().remove(CLE);
  const DescriptionRuban o = original();
  if (o == m_description) return;
  m_description = o;
  emit change();
}

bool RubanEnCours::personnalise() const { return QSettings().contains(CLE); }
