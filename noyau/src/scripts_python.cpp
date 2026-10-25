// Prométhée : scripts Python sur les commandes annulables. Voir scripts_python.hpp. Licence GPL-3.0-only.
#include "promethee/scripts_python.hpp"

#include <filesystem>
#include <random>
#include <regex>

#include "promethee/api_commandes.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/processus.hpp"

namespace fs = std::filesystem;
using J = nlohmann::ordered_json;

namespace prom {

namespace {
// Marque des requêtes sur la sortie du script (séparateur d'enregistrement ASCII) : le reste est du texte imprimé
constexpr char MARQUE = '\x1e';

const char* MODULE = R"PY(# -*- coding: utf-8 -*-
"""Prométhée : module des scripts (lot L13). Chaque fonction est une requête à Prométhée ; tout le script forme un
seul pas d'annulation dans l'atelier. Les longueurs sont en millimètres, les fréquences en hertz."""
import json as _json
import sys as _sys

_sortie = _sys.__stdout__
_n = 0


class ErreurPromethee(Exception):
    """Requête refusée par Prométhée (raison en français)."""


def appeler(methode, **params):
    """Requête JSON-RPC brute : appeler("symbole.poser", symbole="R", x=10, y=20)."""
    global _n
    _n += 1
    params = {k: v for k, v in params.items() if v is not None}
    _sortie.write("\x1e" + _json.dumps({"jsonrpc": "2.0", "id": _n, "method": methode, "params": params}, ensure_ascii=False) + "\n")
    _sortie.flush()
    ligne = _sys.stdin.readline()
    if not ligne:
        raise SystemExit("Prométhée a fermé la liaison")
    r = _json.loads(ligne)
    if "error" in r:
        raise ErreurPromethee(r["error"].get("message", "erreur"))
    return r.get("result")


def methodes():
    return appeler("methodes")


def version():
    return appeler("version")


def projet():
    return appeler("projet.resume")


def composants(conception=None):
    return appeler("conception.composants", conception=conception)


def reseaux(conception=None):
    return appeler("conception.reseaux", conception=conception)


def poser_symbole(symbole, x, y, ref=None, valeur=None, feuille=0, rotation=0, conception=None):
    return appeler("symbole.poser", symbole=symbole, x=x, y=y, ref=ref, valeur=valeur, feuille=feuille, rotation=rotation, conception=conception)


def valeur(ref, valeur, conception=None):
    return appeler("composant.valeur", ref=ref, valeur=str(valeur), conception=conception)


def poser_via(x, y, reseau=None, diametre=0.8, percage=0.4, conception=None):
    return appeler("via.poser", x=x, y=y, reseau=reseau, diametre=diametre, percage=percage, conception=conception)


def poser_piste(a, b, largeur=0.25, couche="F.Cu", reseau=None, conception=None):
    return appeler("piste.poser", a=list(a), b=list(b), largeur=largeur, couche=couche, reseau=reseau, conception=conception)


def erc(conception=None):
    return appeler("erc", conception=conception)


def drc(conception=None):
    return appeler("drc", conception=conception)


def revue(conception=None):
    return appeler("revue", conception=conception)


def cout(cartes=1, variante=None, conception=None):
    return appeler("cout", cartes=cartes, variante=variante, conception=conception)


def analyse_ac(debut=10, fin=1e6, points=50):
    return {"type": "ac", "debut": debut, "fin": fin, "points": points}


def analyse_tran(pas=1e-5, duree=1e-2):
    return {"type": "tran", "pas": pas, "duree": duree}


def mesurer(mesure, reseau, analyse=None, parametre=None, conception=None):
    """Mesure calculée par ngspice : mesurer("coupure", "SORTIE", analyse_ac()) → nombre (unité SI)."""
    r = appeler("simulation.mesurer", mesure=mesure, reseau=reseau, analyse=analyse or analyse_ac(), parametre=parametre, conception=conception)
    return r["valeur"]


def courbe_simulee(expression, analyse=None, conception=None):
    """Courbe calculée (« db(V(SORTIE)/V(ENTREE)) ») : (x, y)."""
    r = appeler("simulation.courbe", expression=expression, analyse=analyse or analyse_ac(), conception=conception)
    return r["x"], r["y"]


def ngspice(commande):
    """Commande de lecture de ngspice sur le dernier calcul (print, meas, let…), dans l'esprit de Nutmeg."""
    return appeler("ngspice.commande", commande=commande)


def courbe(nom, x, y, axe_x="", axe_y=""):
    """Courbe tracée dans l'atelier à la fin du script."""
    return appeler("script.courbe", nom=nom, x=[float(v) for v in x], y=[float(v) for v in y], axeX=axe_x, axeY=axe_y)


def echantillons(sonde):
    """Instrument (14.5) : bloc suivant d'échantillons [(t, v), …] de la sonde ; liste vide quand il n'y en a plus."""
    return [tuple(x) for x in appeler("instrument.echantillons", sonde=sonde)]


def sondes():
    """Instrument : sondes branchées."""
    return appeler("instrument.sondes")


def afficher(nom, valeur):
    """Instrument : une mesure rendue à sa façade (« frequence », 1234.5)."""
    return appeler("instrument.afficher", nom=nom, valeur=float(valeur))


def facade(svg):
    """Instrument : façade SVG déclarée (éléments nommés bouton:, calibre:, menu:, affichage:)."""
    return appeler("instrument.facade", svg=svg)


def signaler(message, code="PY-1", objet=None, x=0.0, y=0.0, erreur=True):
    """Contrôle écrit en Python : une violation rendue avec celles de la DRC."""
    return appeler("script.signaler", message=message, code=code, objet=objet, x=x, y=y, erreur=erreur)
)PY";

const char* LANCEUR = R"PY(# -*- coding: utf-8 -*-
# Lanceur des scripts de Prométhée : module « promethee », points d'arrêt et pas à pas par bdb
import bdb, json, os, sys, traceback
sys.path.insert(0, sys.argv[1])
import promethee
fichier = os.path.abspath(sys.argv[2])
points = set(json.loads(sys.argv[3]))
pas_a_pas = sys.argv[4] == "1"
with open(fichier, encoding="utf-8") as f:
    code = f.read()


class Debogueur(bdb.Bdb):
    pas = pas_a_pas

