// Prométhée : sélection partagée d'une conception électronique. Licence GPL-3.0-only.
#include "SelectionElectronique.hpp"

#include <algorithm>

SelectionElectronique::Mode SelectionElectronique::modePour(Qt::KeyboardModifiers m) {
  const bool maj = m & Qt::ShiftModifier, ctrl = m & Qt::ControlModifier;
  if (ctrl && maj) return Mode::Retirer;
  if (ctrl) return Mode::Basculer;
  if (maj) return Mode::Ajouter;
  return Mode::Remplacer;
}

bool SelectionElectronique::contient(Genre g, const std::string& id) const {
  return std::any_of(m_objets.begin(), m_objets.end(), [&](const Ref& r) { return r.genre == g && r.id == id; });
}

std::vector<std::string> SelectionElectronique::ids(Genre g) const {
  std::vector<std::string> r;
  for (const auto& o : m_objets)
    if (o.genre == g) r.push_back(o.id);
  return r;
}

bool SelectionElectronique::appliquer(const std::vector<Ref>& refs, Mode mode) {
  std::vector<Ref> s = mode == Mode::Remplacer ? std::vector<Ref>{} : m_objets;
  for (const Ref& r : refs) {
    const auto it = std::find(s.begin(), s.end(), r);
    switch (mode) {
      case Mode::Remplacer:
      case Mode::Ajouter:
        if (it == s.end()) s.push_back(r);
        break;
      case Mode::Basculer:
        if (it == s.end()) s.push_back(r);
        else s.erase(it);
        break;
      case Mode::Retirer:
        if (it != s.end()) s.erase(it);
        break;
    }
  }
  if (s == m_objets) {
    // Même ensemble : l'objet cliqué devient l'objet tenu (il est détaillé, c'est lui qu'on glisse)
    if (mode != Mode::Retirer && refs.size() == 1 && !s.empty() && !(s.back() == refs.front())) tenir(refs.front());
    return false;
  }
  m_objets = std::move(s);
  if (!m_palier) m_paliers.clear();
  majComposants();
  emit changee();
  return true;
}

void SelectionElectronique::setForme(Forme f) {
  if (f == m_forme) return;
  m_forme = f;
  emit formeChange();
}

void SelectionElectronique::setEstompage(Estompage e, double niveau) {
  const double n = niveau < 0 ? m_niveau : std::clamp(niveau, 0.1, 0.9);
  if (e == m_estompage && n == m_niveau) return;
  m_estompage = e;
  m_niveau = n;
  emit estompageChange();
}

void SelectionElectronique::memoriser(int i) {
  if (i < 0 || i >= MEMOIRES) return;
  m_memoires[static_cast<std::size_t>(i)] = m_objets;
  emit memoiresChange();
}

bool SelectionElectronique::rappeler(int i, Mode mode) {
  if (i < 0 || i >= MEMOIRES || m_memoires[static_cast<std::size_t>(i)].empty()) return false;
  appliquer(m_memoires[static_cast<std::size_t>(i)], mode);
  return true;
}

void SelectionElectronique::choisirPalier(const std::vector<Ref>& refs) {
  std::vector<Ref> avant = m_objets;
  m_palier = true;
  const bool change = appliquer(refs, Mode::Ajouter);
  m_palier = false;
  if (change) m_paliers.push_back(std::move(avant));
}

bool SelectionElectronique::reduire() {
  if (m_paliers.empty()) return false;
  std::vector<Ref> avant = std::move(m_paliers.back());
  m_paliers.pop_back();
  m_palier = true;
  appliquer(avant, Mode::Remplacer);
  m_palier = false;
  return true;
}

void SelectionElectronique::tenir(const Ref& r) {
  const auto it = std::find(m_objets.begin(), m_objets.end(), r);
  if (it == m_objets.end() || it + 1 == m_objets.end()) return;
  Ref x = *it;
  m_objets.erase(it);
  m_objets.push_back(std::move(x));
  emit changee();
}

bool SelectionElectronique::filtrer(const std::function<bool(const Ref&)>& existe) {
  const std::size_t avant = m_objets.size();
  m_objets.erase(std::remove_if(m_objets.begin(), m_objets.end(), [&](const Ref& r) { return !existe(r); }), m_objets.end());
  if (m_objets.size() == avant) return false;
  majComposants();
  emit changee();
  return true;
}

void SelectionElectronique::surligner(const std::string& reseau) {
  if (reseau == m_reseau) return;
  m_reseau = reseau;
  emit surlignageChange();
}

