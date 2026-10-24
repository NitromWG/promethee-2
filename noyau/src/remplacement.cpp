// Prométhée : remplacer un composant, modifier une propriété objet par objet (lot L3, tâches 3.15 et 3.16). Voir
// remplacement.hpp.
// Licence GPL-3.0-only.
#include "promethee/remplacement.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <tuple>

#include "promethee/approvisionnement.hpp"
#include "promethee/formules.hpp"
#include "promethee/synchronisation.hpp"

namespace prom::ecad {

namespace {
// Ce qui touche un point de la feuille : bouts ou milieux de fils, étiquettes, marques de non-connexion, autres broches
struct Contacts {
  int fils = 0;
  bool autre = false;  // étiquette, marque, jonction ou broche d'un autre symbole
  bool relie() const { return fils > 0 || autre; }
};
Contacts contactsEn(const ConceptionElec& c, const Feuille& f, Pt p, const std::string& symboleIgnore) {
  Contacts r;
  for (const auto& w : f.fils)
    if (w.a != w.b && surSegment(p, w.a, w.b)) ++r.fils;
  for (const auto& e : f.etiquettes) r.autre |= e.p == p;
  for (const auto& n : f.nonConnexions) r.autre |= n.p == p;
  for (const auto& s : f.symboles) {
    if (s.uuid == symboleIgnore) continue;
    if (const DefSymbole* d = c.symbole(s.symbole))
      for (const auto& b : brochesPlacees(s, *d)) r.autre |= b.p == p;
  }
  return r;
}

// Remplaçante d'une broche dans le nouveau symbole : par son nom (s'il est unique et parlant), sinon par son numéro
const Broche* remplacante(const DefSymbole& neuf, const Broche& ancienne, bool parNom) {
  auto parlant = [](const std::string& n) { return !n.empty() && n != "~" && n.find_first_not_of("0123456789") != std::string::npos; };
  if (parNom && parlant(ancienne.nom)) {
    const Broche* trouvee = nullptr;
    int n = 0;
    for (const auto& b : neuf.broches)
      if (b.nom == ancienne.nom) {
        trouvee = &b;
        ++n;
      }
    if (n == 1) return trouvee;
    if (n > 1)
      for (const auto& b : neuf.broches)
        if (b.nom == ancienne.nom && b.numero == ancienne.numero) return &b;
  }
  for (const auto& b : neuf.broches)
    if (b.numero == ancienne.numero) return &b;
  return nullptr;
}

std::string nomBroche(const Broche& b) { return b.numero + (b.nom.empty() || b.nom == "~" ? std::string() : " (" + b.nom + ")"); }
}  // namespace

ApercuRemplacement apercuRemplacement(const ConceptionElec& c, const std::string& composant, const std::string& symbole, const RegleRemplacement& r) {
  ApercuRemplacement a;
  const ComposantElec* x = c.composant(composant);
  const DefSymbole* ancien = x ? c.symbole(x->symbole) : nullptr;
  const DefSymbole* neuf = c.symbole(symbole);
  if (!x || !ancien || !neuf) {
    a.perdues.push_back("article inconnu");
    return a;
  }
  a.empreinteAvant = x->empreinte;
  a.empreinteApres = r.garderEmpreinte ? x->empreinte : neuf->empreinte;
  std::set<std::string> vues;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) {
      if (s.composant != composant) continue;
      SymbolePlace s2 = s;
      s2.symbole = symbole;
      if (s2.unite > std::max(1, neuf->unites)) s2.unite = 1;
      const auto apres = brochesPlacees(s2, *neuf);
      for (const auto& b : brochesPlacees(s, *ancien)) {
        const Contacts k = contactsEn(c, f, b.p, s.uuid);
        if (!k.relie()) continue;
        const Broche* n = remplacante(*neuf, *b.broche, r.brochesParNom);
        if (!n) {
          if (vues.insert(b.broche->numero).second) a.perdues.push_back(nomBroche(*b.broche));
          continue;
        }
        Pt q = b.p;
        for (const auto& bp : apres)
          if (bp.broche->numero == n->numero) q = bp.p;
        a.reliees.push_back({b.numero, n->numero, b.broche->nom, q != b.p});
        a.fils += k.fils;
      }
    }
  return a;
}