    def user_line(self, frame):
        nom = self.canonic(frame.f_code.co_filename)
        if nom != self.canonic(fichier):
            return
        # Au départ, bdb s'arrête sur la première ligne : hors pas à pas, on file jusqu'au premier point d'arrêt
        if not self.pas and not self.get_breaks(nom, frame.f_lineno):
            self.set_continue()
            return
        variables = {}
        for k, v in frame.f_locals.items():
            if k.startswith("__") or k in ("promethee",):
                continue
            try:
                variables[k] = repr(v)[:120]
            except Exception:
                variables[k] = "?"
        r = promethee.appeler("script.pause", ligne=frame.f_lineno, variables=variables)
        if r == "arreter":
            self.set_quit()
        elif r == "pas":
            self.pas = True
            self.set_step()
        else:
            self.pas = False
            self.set_continue()


glob = {"__name__": "__main__", "__file__": fichier, "__builtins__": __builtins__}
try:
    objet = compile(code, fichier, "exec")
    if points or pas_a_pas:
        d = Debogueur()
        for ligne in points:
            d.set_break(fichier, ligne)
        d.run(objet, glob)
    else:
        exec(objet, glob)
except (SystemExit, bdb.BdbQuit):
    pass
except promethee.ErreurPromethee as e:
    tb = traceback.extract_tb(sys.exc_info()[2])
    ligne = next((x.lineno for x in reversed(tb) if os.path.abspath(x.filename) == fichier), 0)
    sys.stderr.write("Ligne %d : refusé par Prométhée : %s\n" % (ligne, e))
    sys.exit(3)
except Exception as e:
    tb = traceback.extract_tb(sys.exc_info()[2])
    ligne = next((x.lineno for x in reversed(tb) if os.path.abspath(x.filename) == fichier), 0)
    sys.stderr.write("Ligne %d : %s : %s\n" % (ligne, type(e).__name__, e))
    sys.exit(2)
)PY";

