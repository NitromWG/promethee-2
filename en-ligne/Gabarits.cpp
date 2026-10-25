// Prométhée : gabarits intégrés des adaptateurs PLM du programme d'accès en ligne (19.14). Chacun décrit les requêtes
// d'un serveur (méthode, chemin, corps, champs de la réponse) ; une copie posée dans « gabarits/<nom>.json » à côté
// des réglages l'emporte et s'adapte au serveur de l'entreprise (versions, domaines, champs). Seul « simule » est
// vérifié par les essais (tests/plm_simule.py) ; les autres suivent la documentation publique de leurs interfaces
// REST et se vérifient sur le serveur de l'entreprise. Licence GPL-3.0-only.
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace prom::enligne {

namespace {
struct Integre {
  const char* nom;
  const char* json;
};
const Integre kGabarits[] = {
    {"simule", R"({
  "nom": "simule",
  "description": "Serveur PLM simulé des essais (tests/plm_simule.py) : dépôt, réservation, restitution en révision suivante",
  "authentification": "bearer",
  "operations": {
    "deposer": {"methode": "POST", "chemin": "/api/articles",
                "corps": "{\"numero\": \"{numero}\", \"revision\": \"{revision}\", \"designation\": \"{designation}\", \"manifeste\": {manifeste}, \"fichiers\": {fichiers}}"},
    "reserver": {"methode": "POST", "chemin": "/api/articles/{numero}/reserver", "corps": "{\"utilisateur\": \"{utilisateur}\"}"},
    "restituer": {"methode": "POST", "chemin": "/api/articles/{numero}/restituer",
                  "corps": "{\"utilisateur\": \"{utilisateur}\", \"manifeste\": {manifeste}, \"fichiers\": {fichiers}}"},
    "consulter": {"methode": "GET", "chemin": "/api/articles/{numero}"}
  }
})"},
    {"distributeur-simule", R"({
  "nom": "distributeur-simule",
  "description": "Distributeur simulé des essais (tests/plm_simule.py) : offres d'une référence fabricant",
  "authentification": "bearer",
  "operations": {
    "offres": {"methode": "GET", "chemin": "/api/offres/{mpn}"}
  }
})"},
    {"windchill", R"({
  "nom": "windchill",
  "description": "PTC Windchill, Windchill REST Services (OData, domaine ProdMgmt) ; jeton CSRF demandé par PTC/GetCSRFToken() selon la version",
  "authentification": "basic",
  "operations": {
    "deposer": {"methode": "POST", "chemin": "/Windchill/servlet/odata/ProdMgmt/Parts",
                "corps": "{\"Number\": \"{numero}\", \"Name\": \"{designation}\", \"View\": \"Design\"}",
                "revision": "Revision", "etat": "State.Display", "entetes": {"CSRF_NONCE": "{csrf}"}},
    "reserver": {"methode": "POST", "chemin": "/Windchill/servlet/odata/ProdMgmt/Parts('{identifiant}')/PTC.ProdMgmt.CheckOut",
                 "corps": "{\"CheckOutNote\": \"Prométhée\"}", "revision": "Revision", "etat": "State.Display", "entetes": {"CSRF_NONCE": "{csrf}"}},
    "restituer": {"methode": "POST", "chemin": "/Windchill/servlet/odata/ProdMgmt/Parts('{identifiant}')/PTC.ProdMgmt.CheckIn",
                  "corps": "{\"CheckInNote\": \"Révision publiée par Prométhée\"}", "revision": "Revision", "etat": "State.Display", "entetes": {"CSRF_NONCE": "{csrf}"}},
    "consulter": {"methode": "GET", "chemin": "/Windchill/servlet/odata/ProdMgmt/Parts?$filter=Number%20eq%20'{numero}'", "revision": "value.0.Revision", "etat": "value.0.State.Display"}
  }
})"},
    {"teamcenter", R"({
  "nom": "teamcenter",
  "description": "Siemens Teamcenter, services REST (Teamcenter Service-Oriented Architecture) : création, réservation (checkout), restitution (checkin), révision",
  "authentification": "basic",
  "operations": {
    "deposer": {"methode": "POST", "chemin": "/tc/RestServices/Core-2008-06-DataManagement/createObjects",
                "corps": "{\"body\": {\"input\": [{\"data\": {\"boName\": \"Item\", \"stringProps\": {\"item_id\": \"{numero}\", \"object_name\": \"{designation}\"}}}]}}",
                "revision": "output.0.objects.1.props.item_revision_id.dbValues.0"},
    "reserver": {"methode": "POST", "chemin": "/tc/RestServices/Core-2006-03-Reservation/checkout",
                 "corps": "{\"body\": {\"objects\": [{\"uid\": \"{identifiant}\"}], \"comment\": \"Prométhée\"}}"},
    "restituer": {"methode": "POST", "chemin": "/tc/RestServices/Core-2006-03-Reservation/checkin", "corps": "{\"body\": {\"objects\": [{\"uid\": \"{identifiant}\"}]}}"},
    "consulter": {"methode": "POST", "chemin": "/tc/RestServices/Core-2007-01-DataManagement/getItemFromId",
                  "corps": "{\"body\": {\"infos\": [{\"itemId\": \"{numero}\"}]}}", "revision": "output.0.itemRevOutput.0.itemRevision.props.item_revision_id.dbValues.0"}
  }
})"},
    {"3dexperience", R"({
  "nom": "3dexperience",
  "description": "Dassault Systèmes 3DEXPERIENCE, services web Engineering (dseng) et Lifecycle (dslc) : création, réservation, révision",
  "authentification": "bearer",
  "operations": {
    "deposer": {"methode": "POST", "chemin": "/resources/v1/modeler/dseng/dseng:EngItem",
                "corps": "{\"items\": [{\"attributes\": {\"title\": \"{designation}\", \"description\": \"{numero}\"}}]}",
                "revision": "member.0.revision", "etat": "member.0.state", "entetes": {"ENO_CSRF_TOKEN": "{csrf}"}},
    "reserver": {"methode": "POST", "chemin": "/resources/v1/modeler/dslc/reserve",
                 "corps": "{\"data\": [{\"id\": \"{identifiant}\", \"type\": \"VPMReference\"}]}", "entetes": {"ENO_CSRF_TOKEN": "{csrf}"}},
    "restituer": {"methode": "POST", "chemin": "/resources/v1/modeler/dslc/unreserve",
                  "corps": "{\"data\": [{\"id\": \"{identifiant}\", \"type\": \"VPMReference\"}]}", "entetes": {"ENO_CSRF_TOKEN": "{csrf}"}},
    "consulter": {"methode": "GET", "chemin": "/resources/v1/modeler/dseng/dseng:EngItem/{identifiant}", "revision": "member.0.revision", "etat": "member.0.state"}
  }
})"},
    {"agile", R"({
  "nom": "agile",
  "description": "Oracle Agile PLM, interface REST : articles (items), ordres de modification, révision",
  "authentification": "basic",
  "operations": {
    "deposer": {"methode": "POST", "chemin": "/Agile/rest/items", "corps": "{\"number\": \"{numero}\", \"subclass\": \"Part\", \"description\": \"{designation}\"}",
                "revision": "revision", "etat": "lifecyclePhase"},
    "reserver": {"methode": "POST", "chemin": "/Agile/rest/items/{numero}/changes", "corps": "{\"changeType\": \"ECO\", \"description\": \"Réservé par Prométhée\"}"},
    "restituer": {"methode": "POST", "chemin": "/Agile/rest/items/{numero}/release", "corps": "{\"comment\": \"Révision publiée par Prométhée\"}", "revision": "revision", "etat": "lifecyclePhase"},
    "consulter": {"methode": "GET", "chemin": "/Agile/rest/items/{numero}", "revision": "revision", "etat": "lifecyclePhase"}
  }
})"}};
}  // namespace

QStringList gabaritsIntegres() {
  QStringList r;
  for (const auto& g : kGabarits) r << QString::fromLatin1(g.nom);
  return r;
}

QByteArray texteGabaritIntegre(const QString& nom) {
  for (const auto& g : kGabarits)
    if (nom == QLatin1String(g.nom)) return QByteArray(g.json);
  return {};
}

}  // namespace prom::enligne