int remplacerComposants(ConceptionElec& c, const std::vector<std::string>& composants, const std::string& symbole, const RegleRemplacement& r, std::string* erreur) {
  exigerModifiable(c);
  const DefSymbole* pn = c.symbole(symbole);
  if (!pn) {
    if (erreur) *erreur = "article inconnu : " + symbole;
    return 0;
  }
  // D'abord l'aperçu de tous : un seul refus et rien ne change
  for (const auto& id : composants) {
    const ApercuRemplacement a = apercuRemplacement(c, id, symbole, r);
    if (!a.possible()) {
      const ComposantElec* x = c.composant(id);
      std::string liste;
      for (const auto& p : a.perdues) liste += (liste.empty() ? "" : ", ") + p;
      if (erreur) *erreur = (x ? x->reference : std::string("?")) + " : broche" + (a.perdues.size() > 1 ? "s reliées" : " reliée") + " sans remplaçante dans « " + pn->nom + " » : " + liste;
      return 0;
    }
  }
  const DefSymbole neuf = *pn;
  if (!c.symboles.count(symbole)) c.symboles[symbole] = neuf;  // la version utilisée est verrouillée dans le projet
  int n = 0;
  for (const auto& id : composants) {
    ComposantElec* x = c.composant(id);
    if (!x) continue;
    const DefSymbole* pa = c.symbole(x->symbole);
    if (!pa) continue;
    const DefSymbole ancien = *pa;
    for (std::size_t fi = 0; fi < c.feuilles.size(); ++fi) {
      Feuille& f = c.feuilles[fi];
      for (auto& s : f.symboles) {
        if (s.composant != id) continue;
        // Broches reliées : point d'avant, et broche remplaçante
        struct Lien {
          Pt avant;
          std::string numero;  // numéro de la remplaçante dans la définition
        };
        std::vector<Lien> liens;
        for (const auto& b : brochesPlacees(s, ancien))
          if (contactsEn(c, f, b.p, s.uuid).relie())
            if (const Broche* nb = remplacante(neuf, *b.broche, r.brochesParNom)) liens.push_back({b.p, nb->numero});
        // Fils accrochés par un bout aux broches qui vont bouger : retirés le temps de poser le nouveau symbole, puis
        // retracés de la nouvelle broche à leur autre bout. Laissés en place, leur bout pourrait tomber sur une autre
        // broche du nouveau symbole et la relier au mauvais réseau.
        struct Retrace {
          Pt depuis;           // autre bout du fil retiré
          std::string numero;  // remplaçante
          Nm rayon = 0;
        };
        std::vector<Retrace> aRetracer;
        std::vector<Lien> traversants;  // broche au milieu d'un fil (jonction en T) : un fil de raccord
        SymbolePlace futur = s;
        futur.symbole = symbole;
        if (futur.unite > std::max(1, neuf.unites)) futur.unite = 1;
        const auto prevues = brochesPlacees(futur, neuf);
        auto bouge = [&](const Lien& l) {
          for (const auto& bp : prevues)
            if (bp.broche->numero == l.numero) return bp.p != l.avant;
          return false;
        };
        for (const Lien& l : liens) {
          if (!bouge(l)) continue;
          bool milieu = false;
          for (const auto& w : f.fils)
            if (w.a != w.b && surSegment(l.avant, w.a, w.b) && w.a != l.avant && w.b != l.avant) milieu = true;
          for (auto it = f.fils.begin(); it != f.fils.end();) {
            if (it->a == l.avant || it->b == l.avant) {
              aRetracer.push_back({it->a == l.avant ? it->b : it->a, l.numero, it->rayon});
              it = f.fils.erase(it);
            } else
              ++it;
          }
          if (milieu) traversants.push_back(l);
        }
        s.symbole = symbole;
        if (s.unite > std::max(1, neuf.unites)) s.unite = 1;
        // Repères de bornes propres à la pose : gardés pour les broches de même numéro
        std::erase_if(s.numeros, [&](const auto& kv) { return std::none_of(neuf.broches.begin(), neuf.broches.end(), [&](const Broche& b) { return b.numero == kv.first; }); });
        s.champReference.reset();
        s.champValeur.reset();
        const SymbolePlace pose = s;
        const auto apres = brochesPlacees(pose, neuf);
        const Boite corps = boiteSymbole(pose, neuf);
        auto pointNeuf = [&](const std::string& numero, Pt defaut) {
          for (const auto& bp : apres)
            if (bp.broche->numero == numero) return bp.p;
          return defaut;
        };
        // Un tracé n'est bon que s'il ne touche rien d'autre que ses deux bouts : ni une broche, ni un bout de fil, ni
        // une étiquette, ni un fil par un de ses coudes (cela relierait un autre réseau)
        auto propre = [&](const std::vector<Pt>& ch) {
          if (ch.size() < 2) return false;
          std::vector<Pt> points;
          for (const auto& sy : f.symboles)
            if (const DefSymbole* d = c.symbole(sy.symbole))
              for (const auto& b : brochesPlacees(sy, *d)) points.push_back(b.p);
          for (const auto& w : f.fils) {
            points.push_back(w.a);
            points.push_back(w.b);
          }
          for (const auto& e : f.etiquettes) points.push_back(e.p);
          for (const auto& m : f.nonConnexions) points.push_back(m.p);
          for (std::size_t i = 1; i < ch.size(); ++i) {
            if (ch[i - 1].x != ch[i].x && ch[i - 1].y != ch[i].y) return false;  // à l'équerre seulement
            for (const Pt& q : points)
              if (q != ch.front() && q != ch.back() && surSegment(q, ch[i - 1], ch[i])) return false;
          }
          for (std::size_t i = 1; i + 1 < ch.size(); ++i)
            for (const auto& w : f.fils)
              if (w.a != w.b && surSegment(ch[i], w.a, w.b)) return false;
          return true;
        };
        auto poserChemin = [&](Pt de, Pt vers, int direction) {
          if (de == vers) return;
          std::vector<Pt> chemin = tracerFil(c, fi, de, vers);
          // Le tracé suit la grille : un bout hors grille (symbole posé hors grille) y est raccordé
          if (chemin.size() >= 2) {
            if (chemin.front() != de) chemin.insert(chemin.begin(), de);
            if (chemin.back() != vers) chemin.push_back(vers);
          }
          if (!propre(chemin)) {
            // Coudes simples, après une amorce de 0 à 8 pas vers l'extérieur de la broche
            const Pt dehors = direction == 0 ? Pt{-1, 0} : direction == 1 ? Pt{0, -1} : direction == 2 ? Pt{1, 0} : Pt{0, 1};
            std::vector<Pt> trouve;
            for (int pas = 0; pas <= 8 && trouve.empty(); ++pas) {
              const Pt a{de.x + dehors.x * GRILLE_SCHEMA * pas, de.y + dehors.y * GRILLE_SCHEMA * pas};
              for (const Pt coin : {Pt{vers.x, a.y}, Pt{a.x, vers.y}}) {
                std::vector<Pt> ch{de};
                if (a != de) ch.push_back(a);
                if (coin != ch.back() && coin != vers) ch.push_back(coin);
                ch.push_back(vers);
                if (!corps.contient(coin) && propre(ch)) {
                  trouve = ch;
                  break;
                }
              }
            }
            if (!trouve.empty()) chemin = trouve;
            else if (chemin.size() < 2) {
              const Pt coinA{vers.x, de.y}, coinB{de.x, vers.y};
              chemin = {de, corps.contient(coinA) ? coinB : coinA, vers};
            }
          }
          for (std::size_t i = 1; i < chemin.size(); ++i)
            if (chemin[i - 1] != chemin[i]) f.fils.push_back({nouvelUuid(), chemin[i - 1], chemin[i]});
        };
        auto directionNeuve = [&](const std::string& numero) {
          for (const auto& bp : apres)
            if (bp.broche->numero == numero) return bp.direction;
          return 0;
        };
        // Étiquettes et marques posées sur une broche qui bouge, sans fil : elles la suivent
        for (const Lien& l : liens) {
          const Pt q = pointNeuf(l.numero, l.avant);
          if (q == l.avant) continue;
          const bool fil = std::any_of(f.fils.begin(), f.fils.end(), [&](const Fil& w) { return surSegment(l.avant, w.a, w.b); });
          if (fil) continue;
          for (auto& m : f.nonConnexions)
            if (m.p == l.avant) m.p = q;
          for (auto& e : f.etiquettes)
            if (e.p == l.avant) e.p = q;
        }
        // Les fils retirés, retracés depuis la nouvelle broche
        for (const Retrace& r2 : aRetracer) {
          const Pt q = pointNeuf(r2.numero, r2.depuis);
          const std::size_t avant = f.fils.size();
          poserChemin(q, r2.depuis, directionNeuve(r2.numero));
          if (r2.rayon > 0)
            for (std::size_t i = avant; i < f.fils.size(); ++i) f.fils[i].rayon = r2.rayon;
        }
        // Broche au milieu d'un fil : un raccord de la nouvelle broche au point d'avant
        for (const Lien& l : traversants) poserChemin(pointNeuf(l.numero, l.avant), l.avant, directionNeuve(l.numero));
      }
    }
    // Champs du composant : ce qui l'emporte, champ par champ
    const std::string prefixeAvant = ancien.prefixe;
    x->symbole = symbole;
    if (!r.garderValeur) {
      x->valeur = neuf.nom;
      x->formule.clear();
    }
    if (!r.garderEmpreinte) x->empreinte = neuf.empreinte;
    if (!r.garderFabricant) {
      x->fabricant = neuf.fabricant;
      x->mpn = neuf.mpn;
      x->fiche = neuf.fiche;
    }
    x->description = neuf.description;
    x->genre = neuf.genre;
    if (!r.garderRepere || neuf.prefixe != prefixeAvant) x->reference = neuf.prefixe + "?";
    if (x->brochageVerifie != x->empreinte) x->brochageVerifie.clear();
    ++n;
  }
  return n;
}