std::string programmePython(const std::string& choisi) {
  if (!choisi.empty()) return choisi;
  if (!programmeDuPoste("python3").empty()) return "python3";
  if (!programmeDuPoste("python").empty()) return "python";
  return {};
}
std::string dossierTemporaire() {
  std::random_device r;
  std::error_code ec;
  const fs::path p = fs::temp_directory_path(ec) / ("promethee-script-" + std::to_string(r()) + std::to_string(r()));
  fs::create_directories(p, ec);
  return utf8(p);
}
}  // namespace

std::string ResultatScript::nomPas() const {
  return "Script : " + std::to_string(modifications) + " modification" + (modifications > 1 ? "s" : "");
}

const std::string& modulePython() {
  static const std::string m = MODULE;
  return m;
}

bool pythonDisponible(std::string* version, const std::string& programme) {
  const std::string py = programmePython(programme);
  if (py.empty()) return false;
  const ResultatProcessus r = executerProcessus({py, "--version"});
  if (!r.reussi()) return false;
  std::string v = r.sortie.empty() ? r.erreurs : r.sortie;
  while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
  if (version) *version = v;
  return v.rfind("Python 3", 0) == 0;
}

ResultatScript executerScript(Projet& p, const std::string& code, const OptionsScript& o) {
  ResultatScript r;
  const std::string py = programmePython(o.python);
  if (py.empty()) {
    r.pythonAbsent = true;
    r.erreurs = "Python 3 n’est pas installé sur ce poste : les scripts ne peuvent pas tourner.";
    return r;
  }
  const std::string dossier = dossierTemporaire();
  const fs::path d = cheminFichier(dossier);
  const std::string nom = o.nom.empty() ? std::string("script") : o.nom;
  const fs::path script = d / cheminFichier(nomFichierAscii(nom, "script") + ".py");
  try {
    ecrireFichierAtomique(d / "promethee.py", modulePython());
    ecrireFichierAtomique(d / "lanceur_promethee.py", LANCEUR);
    ecrireFichierAtomique(script, code);
  } catch (const std::exception& e) {
    r.erreurs = e.what();
    return r;
  }
  J points = J::array();
  for (int l : o.pointsArret) points.push_back(l);
  OptionsProcessus op;
  op.delai = o.delai;
  op.environnement["PYTHONIOENCODING"] = "utf-8";
  op.environnement["PYTHONDONTWRITEBYTECODE"] = "1";
  op.environnement["PYTHONUTF8"] = "1";
  const ResultatProcessus pr = dialoguerProcessus(
      {py, "-I", "-u", utf8(d / "lanceur_promethee.py"), dossier, utf8(script), points.dump(), o.pasAPas ? "1" : "0"}, op,
      [&](const std::string& ligne) -> std::optional<std::string> {
        if (ligne.empty() || ligne[0] != MARQUE) {
          if (o.sortie) o.sortie(ligne);
          return std::nullopt;
        }
        const J q = J::parse(ligne.substr(1), nullptr, false);
        const J id = q.is_object() && q.contains("id") ? q["id"] : J();
        J rep = {{"jsonrpc", "2.0"}, {"id", id}};
        if (q.is_discarded() || !q.contains("method")) {
          rep["error"] = {{"code", -32700}, {"message", "requête illisible"}};
          return rep.dump();
        }
        const std::string m = q["method"].get<std::string>();
        const J params = q.contains("params") ? q["params"] : J::object();
        ++r.requetes;
        if (m == "script.courbe") {
          CourbeScript c;
          c.nom = params.value("nom", std::string("courbe"));
          c.axeX = params.value("axeX", std::string());
          c.axeY = params.value("axeY", std::string());
          if (params.contains("x") && params["x"].is_array())
            for (const auto& v : params["x"]) c.x.push_back(v.is_number() ? v.get<double>() : 0.0);
          if (params.contains("y") && params["y"].is_array())
            for (const auto& v : params["y"]) c.y.push_back(v.is_number() ? v.get<double>() : 0.0);
          r.courbes.push_back(c);
          rep["result"] = static_cast<int>(r.courbes.size());
          return rep.dump();
        }
        if (m == "script.signaler") {
          ViolationScript v;
          v.message = params.value("message", std::string());
          v.code = params.value("code", std::string("PY-1"));
          v.objet = params.contains("objet") && params["objet"].is_string() ? params["objet"].get<std::string>() : std::string();
          v.x = params.contains("x") && params["x"].is_number() ? params["x"].get<double>() : 0;
          v.y = params.contains("y") && params["y"].is_number() ? params["y"].get<double>() : 0;
          v.erreur = !params.contains("erreur") || !params["erreur"].is_boolean() || params["erreur"].get<bool>();
          r.violations.push_back(v);
          rep["result"] = static_cast<int>(r.violations.size());
          return rep.dump();
        }
        if (m == "script.pause") {
          const int l = params.value("ligne", 0);
          r.arrets.push_back(l);
          const std::string d2 = o.pause ? o.pause(l, params.contains("variables") ? params["variables"] : J::object()) : std::string("continuer");
          rep["result"] = d2;
          return rep.dump();
        }
        if (o.hote)
          if (const auto v = o.hote(m, params)) {
            rep["result"] = *v;
            return rep.dump();
          }
        // Conception visée par défaut : celle de l'atelier qui lance le script
        J qc = q;
        if (o.conception >= 0 && (!qc.contains("params") || (qc["params"].is_object() && !qc["params"].contains("conception")))) qc["params"]["conception"] = o.conception;
        ReponseApi a = traiterRequete(p, qc);
        if (a.modifie) {
          ++r.modifications;
          r.pas.push_back(a.nomPas);
        }
        return a.reponse.dump();
      },
      o.arret);
  std::error_code ec;
  fs::remove_all(d, ec);
  r.sortie = pr.sortie;
  r.erreurs = pr.erreurs;
  r.reussi = pr.reussi();
  if (!pr.lance) r.erreurs = pr.erreurs.empty() ? std::string("Python n’a pas pu être lancé.") : pr.erreurs;
  if (pr.delaiDepasse) r.erreurs += (r.erreurs.empty() ? "" : "\n") + std::string("Script arrêté : délai dépassé.");
  static const std::regex ligneErreur(R"(Ligne (\d+) :)");
  std::smatch m;
  if (std::regex_search(r.erreurs, m, ligneErreur)) r.ligneErreur = std::stoi(m[1].str());
  return r;
}

std::vector<ecad::Violation> executerControlesPython(const Projet& p0, const std::vector<std::pair<std::string, std::string>>& scripts, std::vector<std::string>* erreurs) {
  std::vector<ecad::Violation> v;
  for (const auto& [nom, code] : scripts) {
    Projet p = p0;  // un contrôle ne modifie rien
    OptionsScript o;
    o.nom = nom;
    o.delai = 120;
    const ResultatScript r = executerScript(p, code, o);
    if (!r.reussi && erreurs) erreurs->push_back(nom + " : " + r.erreurs);
    for (const auto& x : r.violations) {
      ecad::Violation w;
      w.gravite = x.erreur ? ecad::Gravite::Erreur : ecad::Gravite::Avertissement;
      w.code = x.code.empty() ? std::string("PY-1") : x.code;
      w.message = x.message + " (contrôle « " + nom + " »)";
      w.position = ecad::ptMm(x.x, x.y);
      if (!x.objet.empty())
        for (const auto& c : p0.electronique)
          if (const ecad::ComposantElec* k = c.parReference(x.objet)) w.objets.push_back(k->uuid);
      v.push_back(w);
    }
  }
  return v;
}

}  // namespace prom
