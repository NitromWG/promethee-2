// Génère les cas de référence du prototype web (noyau JS) pour vérifier le portage C++.
const THREE = require('three');
const P = require('./core.js');
const cas = [];
function ajouterCas(nom, p) {
  const source = JSON.parse(JSON.stringify(p));
  p = P.normaliser(JSON.parse(JSON.stringify(p)));
  const d = P.deriver(p), pb = P.verifier(p, d);
  const vc = P.volume(P.construireCorps(p, d, THREE, { seg: 48, segCercle: 192 }));
  const vl = P.volume(P.construireCouvercle(p, d, THREE, { seg: 48, segCercle: 192 }));
  const champs = ['cx', 'cy', 'Li', 'Wi', 'ri', 'Lo', 'Wo', 'ro', 'zf', 'zpb', 'zpt', 'Rb', 'rp', 'rTete', 'sb', 'sa', 'H', 'zt', 'ztop', 'zLevre', 'longVis'];
  const der = {}; for (const k of champs) der[k] = d[k];
  der.lo = d.lo; der.li = d.li;
  der.trous = d.trous.map((t) => ({ id: t.id, x: t.x, y: t.y }));
  der.comps = d.comps.map((g) => ({ id: g.id, x: g.x, y: g.y, hx: g.hx, hy: g.hy, rot: g.rot, top: g.top, face: g.face === undefined ? null : g.face, decoupe: g.decoupe || null, trouCouvercle: g.trouCouvercle || null }));
  der.decoupesValides = Object.fromEntries(Object.entries(P.decoupesValides(d)).map(([m, l]) => [m, l.map((o) => o.id)]));
  der.piliersValides = P.piliersValides(d).map((q) => q.id);
  der.percagesValides = P.percagesValides(d).map((q) => q.id);
  // séquence de corrections automatiques
  const q = JSON.parse(JSON.stringify(p)), corrections = [];
  for (let i = 0; i < 12; i++) { const f = P.verifier(q, P.deriver(q)).find((x) => x.fix); if (!f) break; corrections.push(f.fix.libelle); P.corriger(q, f.fix); }
  const dq = P.deriver(q);
  cas.push({
    nom, source, projet: p, derive: der,
    problemes: pb.map((x) => ({ sev: x.sev, code: x.code, msg: x.msg, ids: x.ids, fix: x.fix ? x.fix.libelle : null })),
    volumes: { corps: vc, couvercle: vl },
    corrige: { corrections, projet: q, H: dq.H, problemes: P.verifier(q, dq).map((x) => x.code) }
  });
}
ajouterCas('exemple', P.exemple());
const murs = P.exemple();
murs.composants.push({ id: 'x1', ref: 'J3', type: 'usbc', bord: 'E', le_long: 0, ecart: 0.4 });
murs.composants.push({ id: 'x2', ref: 'J4', type: 'jack', bord: 'N', le_long: 8, ecart: 0.4 });
murs.composants.push({ id: 'x3', ref: 'J5', type: 'usbc', bord: 'S', le_long: 15, ecart: 0.4, decoupe: { w: 12.5, h: 7, r: 0 } });
ajouterCas('murs', murs);
ajouterCas('rayon_nul', { carte: { L: 80, W: 50, r: 0, t: 1.6 }, boitier: { jeu: 0.5, paroi: 1.6, fond: 1.6, entretoise: 4, hauteur: 15, couvercle: 1.6, vis: 'M2.5' },
  trous: [{ coin: 'SW', ox: 4, oy: 4 }, { coin: 'SE', ox: 4, oy: 4 }, { coin: 'NE', ox: 4, oy: 4 }, { coin: 'NW', ox: 4, oy: 4 }, { x: 0, y: 0 }, { x: 10, y: 12 }],
  composants: [{ type: 'led', x: -20, y: 10 }, { type: 'bouton', x: 20, y: -10 }, { type: 'usbc', bord: 'E', le_long: -5 }] });
const aj = P.projetVide();
['usbc', 'led', 'led', 'bouton', 'module', 'condo', 'jst', 'capteur', 'ic', 'jack'].forEach((t) => P.ajouterComposant(aj, t));
P.ajouterTrou(aj);
ajouterCas('encombre', aj);
const man = P.exemple();
man.boitier.hauteurAuto = false; man.boitier.hauteur = 10; man.trous[0].ox = 1.5; man.composants[0].ecart = 3;
ajouterCas('manuel_erreurs', man);
const haut = P.exemple(); haut.boitier.hauteurAuto = false; haut.boitier.hauteur = 30;
ajouterCas('manuel_trop_haut', haut);
const fin = P.exemple(); fin.boitier.paroi = 1; fin.boitier.fond = 0.9; fin.boitier.couvercle = 1; fin.boitier.entretoise = 1.5; fin.boitier.vis = 'M2';
ajouterCas('parois_fines', fin);
ajouterCas('vide', P.projetVide());
ajouterCas('bizarre', { nom: '  Été  ', carte: { L: '75,5', W: 5, r: 99, t: 1.7, x0: 3, y0: -2 }, boitier: { jeu: '0.8', vis: 'M9', hauteurAuto: false, hauteur: 2 },
  trous: [{ coin: 'NE', ox: 3, oy: 3 }, null, { x: 'a', y: 2 }, { id: 'dbl', coin: 'XX' }, { id: 'dbl', x: 1, y: 1 }],
  composants: [{ type: 'inconnu' }, { type: 'led', rot: 45, x: 1, y: 1, trou_couvercle: 99 }, { type: 'jack', bord: 'Z', decoupe: { w: 200, h: 0, r: 50 } }, { type: 'condo', ref: '   ', h: 0 }] });
require('fs').writeFileSync(require('path').join(__dirname, '..', 'tests', 'donnees', 'reference_js.json'), JSON.stringify({ origine: 'Prototype web, jalon 0 (core.js)', cas }, null, 1));
console.log(cas.map((c) => c.nom + ': ' + c.problemes.length + ' pb, V=' + (c.volumes.corps / 1000).toFixed(3) + '/' + (c.volumes.couvercle / 1000).toFixed(3) + ' cm3, corr=' + c.corrige.corrections.length).join('\n'));