std::optional<std::string> transformerTexte(const std::string& texte, const RegleTexte& r, FamilleValeur famille, std::string* erreur) {
  switch (r.genre) {
    case RegleTexte::Genre::Prefixe: return r.a + texte;
    case RegleTexte::Genre::Suffixe: return texte + r.a;
    case RegleTexte::Genre::Remplacer: {
      if (r.de.empty()) return texte;
      std::string s = texte;
      for (std::size_t i = s.find(r.de); i != std::string::npos; i = s.find(r.de, i + r.a.size())) s.replace(i, r.de.size(), r.a);
      return s;
    }
    case RegleTexte::Genre::Formule: {
      // « x » : la valeur actuelle, lue dans l'unité de sa famille
      const ValeurComprise v = lireValeurComposant(texte, famille);
      std::optional<Quantite> x;
      if (v.comprise()) x = Quantite::nombre(v.nombre, dimensionDeFamille(famille));
      else if (const ResultatFormule t = evaluerFormule(texte); t.ok) x = t.q;
      const ResultatFormule res = evaluerFormule(r.a, [&](const std::string& nom) -> std::optional<Quantite> {
        if (nom == "x") return x;
        return std::nullopt;
      });
      if (!res.ok) {
        if (erreur) *erreur = x ? res.erreur : "« " + texte + " » n'est pas un nombre";
        return std::nullopt;
      }
      if (famille != FamilleValeur::Aucune && res.q.scalaire() && res.q.reelle() && (res.q.dim.sans() || res.q.dim == dimensionDeFamille(famille)))
        return ecrireSI(res.q.reel(), uniteDeFamille(famille), 3);
      return ecrireQuantite(res.q);
    }
  }
  return texte;
}