void SelectionElectronique::neutre() {
  vider();
  surligner({});
  setModeBandeau(Mode::Remplacer);
}

void SelectionElectronique::setModeBandeau(Mode m) {
  if (m == m_modeBandeau) return;
  m_modeBandeau = m;
  emit modeBandeauChange();
}

void SelectionElectronique::setAdmis(Genre g, bool oui) {
  if (oui == admis(g)) return;
  if (oui) m_exclus.erase(g);
  else m_exclus.insert(g);
  emit filtreChange();
}

void SelectionElectronique::majComposants() {
  m_composants.clear();
  for (const auto& o : m_objets)
    if (!o.composant.empty()) m_composants.insert(o.composant);
    else if (o.genre == Genre::Empreinte) m_composants.insert(o.id);
}

bool SelectionElectronique::refSchema(const prom::ecad::Feuille& f, const std::string& id, Ref& r) {
  using namespace prom::ecad;
  r.id = id;
  r.composant.clear();
  for (const auto& s : f.symboles)
    if (s.uuid == id) {
      r.genre = Genre::Symbole;
      r.composant = s.composant;
      return true;
    }
  for (const auto& w : f.fils)
    if (w.uuid == id) return r.genre = Genre::Fil, true;
  for (const auto& e : f.etiquettes)
    if (e.uuid == id) return r.genre = Genre::Etiquette, true;
  for (const auto& t : f.textes)
    if (t.uuid == id) return r.genre = Genre::TexteSchema, true;
  for (const auto& j : f.jonctions)
    if (idJonction(j) == id) return r.genre = Genre::Jonction, true;
  for (const auto& n : f.nonConnexions)
    if (idNonConnexion(n) == id) return r.genre = Genre::NonConnexion, true;
  return false;
}

bool SelectionElectronique::existe(const prom::ecad::ConceptionElec& c, const Ref& r) {
  auto dans = [&](const auto& liste) { return std::any_of(liste.begin(), liste.end(), [&](const auto& x) { return x.uuid == r.id; }); };
  switch (r.genre) {
    case Genre::Symbole:
      for (const auto& f : c.feuilles)
        if (dans(f.symboles)) return true;
      return false;
    case Genre::Fil:
      for (const auto& f : c.feuilles)
        if (dans(f.fils)) return true;
      return false;
    case Genre::Etiquette:
      for (const auto& f : c.feuilles)
        if (dans(f.etiquettes)) return true;
      return false;
    case Genre::TexteSchema:
      for (const auto& f : c.feuilles)
        if (dans(f.textes)) return true;
      return false;
    case Genre::Jonction:
      for (const auto& f : c.feuilles)
        for (const auto& j : f.jonctions)
          if (prom::ecad::idJonction(j) == r.id) return true;
      return false;
    case Genre::NonConnexion:
      for (const auto& f : c.feuilles)
        for (const auto& n : f.nonConnexions)
          if (prom::ecad::idNonConnexion(n) == r.id) return true;
      return false;
    case Genre::Empreinte:
      return std::any_of(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const auto& e) { return e.composant == r.id; });
    case Genre::Piste: return dans(c.circuit.pistes);
    case Genre::Via: return dans(c.circuit.vias);
    case Genre::Zone: return dans(c.circuit.zones);
    case Genre::TexteCarte: return dans(c.circuit.textes);
  }
  return false;
}

std::vector<std::string> SelectionElectronique::instantane() const {
  std::vector<std::string> r;
  r.reserve(m_objets.size());
  for (const auto& o : m_objets) r.push_back(std::to_string(static_cast<int>(o.genre)) + '\x1f' + o.id + '\x1f' + o.composant);
  return r;
}

void SelectionElectronique::restaurer(const std::vector<std::string>& s, const std::function<bool(const Ref&)>& existe) {
  std::vector<Ref> refs;
  for (const auto& t : s) {
    const auto a = t.find('\x1f');
    const auto b = a == std::string::npos ? std::string::npos : t.find('\x1f', a + 1);
    if (b == std::string::npos) continue;
    Ref r;
    try {
      r.genre = static_cast<Genre>(std::stoi(t.substr(0, a)));
    } catch (const std::exception&) {
      continue;
    }
    r.id = t.substr(a + 1, b - a - 1);
    r.composant = t.substr(b + 1);
    if (existe(r)) refs.push_back(std::move(r));
  }
  choisir(refs);
}