bool appliquerPinceau(ConceptionElec& c, const ComposantElec& m, const std::string& cible, const ProprietesPinceau& p) {
  ComposantElec* x = c.composant(cible);
  if (!x || x->uuid == m.uuid) return false;
  const ComposantElec avant = *x;
  if (p.valeur) {
    x->valeur = m.valeur;
    x->formule = m.formule;
  }
  if (p.empreinte) x->empreinte = m.empreinte;
  if (p.fabricant) {
    x->fabricant = m.fabricant;
    x->mpn = m.mpn;
    x->fournisseur = m.fournisseur;
    x->fiche = m.fiche;
  }
  if (p.parametres) x->parametres = m.parametres;
  if (p.champs) x->champs = m.champs;
  if (p.exclusions) {
    x->nePasMonter = m.nePasMonter;
    x->horsNomenclature = m.horsNomenclature;
    x->horsPlacement = m.horsPlacement;
    x->horsSimulation = m.horsSimulation;
  }
  if (p.hauteur) {
    x->hauteur = m.hauteur;
    x->provenanceHauteur = m.provenanceHauteur;
    x->sourceHauteur = m.sourceHauteur;
  }
  return !(*x == avant);
}

// ---------- Mise à jour et échange en masse (4.20) ----------
namespace {
std::string minusculesAscii(const std::string& s) {
  std::string r;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    // Lettres accentuées courantes ramenées à leur base (é, è, ê → e ; à → a ; ç → c ; ô → o ; î → i ; û, ù → u)
    if (ch == 0xC3 && i + 1 < s.size()) {
      const unsigned char d = static_cast<unsigned char>(s[i + 1]);
      const char base = (d >= 0xA8 && d <= 0xAB) || (d >= 0x88 && d <= 0x8B) ? 'e' : (d >= 0xA0 && d <= 0xA5) || (d >= 0x80 && d <= 0x85) ? 'a'
                      : d == 0xA7 || d == 0x87 ? 'c' : (d >= 0xB2 && d <= 0xB6) || (d >= 0x92 && d <= 0x96) ? 'o' : (d >= 0xAC && d <= 0xAF) || (d >= 0x8C && d <= 0x8F) ? 'i'
                      : (d >= 0xB9 && d <= 0xBC) || (d >= 0x99 && d <= 0x9C) ? 'u' : '\0';
      if (base) {
        r += base;
        ++i;
        continue;
      }
    }
    r += static_cast<char>(std::tolower(ch));
  }
  return r;
}
bool aJoker(const std::string& m) { return m.find_first_of("*?") != std::string::npos; }
// Valeur visée : jokers sur le texte, sinon même clé de nomenclature (« 10k », « 10 kΩ », « 10K »)
bool valeurVisee(const ConceptionElec& c, const ComposantElec& k, const std::string& motif) {
  if (motif.empty()) return true;
  if (aJoker(motif)) return correspondJoker(motif, k.valeur);
  const FamilleValeur f = familleDe(c, k);
  const std::string a = cleValeur(lireValeurComposant(motif, f), motif), b = cleValeur(lireValeurComposant(k.valeur, f), k.valeur);
  return a == b || minusculesAscii(motif) == minusculesAscii(k.valeur);
}
}  // namespace

bool correspondJoker(const std::string& motif, const std::string& texte) {
  const std::string m = minusculesAscii(motif), t = minusculesAscii(texte);
  // Parcours glouton avec retour au dernier « * »
  std::size_t i = 0, j = 0, etoile = std::string::npos, reprise = 0;
  while (j < t.size()) {
    if (i < m.size() && (m[i] == '?' || m[i] == t[j])) {
      ++i;
      ++j;
    } else if (i < m.size() && m[i] == '*') {
      etoile = i++;
      reprise = j;
    } else if (etoile != std::string::npos) {
      i = etoile + 1;
      j = ++reprise;
    } else
      return false;
  }
  while (i < m.size() && m[i] == '*') ++i;
  return i == m.size();
}

std::vector<std::string> composantsVises(const ConceptionElec& c, const PorteeMasse& p) {
  std::vector<std::string> r;
  const std::set<std::string> sel(p.selection.begin(), p.selection.end());
  for (const auto& k : c.composants) {
    if (!sel.empty() && !sel.count(k.uuid)) continue;
    if (!p.reference.empty() && !correspondJoker(p.reference, k.reference)) continue;
    if (!p.symbole.empty() && !correspondJoker(p.symbole, k.symbole)) continue;
    if (!p.empreinte.empty() && !correspondJoker(p.empreinte, k.empreinte)) continue;
    if (!valeurVisee(c, k, p.valeur)) continue;
    r.push_back(k.uuid);
  }
  return r;
}

int echangerEmpreintesEnMasse(ConceptionElec& c, const std::vector<std::string>& composants, const std::string& empreinte, const RegleEchangeEmpreinte& r, std::string* erreur) {
  exigerModifiable(c);
  const DefEmpreinte* neuve = c.empreinte(empreinte);
  if (!neuve) {
    if (erreur) *erreur = "empreinte inconnue : " + empreinte;
    return 0;
  }
  const DefEmpreinte def = *neuve;
  if (!c.empreintes.count(empreinte)) c.empreintes[empreinte] = def;  // la version utilisée est figée dans le projet
  int n = 0;
  for (const auto& id : composants) {
    ComposantElec* k = c.composant(id);
    if (!k || k->empreinte == empreinte) continue;
    auto it = std::find_if(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == id; });
    if (it == c.circuit.empreintes.end()) {
      k->empreinte = empreinte;  // pas encore sur la carte : la synchronisation la posera
      ++n;
      continue;
    }
    // Marges des pastilles retouchées sur la carte (copie locale) : reportées, numéro par numéro, sur une copie de
    // la nouvelle empreinte
    std::map<std::string, std::pair<std::optional<Nm>, std::optional<Nm>>> marges;
    if (!it->copieLocale.empty())
      if (const auto cl = c.empreintes.find(it->empreinte); cl != c.empreintes.end())
        for (const auto& p : cl->second.pastilles)
          if (p.margeVernis || p.margePate) marges[p.numero] = {p.margeVernis, p.margePate};
    const std::optional<Pt> repere = it->repere;
    if (!remplacerEmpreinteEnPlace(c, id, empreinte)) continue;
    k->empreinte = empreinte;
    it = std::find_if(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == id; });
    it->repere = r.garderTextes ? repere : std::nullopt;
    it->copieLocale.clear();
    if (r.garderMarges && !marges.empty()) {
      // Nouvelle copie locale (7.24) de la nouvelle empreinte, qui reprend les marges retouchées
      DefEmpreinte copie = def;
      copie.id = empreinte + "~local~" + k->reference;
      for (auto& p : copie.pastilles)
        if (const auto m = marges.find(p.numero); m != marges.end()) std::tie(p.margeVernis, p.margePate) = m->second;
      c.empreintes[copie.id] = copie;
      it->empreinte = copie.id;
      it->copieLocale = empreinte;
    }
    if (!r.garderAttributs) {
      k->horsNomenclature = k->horsPlacement = false;
      it->couleur.clear();
    }
    if (!r.garderModele3D) k->modele3D.clear();
    ++n;
  }
  return n;
}

// ---------- Règles d'équivalence (4.28) ----------
std::optional<RegleEquivalence> lireRegleEquivalence(const std::string& texte, std::string* erreur) {
  auto echec = [&](const std::string& m) -> std::optional<RegleEquivalence> {
    if (erreur) *erreur = m;
    return std::nullopt;
  };
  std::size_t fleche = std::string::npos, largeur = 0;
  for (const char* f : {"\xE2\x86\x92", "->", "=>"})
    if (const auto i = texte.find(f); i != std::string::npos && i < fleche) {
      fleche = i;
      largeur = std::char_traits<char>::length(f);
    }
  if (fleche == std::string::npos) return echec("règle sans flèche « → » : « " + texte + " »");
  auto jetons = [](const std::string& s) {
    std::vector<std::string> r;
    std::string m;
    for (char ch : s + " ") {
      if (ch == ' ' || ch == '\t' || ch == ',') {
        if (!m.empty()) r.push_back(m);
        m.clear();
      } else
        m += ch;
    }
    return r;
  };
  const auto gauche = jetons(texte.substr(0, fleche)), droite = jetons(texte.substr(fleche + largeur));
  if (droite.size() != 1) return echec("une seule empreinte après la flèche : « " + texte + " »");
  if (gauche.empty()) return echec("rien avant la flèche : « " + texte + " »");
  RegleEquivalence r;
  r.texte = texte;
  r.empreinte = droite.front();
  static const std::map<std::string, FamilleValeur> familles = {
      {"resistance", FamilleValeur::Resistance}, {"resistances", FamilleValeur::Resistance}, {"resistor", FamilleValeur::Resistance},
      {"condensateur", FamilleValeur::Condensateur}, {"condensateurs", FamilleValeur::Condensateur}, {"capacitor", FamilleValeur::Condensateur},
      {"inductance", FamilleValeur::Inductance}, {"inductances", FamilleValeur::Inductance}, {"self", FamilleValeur::Inductance}, {"inductor", FamilleValeur::Inductance},
      {"ferrite", FamilleValeur::Ferrite}, {"fusible", FamilleValeur::Fusible}, {"fuse", FamilleValeur::Fusible}, {"quartz", FamilleValeur::Quartz},
      {"crystal", FamilleValeur::Quartz}, {"zener", FamilleValeur::Zener}};
  std::size_t i = 0;
  if (const auto f = familles.find(minusculesAscii(gauche[0])); f != familles.end()) r.famille = f->second;
  else r.reference = gauche[0];
  for (i = 1; i < gauche.size(); ++i) {
    const std::string& m = gauche[i];
    if (r.boitier.empty() && m.size() == 4 && std::all_of(m.begin(), m.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) r.boitier = m;
    else if (r.valeur.empty()) r.valeur = m;
    else return echec("trop de critères avant la flèche : « " + texte + " »");
  }
  return r;
}

std::string boitierDeComposant(const ComposantElec& k) {
  for (const char* cle : {"Boîtier", "Boitier", "boitier", "Package", "package", "Taille", "Case"}) {
    if (const auto it = k.champs.find(cle); it != k.champs.end() && !it->second.empty()) return codeTaille(it->second).empty() ? it->second : codeTaille(it->second);
    if (const auto it = k.parametres.find(cle); it != k.parametres.end() && !it->second.empty()) return codeTaille(it->second).empty() ? it->second : codeTaille(it->second);
  }
  if (const std::string t = codeTaille(k.empreinte); !t.empty()) return t;
  // « 10k 0603 » : le boîtier écrit dans la valeur
  std::string mot;
  for (char ch : k.valeur + " ") {
    if (ch == ' ') {
      if (mot.size() == 4 && std::all_of(mot.begin(), mot.end(), [](char x) { return x >= '0' && x <= '9'; })) return mot;
      mot.clear();
    } else
      mot += ch;
  }
  return {};
}

std::vector<std::string> composantsDeRegle(const ConceptionElec& c, const RegleEquivalence& r, bool aussiAffectes) {
  std::vector<std::string> v;
  for (const auto& k : c.composants) {
    if (k.horsCarte || (!aussiAffectes && !k.empreinte.empty())) continue;
    if (r.famille != FamilleValeur::Aucune && familleDe(c, k) != r.famille) continue;
    if (!r.reference.empty() && !correspondJoker(r.reference, k.reference)) continue;
    if (!r.boitier.empty() && boitierDeComposant(k) != r.boitier) continue;
    if (!r.valeur.empty()) {
      // « 100n* » : le motif sur la valeur sans le boîtier qu'elle peut porter
      std::string val = k.valeur;
      if (const auto b = boitierDeComposant(k); !b.empty())
        if (const auto i = val.find(" " + b); i != std::string::npos) val.erase(i);
      ComposantElec sans = k;
      sans.valeur = val;
      if (!valeurVisee(c, sans, r.valeur)) continue;
    }
    v.push_back(k.uuid);
  }
  return v;
}

int appliquerEquivalences(ConceptionElec& c, const std::vector<RegleEquivalence>& regles, bool aussiAffectes, std::vector<std::string>* journal) {
  exigerModifiable(c);
  std::set<std::string> faits;
  int n = 0;
  for (const auto& r : regles) {
    if (!c.empreinte(r.empreinte)) {
      if (journal) journal->push_back("« " + r.texte + " » : empreinte « " + r.empreinte + " » introuvable");
      continue;
    }
    std::vector<std::string> vises;
    for (const auto& id : composantsDeRegle(c, r, aussiAffectes))
      if (faits.insert(id).second) vises.push_back(id);
    const int k = echangerEmpreintesEnMasse(c, vises, r.empreinte);
    if (journal) journal->push_back("« " + r.texte + " » : " + std::to_string(k) + " composant" + (k > 1 ? "s" : "") + " affecté" + (k > 1 ? "s" : ""));
    n += k;
  }
  return n;
}

}  // namespace prom::ecad
