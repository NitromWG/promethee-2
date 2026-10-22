/* Prométhée, jalon 0 : noyau (modèle unique, géométrie, vérifications, exports). Licence GPL-3.0-or-later. */
(function (racine) {
  'use strict';

  // ---------- Données de référence ----------
  const VIS = {
    'M2':   { d: 2,   trou: 2.2, avant: 1.6, tete: 3.8 },
    'M2.5': { d: 2.5, trou: 2.7, avant: 2.0, tete: 4.5 },
    'M3':   { d: 3,   trou: 3.2, avant: 2.5, tete: 5.5 }
  };
  const LEVRE = { jeu: 0.25, ep: 1.2, h: 2 };
  const LONGUEURS_VIS = [4, 5, 6, 8, 10, 12, 16, 20, 25, 30];
  const PAROI_PILIER = 2.4;
  const EPAISSEURS_PCB = [0.8, 1, 1.2, 1.6, 2];

  const TYPES = {
    module:  { nom: 'Module radio', prefixe: 'U', w: 18, d: 20, h: 3.2, valeur: 'ESP32-C3-WROOM-02' },
    ic:      { nom: 'Circuit intégré', prefixe: 'U', w: 6.5, d: 7, h: 1.8, valeur: 'Régulateur 3,3 V' },
    capteur: { nom: 'Capteur', prefixe: 'U', w: 2.5, d: 2.5, h: 0.93, valeur: 'BME280' },
    usbc:    { nom: 'Connecteur USB-C', prefixe: 'J', w: 8.94, d: 7.35, h: 3.26, bord: true, decoupe: { w: 12.5, h: 7, r: 2 }, zc: 1.63, valeur: 'USB-C 16 broches' },
    jack:    { nom: 'Jack d\u2019alimentation', prefixe: 'J', w: 9, d: 14, h: 11, bord: true, decoupe: { w: 8, h: 8, r: 4 }, zc: 6.3, valeur: '5,5 × 2,1 mm' },
    jst:     { nom: 'Connecteur JST-PH', prefixe: 'J', w: 6, d: 4.5, h: 6, valeur: '2 broches' },
    led:     { nom: 'LED 3 mm', prefixe: 'D', w: 3.2, d: 3.2, h: 5.3, rond: true, couvercle: 3.4, valeur: 'Rouge' },
    bouton:  { nom: 'Bouton poussoir', prefixe: 'SW', w: 6, d: 6, h: 13, couvercle: 4.2, bouton: true, valeur: '6 × 6 mm, tige de 13 mm' },
    condo:   { nom: 'Condensateur', prefixe: 'C', w: 6.3, d: 6.3, h: 7.7, rond: true, valeur: '100 µF 16 V' }
  };

  // ---------- Outils ----------
  function nombre(v, def) { const n = typeof v === 'number' ? v : parseFloat(v); return Number.isFinite(n) ? n : def; }
  function borne(v, a, b) { return Math.min(b, Math.max(a, v)); }
  function fmt(v, dec) {
    if (!Number.isFinite(v)) return '\u2013';
    const k = Math.pow(10, dec === undefined ? 1 : dec);
    let s = (Math.round(v * k) / k).toFixed(dec === undefined ? 1 : dec);
    if (s.indexOf('.') >= 0) s = s.replace(/0+$/, '').replace(/\.$/, '');
    if (s === '-0') s = '0';
    return s.replace('.', ',').replace('-', '\u2212');
  }
  function lireNombre(txt) {
    if (typeof txt === 'number') return txt;
    const s = String(txt).trim().replace(/\s/g, '').replace(',', '.').replace('\u2212', '-');
    if (!/^-?\d*\.?\d+$|^-?\d+\.$/.test(s)) return NaN;
    return parseFloat(s);
  }
  function nouvelId(prefixe, existants) {
    let id;
    do { id = prefixe + Math.random().toString(36).slice(2, 8); } while (existants && existants.has(id));
    return id;
  }
  function sdRR(px, py, cx, cy, hx, hy, r) {
    const qx = Math.abs(px - cx) - (hx - r), qy = Math.abs(py - cy) - (hy - r);
    return Math.hypot(Math.max(qx, 0), Math.max(qy, 0)) + Math.min(Math.max(qx, qy), 0) - r;
  }
  function distRectPoint(g, x, y) {
    const dx = Math.max(g.x1 - x, 0, x - g.x2), dy = Math.max(g.y1 - y, 0, y - g.y2);
    return Math.hypot(dx, dy);
  }
  function coins(g) { return [[g.x1, g.y1], [g.x2, g.y1], [g.x2, g.y2], [g.x1, g.y2]]; }

  // ---------- Modèle ----------
  function prochaineRef(p, prefixe, exclure) {
    const pris = new Set();
    for (const k of p.composants) if (k !== exclure) pris.add(k.ref);
    for (const t of p.trous) if (t !== exclure) pris.add(t.ref);
    let i = 1; while (pris.has(prefixe + i)) i++;
    return prefixe + i;
  }

  function normaliser(src) {
    const s = src && typeof src === 'object' ? src : {};
    const c = s.carte && typeof s.carte === 'object' ? s.carte : {};
    const b = s.boitier && typeof s.boitier === 'object' ? s.boitier : {};
    const t0 = nombre(c.t, 1.6);
    const p = {
      format: 'promethee-projet', version: 1,
      nom: typeof s.nom === 'string' && s.nom.trim() ? s.nom.trim().slice(0, 80) : 'Projet sans nom',
      carte: { x0: nombre(c.x0, 0), y0: nombre(c.y0, 0), L: borne(nombre(c.L, 60), 10, 300), W: borne(nombre(c.W, 40), 10, 300), r: 0, t: EPAISSEURS_PCB.indexOf(t0) >= 0 ? t0 : 1.6 },
      boitier: {
        jeu: borne(nombre(b.jeu, 1), 0.2, 10), paroi: borne(nombre(b.paroi, 2), 0.8, 8), fond: borne(nombre(b.fond, 2), 0.8, 8),
        entretoise: borne(nombre(b.entretoise, 5), 1, 30), hauteur: 20, couvercle: borne(nombre(b.couvercle, 2), 0.8, 8),
        vis: VIS[b.vis] ? b.vis : 'M3', hauteurAuto: typeof b.hauteurAuto === 'boolean' ? b.hauteurAuto : true
      },
      trous: [], composants: []
    };
    p.carte.r = borne(nombre(c.r, 2), 0, Math.min(p.carte.L, p.carte.W) / 2 - 0.5);
    p.boitier.hauteur = borne(nombre(b.hauteur, 20), p.boitier.entretoise + p.carte.t + 1, 200);
    const ids = new Set();
    (Array.isArray(s.trous) ? s.trous : []).slice(0, 40).forEach(function (t) {
      if (!t || typeof t !== 'object') return;
      const id = typeof t.id === 'string' && t.id && !ids.has(t.id) ? t.id : nouvelId('t', ids);
      ids.add(id);
      const o = { id: id, ref: typeof t.ref === 'string' && t.ref.trim() ? t.ref.trim().slice(0, 12) : '' };
      if (['NE', 'NW', 'SE', 'SW'].indexOf(t.coin) >= 0) { o.coin = t.coin; o.ox = borne(nombre(t.ox, 4), -300, 300); o.oy = borne(nombre(t.oy, 4), -300, 300); }
      else { o.x = nombre(t.x, p.carte.x0); o.y = nombre(t.y, p.carte.y0); }
      p.trous.push(o);
    });
    (Array.isArray(s.composants) ? s.composants : []).slice(0, 200).forEach(function (k) {
      if (!k || typeof k !== 'object' || !TYPES[k.type]) return;
      const T = TYPES[k.type];
      const id = typeof k.id === 'string' && k.id && !ids.has(k.id) ? k.id : nouvelId('c', ids);
      ids.add(id);
      const o = {
        id: id, ref: typeof k.ref === 'string' && k.ref.trim() ? k.ref.trim().slice(0, 12) : '', type: k.type,
        valeur: typeof k.valeur === 'string' ? k.valeur.slice(0, 60) : T.valeur,
        w: borne(nombre(k.w, T.w), 0.5, 150), d: borne(nombre(k.d, T.d), 0.5, 150), h: borne(nombre(k.h, T.h), 0.2, 100)
      };
      if (T.bord) {
        o.bord = ['N', 'S', 'E', 'W'].indexOf(k.bord) >= 0 ? k.bord : 'W';
        o.le_long = nombre(k.le_long, (o.bord === 'N' || o.bord === 'S') ? p.carte.x0 : p.carte.y0);
        o.ecart = borne(nombre(k.ecart, 0.4), 0, 10);
        const dc = k.decoupe && typeof k.decoupe === 'object' ? k.decoupe : {};
        o.decoupe = { w: borne(nombre(dc.w, T.decoupe.w), 1, 100), h: borne(nombre(dc.h, T.decoupe.h), 1, 100), r: 0 };
        o.decoupe.r = borne(nombre(dc.r, T.decoupe.r), 0, Math.min(o.decoupe.w, o.decoupe.h) / 2);
        o.zc = borne(nombre(k.zc, T.zc), 0, 100);
      } else {
        o.x = nombre(k.x, p.carte.x0); o.y = nombre(k.y, p.carte.y0);
        const r = nombre(k.rot, 0); o.rot = [0, 90, 180, 270].indexOf(r) >= 0 ? r : 0;
      }
      if (T.couvercle) o.trou_couvercle = borne(nombre(k.trou_couvercle, T.couvercle), 0.5, 30);
      p.composants.push(o);
    });
    for (const t of p.trous) if (!t.ref) t.ref = prochaineRef(p, 'T', t);
    for (const k of p.composants) if (!k.ref) k.ref = prochaineRef(p, TYPES[k.type].prefixe, k);
    return p;
  }

  function exemple() {
    return normaliser({
      nom: 'Station météo ESP32',
      carte: { x0: 0, y0: 0, L: 60, W: 40, r: 2, t: 1.6 },
      boitier: { jeu: 1, paroi: 2, fond: 2, entretoise: 5, hauteur: 18, couvercle: 2, vis: 'M3', hauteurAuto: true },
      trous: [
        { id: 't1', ref: 'T1', coin: 'SW', ox: 4.5, oy: 4.5 },
        { id: 't2', ref: 'T2', coin: 'SE', ox: 4.5, oy: 4.5 },
        { id: 't3', ref: 'T3', coin: 'NE', ox: 4.5, oy: 4.5 },
        { id: 't4', ref: 'T4', coin: 'NW', ox: 4.5, oy: 4.5 }
      ],
      composants: [
        { id: 'c1', ref: 'J1', type: 'usbc', bord: 'W', le_long: 0, ecart: 0.4 },
        { id: 'c2', ref: 'U1', type: 'module', x: 16, y: 0, rot: 90 },
        { id: 'c3', ref: 'U2', type: 'ic', valeur: 'AMS1117-3.3', x: -16.5, y: 0, rot: 0 },
        { id: 'c4', ref: 'U3', type: 'capteur', x: -3, y: -5, rot: 0 },
        { id: 'c5', ref: 'C1', type: 'condo', x: -3, y: 6, rot: 0 },
        { id: 'c6', ref: 'D1', type: 'led', valeur: 'Verte', x: -14, y: 13, rot: 0 },
        { id: 'c7', ref: 'D2', type: 'led', valeur: 'Rouge', x: -8.5, y: 13, rot: 0 },
        { id: 'c8', ref: 'SW1', type: 'bouton', x: -14, y: -12, rot: 0 },
        { id: 'c9', ref: 'J2', type: 'jst', valeur: 'Batterie, 2 broches', x: 2, y: -15, rot: 0 }
      ]
    });
  }

  function projetVide() {
    return normaliser({
      nom: 'Nouveau projet',
      carte: { x0: 0, y0: 0, L: 50, W: 30, r: 1.5, t: 1.6 },
      boitier: { jeu: 1, paroi: 2, fond: 2, entretoise: 5, hauteur: 12, couvercle: 2, vis: 'M3', hauteurAuto: true },
      trous: [
        { ref: 'T1', coin: 'SW', ox: 4, oy: 4 }, { ref: 'T2', coin: 'SE', ox: 4, oy: 4 },
        { ref: 'T3', coin: 'NE', ox: 4, oy: 4 }, { ref: 'T4', coin: 'NW', ox: 4, oy: 4 }
      ],
      composants: []
    });
  }

  function posTrou(p, t) {
    const c = p.carte;
    if (t.coin) {
      const sx = t.coin.indexOf('E') >= 0 ? 1 : -1, sy = t.coin.indexOf('N') >= 0 ? 1 : -1;
      return { x: c.x0 + sx * (c.L / 2 - t.ox), y: c.y0 + sy * (c.W / 2 - t.oy) };
    }
    return { x: t.x, y: t.y };
  }

  function ancrerTrou(p, t, x, y) {
    const c = p.carte;
    const sx = x >= c.x0 ? 1 : -1, sy = y >= c.y0 ? 1 : -1;
    const ox = c.L / 2 - sx * (x - c.x0), oy = c.W / 2 - sy * (y - c.y0);
    if (ox <= 15 && oy <= 15) {
      t.coin = (sy > 0 ? 'N' : 'S') + (sx > 0 ? 'E' : 'W'); t.ox = ox; t.oy = oy; delete t.x; delete t.y;
    } else { delete t.coin; delete t.ox; delete t.oy; t.x = x; t.y = y; }
  }

  function placerSurBord(p, k, x, y) {
    const c = p.carte;
    const dist = { E: Math.abs(c.x0 + c.L / 2 - x), W: Math.abs(x - (c.x0 - c.L / 2)), N: Math.abs(c.y0 + c.W / 2 - y), S: Math.abs(y - (c.y0 - c.W / 2)) };
    let bord = 'E';
    for (const b of ['W', 'N', 'S']) if (dist[b] < dist[bord]) bord = b;
    k.bord = bord;
    const horizontal = bord === 'N' || bord === 'S';
    const centre = horizontal ? c.x0 : c.y0, demi = (horizontal ? c.L : c.W) / 2;
    const lim = Math.max(0, demi - k.w / 2 - c.r);
    k.le_long = borne(horizontal ? x : y, centre - lim, centre + lim);
  }

  function longueurVis(t, v) {
    for (const L of LONGUEURS_VIS) if (L - t >= 1.5 * v.d - 0.2) return L;
    return LONGUEURS_VIS[LONGUEURS_VIS.length - 1];
  }

  // ---------- Grandeurs dérivées : tout le boîtier découle de la carte ----------
  function geoComp(p, d, k) {
    const c = p.carte;
    const g = { id: k.id, ref: k.ref, type: k.type, h: k.h, top: d.zpt + k.h, bord: k.bord || null, w: k.w, d: k.d };
    if (k.bord) {
      const jeu = p.boitier.jeu;
      if (k.bord === 'E' || k.bord === 'W') {
        const s = k.bord === 'E' ? 1 : -1;
        g.face = c.x0 + s * (c.L / 2 + jeu - k.ecart);
        g.x = g.face - s * k.d / 2; g.y = k.le_long; g.hx = k.d / 2; g.hy = k.w / 2;
      } else {
        const s = k.bord === 'N' ? 1 : -1;
        g.face = c.y0 + s * (c.W / 2 + jeu - k.ecart);
        g.y = g.face - s * k.d / 2; g.x = k.le_long; g.hx = k.w / 2; g.hy = k.d / 2;
      }
      g.decoupe = { mur: k.bord, u: k.le_long, zc: d.zpt + k.zc, w: k.decoupe.w, h: k.decoupe.h, r: k.decoupe.r };
      g.rot = { E: 0, N: 90, W: 180, S: 270 }[k.bord];
    } else {
      const swap = k.rot === 90 || k.rot === 270;
      g.x = k.x; g.y = k.y; g.hx = (swap ? k.d : k.w) / 2; g.hy = (swap ? k.w : k.d) / 2; g.rot = k.rot;
    }
    g.x1 = g.x - g.hx; g.x2 = g.x + g.hx; g.y1 = g.y - g.hy; g.y2 = g.y + g.hy;
    if (k.trou_couvercle) g.trouCouvercle = { x: g.x, y: g.y, d: k.trou_couvercle };
    return g;
  }

  function deriver(p) {
    const c = p.carte, b = p.boitier, v = VIS[b.vis];
    const d = { cx: c.x0, cy: c.y0 };
    d.Li = c.L + 2 * b.jeu; d.Wi = c.W + 2 * b.jeu;
    d.ri = Math.max(c.r + b.jeu, 0.5);
    d.Lo = d.Li + 2 * b.paroi; d.Wo = d.Wi + 2 * b.paroi; d.ro = d.ri + b.paroi;
    d.zf = b.fond; d.zpb = d.zf + b.entretoise; d.zpt = d.zpb + c.t;
    d.vis = v; d.Rb = v.avant / 2 + PAROI_PILIER; d.rp = v.avant / 2; d.rTete = v.tete / 2 + 0.6;
    d.lo = { hx: d.Li / 2 - LEVRE.jeu, hy: d.Wi / 2 - LEVRE.jeu, r: Math.max(d.ri - LEVRE.jeu, 0.3) };
    d.li = { hx: d.lo.hx - LEVRE.ep, hy: d.lo.hy - LEVRE.ep, r: Math.max(d.lo.r - LEVRE.ep, 0.3) };
    d.sb = d.Li / 2 - d.ri; d.sa = d.Wi / 2 - d.ri;
    d.trous = p.trous.map(function (t) { const q = posTrou(p, t); return { id: t.id, ref: t.ref, x: q.x, y: q.y }; });
    d.comps = p.composants.map(function (k) { return geoComp(p, d, k); });
    d.longVis = longueurVis(c.t, v);
    d.H = b.hauteurAuto ? borne(hauteurIdeale(p, d), b.entretoise + c.t + 1, 200) : b.hauteur;
    d.zt = d.zf + d.H; d.ztop = d.zt + b.couvercle; d.zLevre = d.zt - LEVRE.h;
    return d;
  }

  function dansLevre(d, g) {
    let m = -Infinity;
    for (const q of coins(g)) m = Math.max(m, sdRR(q[0], q[1], d.cx, d.cy, d.li.hx, d.li.hy, d.li.r));
    return m > -0.2;
  }

  function hauteurIdeale(p, d) {
    const b = p.boitier, c = p.carte, base = b.entretoise + c.t;
    let H = base + 1;
    for (const g of d.comps) {
      if (TYPES[g.type].bouton) continue;
      H = Math.max(H, base + g.h + (dansLevre(d, g) ? LEVRE.h + 0.3 : 0.5));
      if (g.decoupe) H = Math.max(H, g.decoupe.zc + g.decoupe.h / 2 - d.zf + LEVRE.h + 0.2);
    }
    for (const g of d.comps) if (TYPES[g.type].bouton) H = Math.max(H, base + g.h - b.couvercle);
    return Math.min(200, Math.ceil(H * 2 - 1e-9) / 2);
  }

  // ---------- Vérifications continues ----------
  function verifier(p, d) {
    const pb = [], c = p.carte, b = p.boitier;
    const ajouter = function (sev, code, msg, ids, fix) { pb.push({ sev: sev, code: code, msg: msg, ids: ids || [], fix: fix || null }); };
    const sdCarte = function (x, y) { return sdRR(x, y, c.x0, c.y0, c.L / 2, c.W / 2, c.r); };
    const corrHauteur = { libelle: 'Ajuster la hauteur', type: 'hauteur' };

    for (const g of d.comps) {
      if (g.bord) {
        const horizontal = g.bord === 'N' || g.bord === 'S';
        const dedans = horizontal ? [[g.x1, g.face - Math.sign(g.face - c.y0) * g.d], [g.x2, g.face - Math.sign(g.face - c.y0) * g.d]]
                                  : [[g.face - Math.sign(g.face - c.x0) * g.d, g.y1], [g.face - Math.sign(g.face - c.x0) * g.d, g.y2]];
        const hors = Math.max(sdCarte(dedans[0][0], dedans[0][1]), sdCarte(dedans[1][0], dedans[1][1]));
        if (hors > 0.01) ajouter('erreur', 'bord_hors', g.ref + ' dépasse de l\u2019angle de la carte de ' + fmt(hors) + ' mm.', [g.id]);
        const k = p.composants.find(function (q) { return q.id === g.id; });
        const portee = b.jeu - k.ecart;
        if (k.ecart > 2) ajouter('erreur', 'connecteur_loin', g.ref + ' est à ' + fmt(k.ecart) + ' mm de la paroi : la fiche n\u2019entrera pas.', [g.id], { libelle: 'Rapprocher de la paroi', type: 'ecart', id: g.id });
        else if (k.ecart < 0.1) ajouter('erreur', 'connecteur_paroi', g.ref + ' touche la paroi du boîtier.', [g.id], { libelle: 'Écarter de 0,4 mm', type: 'ecart', id: g.id });
        if (portee > k.d - 2) ajouter('erreur', 'connecteur_porte', g.ref + ' dépasse de ' + fmt(portee) + ' mm du bord de la carte : il ne reste que ' + fmt(k.d - portee) + ' mm pour le souder.', [g.id]);
        const o = g.decoupe, centre = horizontal ? d.cx : d.cy, demi = horizontal ? d.sb : d.sa;
        if (Math.abs(o.u - centre) + o.w / 2 > demi - 0.3) ajouter('erreur', 'decoupe_angle', 'La découpe de ' + g.ref + ' tombe dans l\u2019angle du boîtier. Rapproche le connecteur du milieu du bord.', [g.id]);
        if (o.zc - o.h / 2 < d.zf + 0.3) ajouter('erreur', 'decoupe_bas', 'La découpe de ' + g.ref + ' descend dans le fond du boîtier.', [g.id]);
        else if (o.zc + o.h / 2 > d.zt - 0.3) ajouter('erreur', 'decoupe_haut', 'La découpe de ' + g.ref + ' dépasse le haut de la paroi.', [g.id], corrHauteur);
        else if (o.zc + o.h / 2 > d.zLevre - 0.2) ajouter('erreur', 'decoupe_levre', 'La lèvre du couvercle masque le haut de la découpe de ' + g.ref + '.', [g.id], corrHauteur);
      } else {
        let hors = -Infinity;
        for (const q of coins(g)) hors = Math.max(hors, sdCarte(q[0], q[1]));
        if (hors > 0.01) ajouter('erreur', 'hors_carte', g.ref + ' dépasse du bord de la carte de ' + fmt(hors) + ' mm.', [g.id]);
      }
    }
    const decs = d.comps.filter(function (g) { return g.decoupe; });
    for (let i = 0; i < decs.length; i++) for (let j = i + 1; j < decs.length; j++) {
      const a = decs[i].decoupe, e = decs[j].decoupe;
      if (a.mur === e.mur && Math.abs(a.u - e.u) < (a.w + e.w) / 2 + 0.6 && Math.abs(a.zc - e.zc) < (a.h + e.h) / 2 + 0.6)
        ajouter('erreur', 'decoupes', 'Les découpes de ' + decs[i].ref + ' et ' + decs[j].ref + ' se touchent.', [decs[i].id, decs[j].id]);
    }
    for (let i = 0; i < d.comps.length; i++) for (let j = i + 1; j < d.comps.length; j++) {
      const a = d.comps[i], e = d.comps[j];
      if (a.x1 < e.x2 - 0.05 && e.x1 < a.x2 - 0.05 && a.y1 < e.y2 - 0.05 && e.y1 < a.y2 - 0.05)
        ajouter('erreur', 'chevauchement', a.ref + ' et ' + e.ref + ' se chevauchent.', [a.id, e.id]);
    }
    for (const t of d.trous) for (const g of d.comps) {
      if (distRectPoint(g, t.x, t.y) < d.rTete) ajouter('erreur', 'tete_vis', g.ref + ' empiète sur la tête de vis du trou ' + t.ref + '.', [g.id, t.id]);
    }
    const minBord = d.vis.trou / 2 + 1;
    for (const t of d.trous) {
      const bord = -sdCarte(t.x, t.y);
      if (bord < 0) ajouter('erreur', 'trou_hors', 'Le trou ' + t.ref + ' est hors de la carte.', [t.id], { libelle: 'Ramener sur la carte', type: 'trou', id: t.id });
      else if (bord < minBord - 0.01) ajouter('erreur', 'trou_bord', 'Le trou ' + t.ref + ' est à ' + fmt(bord) + ' mm du bord de la carte (' + fmt(minBord) + ' mm au minimum).', [t.id], { libelle: 'Éloigner du bord', type: 'trou', id: t.id });
      else {
        const paroi = -sdRR(t.x, t.y, d.cx, d.cy, d.Li / 2, d.Wi / 2, d.ri);
        if (paroi < d.Rb + 0.3) ajouter('erreur', 'pilier_paroi', 'Le pilier du trou ' + t.ref + ' touche la paroi du boîtier.', [t.id], { libelle: 'Éloigner de la paroi', type: 'trou', id: t.id });
      }
    }
    for (let i = 0; i < d.trous.length; i++) for (let j = i + 1; j < d.trous.length; j++) {
      const a = d.trous[i], e = d.trous[j];
      if (Math.hypot(a.x - e.x, a.y - e.y) < 2 * d.Rb + 0.4) ajouter('erreur', 'piliers', 'Les piliers des trous ' + a.ref + ' et ' + e.ref + ' se touchent.', [a.id, e.id]);
    }
    for (const g of d.comps) {
      const T = TYPES[g.type];
      if (T.bouton) {
        if (g.top < d.zt - 0.3) ajouter('alerte', 'bouton_bas', g.ref + ' n\u2019atteint pas le couvercle : il manque ' + fmt(d.zt - g.top) + ' mm pour pouvoir l\u2019appuyer.', [g.id], hauteurIdeale(p, d) < d.H - 0.01 ? corrHauteur : null);
        else if (g.top > d.ztop + 3) ajouter('alerte', 'bouton_haut', g.ref + ' dépasse du couvercle de ' + fmt(g.top - d.ztop) + ' mm.', [g.id]);
      } else if (g.top > d.zt - 0.5) {
        ajouter('erreur', 'hauteur', g.ref + ' touche le couvercle : il manque ' + fmt(g.top - d.zt + 0.5) + ' mm.', [g.id], corrHauteur);
      } else if (dansLevre(d, g) && g.top > d.zLevre - 0.3) {
        ajouter('erreur', 'levre', g.ref + ' touche la lèvre du couvercle.', [g.id], corrHauteur);
      }
      if (g.trouCouvercle) {
        const o = g.trouCouvercle;
        if (sdRR(o.x, o.y, d.cx, d.cy, d.li.hx, d.li.hy, d.li.r) > -(o.d / 2 + 0.3)) ajouter('erreur', 'trou_couvercle', 'Le perçage de ' + g.ref + ' dans le couvercle tombe sur la lèvre.', [g.id]);
      }
    }
    const lids = d.comps.filter(function (g) { return g.trouCouvercle; });
    for (let i = 0; i < lids.length; i++) for (let j = i + 1; j < lids.length; j++) {
      const a = lids[i].trouCouvercle, e = lids[j].trouCouvercle;
      if (Math.hypot(a.x - e.x, a.y - e.y) < (a.d + e.d) / 2 + 0.8) ajouter('erreur', 'percages', 'Les perçages de ' + lids[i].ref + ' et ' + lids[j].ref + ' dans le couvercle se touchent.', [lids[i].id, lids[j].id]);
    }
    if (!p.trous.length) ajouter('alerte', 'sans_trou', 'Aucun trou de fixation : la carte ne tiendra pas dans le boîtier.', []);
    if (b.paroi < 1.2) ajouter('alerte', 'paroi', 'Parois de ' + fmt(b.paroi) + ' mm : trop fines pour une impression solide (1,2 mm conseillé).', []);
    if (b.fond < 1) ajouter('alerte', 'fond', 'Fond de ' + fmt(b.fond) + ' mm : trop fin pour une impression solide (1 mm conseillé).', []);
    if (b.couvercle < 1.2) ajouter('alerte', 'couvercle', 'Couvercle de ' + fmt(b.couvercle) + ' mm : trop fin pour une impression solide (1,2 mm conseillé).', []);
    if (b.entretoise < 2) ajouter('alerte', 'entretoise', 'Entretoises de ' + fmt(b.entretoise) + ' mm : les pattes des composants traversants risquent de toucher le fond (2 mm conseillé).', [], { libelle: 'Passer à 2 mm', type: 'entretoise', valeur: 2 });
    const depasse = d.longVis - c.t - (b.entretoise - 0.5);
    if (p.trous.length && depasse > 0.01) {
      const v2 = Math.ceil((d.longVis - c.t + 0.5) * 2) / 2;
      ajouter('alerte', 'vis_longue', 'Les vis ' + b.vis + ' × ' + d.longVis + ' toucheront le fond : entretoises de ' + fmt(b.entretoise) + ' mm trop courtes.', [], { libelle: 'Entretoises de ' + fmt(v2) + ' mm', type: 'entretoise', valeur: v2 });
    }
    return pb;
  }

  function corriger(p, fix) {
    if (!fix) return;
    if (fix.type === 'hauteur') { p.boitier.hauteur = borne(hauteurIdeale(p, deriver(p)), p.boitier.entretoise + p.carte.t + 1, 200); }
    else if (fix.type === 'ecart') { const k = p.composants.find(function (q) { return q.id === fix.id; }); if (k) k.ecart = 0.4; }
    else if (fix.type === 'entretoise') { p.boitier.entretoise = borne(fix.valeur, 1, 30); p.boitier.hauteur = Math.max(p.boitier.hauteur, p.boitier.entretoise + p.carte.t + 1); }
    else if (fix.type === 'trou') repousserTrou(p, fix.id);
  }

  function repousserTrou(p, id) {
    const t = p.trous.find(function (q) { return q.id === id; }); if (!t) return;
    const c = p.carte;
    for (let i = 0; i < 400; i++) {
      const d = deriver(p), q = posTrou(p, t);
      const bord = -sdRR(q.x, q.y, c.x0, c.y0, c.L / 2, c.W / 2, c.r);
      const paroi = -sdRR(q.x, q.y, d.cx, d.cy, d.Li / 2, d.Wi / 2, d.ri);
      if (bord >= d.vis.trou / 2 + 1 && paroi >= d.Rb + 0.3) break;
      const dx = c.x0 - q.x, dy = c.y0 - q.y;
      const fx = Math.abs(q.x - c.x0) > c.L / 2 - 6 ? Math.sign(dx) : 0, fy = Math.abs(q.y - c.y0) > c.W / 2 - 6 ? Math.sign(dy) : 0;
      const ux = fx || (fy ? 0 : Math.sign(dx)), uy = fy || (fx ? 0 : Math.sign(dy));
      if (!ux && !uy) break;
      ancrerTrou(p, t, q.x + ux * 0.25, q.y + uy * 0.25);
    }
    const q = posTrou(p, t); ancrerTrou(p, t, Math.round(q.x * 4) / 4, Math.round(q.y * 4) / 4);
  }

  // ---------- Placement automatique ----------
  const POSITIONNELS = { hors_carte: 1, bord_hors: 1, chevauchement: 1, tete_vis: 1, trou_couvercle: 1, percages: 1, decoupe_angle: 1, decoupes: 1, levre: 1, connecteur_porte: 1, trou_hors: 1, trou_bord: 1, pilier_paroi: 1, piliers: 1 };
  function gene(p, id) { return verifier(p, deriver(p)).filter(function (q) { return q.sev === 'erreur' && POSITIONNELS[q.code] && q.ids.indexOf(id) >= 0; }).length; }
  function placeLibre(p, k) {
    const c = p.carte;
    const pas = 1;
    let meilleur = { n: Infinity, x: c.x0, y: c.y0 };
    for (let r = 0; r < Math.max(c.L, c.W); r += pas) {
      for (let a = 0; a < (r === 0 ? 1 : Math.max(8, Math.round(r * 2))); a++) {
        const ang = r === 0 ? 0 : 2 * Math.PI * a / Math.max(8, Math.round(r * 2));
        k.x = Math.round((c.x0 + r * Math.cos(ang)) * 2) / 2; k.y = Math.round((c.y0 + r * Math.sin(ang)) * 2) / 2;
        const n = gene(p, k.id);
        if (!n) return true;
        if (n < meilleur.n) meilleur = { n: n, x: k.x, y: k.y };
      }
    }
    k.x = meilleur.x; k.y = meilleur.y; return false;
  }
  function bordLibre(p, k) {
    const c = p.carte;
    let meilleur = { n: Infinity, bord: 'S', u: c.x0 };
    for (const bord of ['S', 'E', 'N', 'W']) {
      const horizontal = bord === 'N' || bord === 'S', centre = horizontal ? c.x0 : c.y0, demi = (horizontal ? c.L : c.W) / 2;
      for (let s = 0; s <= demi; s += 1) for (const sg of [1, -1]) {
        k.bord = bord; k.le_long = centre + sg * s;
        const n = gene(p, k.id);
        if (!n) return true;
        if (n < meilleur.n) meilleur = { n: n, bord: bord, u: k.le_long };
      }
    }
    k.bord = meilleur.bord; k.le_long = meilleur.u; return false;
  }
  function ajouterComposant(p, type) {
    const T = TYPES[type];
    const ids = new Set(p.composants.map(function (q) { return q.id; }).concat(p.trous.map(function (q) { return q.id; })));
    const k = { id: nouvelId('c', ids), ref: prochaineRef(p, T.prefixe), type: type, valeur: T.valeur, w: T.w, d: T.d, h: T.h };
    if (T.bord) { k.bord = 'S'; k.le_long = p.carte.x0; k.ecart = 0.4; k.decoupe = { w: T.decoupe.w, h: T.decoupe.h, r: T.decoupe.r }; k.zc = T.zc; }
    else { k.x = p.carte.x0; k.y = p.carte.y0; k.rot = 0; }
    if (T.couvercle) k.trou_couvercle = T.couvercle;
    p.composants.push(k);
    if (T.bord) bordLibre(p, k); else placeLibre(p, k);
    return k;
  }
  function ajouterTrou(p) {
    const ids = new Set(p.composants.map(function (q) { return q.id; }).concat(p.trous.map(function (q) { return q.id; })));
    const t = { id: nouvelId('t', ids), ref: prochaineRef(p, 'T') };
    p.trous.push(t);
    const v = VIS[p.boitier.vis], m = Math.max(v.trou / 2 + 1, v.avant / 2 + PAROI_PILIER + 0.3 - p.boitier.jeu) + 0.5;
    for (const coin of ['SW', 'SE', 'NE', 'NW']) {
      t.coin = coin; t.ox = Math.round(m * 2) / 2 + 0.5; t.oy = t.ox;
      if (!gene(p, t.id)) return t;
    }
    delete t.coin; delete t.ox; delete t.oy; t.x = p.carte.x0; t.y = p.carte.y0;
    return t;
  }

  // ---------- Maillages imprimables (un seul volume fermé par pièce) ----------
  function contourArrondi(cx, cy, hx, hy, r, n) {
    const b = hx - r, a = hy - r, pts = [];
    const centres = [[cx + b, cy + a], [cx - b, cy + a], [cx - b, cy - a], [cx + b, cy - a]];
    for (let k = 0; k < 4; k++) for (let j = 0; j <= n; j++) {
      const ang = (k * 90 + j * 90 / n) * Math.PI / 180;
      pts.push([centres[k][0] + r * Math.cos(ang), centres[k][1] + r * Math.sin(ang)]);
    }
    return pts;
  }
  function profilArrondi(cs, ct, w, h, r, n) {
    r = Math.min(r, w / 2, h / 2);
    const brut = contourArrondi(cs, ct, w / 2, h / 2, r, n), pts = [];
    for (const q of brut) { const l = pts[pts.length - 1]; if (!l || Math.hypot(q[0] - l[0], q[1] - l[1]) > 1e-7) pts.push(q); }
    while (pts.length > 2 && Math.hypot(pts[0][0] - pts[pts.length - 1][0], pts[0][1] - pts[pts.length - 1][1]) <= 1e-7) pts.pop();
    return pts;
  }
  function cercle(cx, cy, r, m) {
    const pts = [];
    for (let k = 0; k < m; k++) { const a = 2 * Math.PI * k / m; pts.push([cx + r * Math.cos(a), cy + r * Math.sin(a)]); }
    return pts;
  }

  function Maillage() { this.t = []; }
  Maillage.prototype.tri = function (a, b, c, n) {
    const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    if (Math.hypot(nx, ny, nz) < 1e-12) return;
    if (nx * n[0] + ny * n[1] + nz * n[2] < 0) this.t.push(a[0], a[1], a[2], c[0], c[1], c[2], b[0], b[1], b[2]);
    else this.t.push(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
  };
  Maillage.prototype.quad = function (a, b, c, d, n) { this.tri(a, b, c, n); this.tri(a, c, d, n); };
  Maillage.prototype.face = function (THREE, contour, trous, vers3, n) {
    const V = THREE.Vector2;
    const c2 = contour.map(function (q) { return new V(q[0], q[1]); });
    const h2 = trous.map(function (h) { return h.map(function (q) { return new V(q[0], q[1]); }); });
    const tous = contour.concat.apply(contour, trous);
    const faces = THREE.ShapeUtils.triangulateShape(c2, h2);
    for (const f of faces) this.tri(vers3(tous[f[0]]), vers3(tous[f[1]]), vers3(tous[f[2]]), n);
  };

  const MURS = ['N', 'W', 'S', 'E'];
  function decoupesValides(d, n) {
    const res = { N: [], S: [], E: [], W: [] };
    for (const g of d.comps) {
      if (!g.decoupe) continue;
      const o = g.decoupe, horizontal = o.mur === 'N' || o.mur === 'S';
      const centre = horizontal ? d.cx : d.cy, demi = horizontal ? d.sb : d.sa;
      if (Math.abs(o.u - centre) + o.w / 2 > demi - 0.3) continue;
      if (o.zc - o.h / 2 < d.zf + 0.3 || o.zc + o.h / 2 > d.zt - 0.3) continue;
      const lst = res[o.mur];
      if (lst.some(function (q) { return Math.abs(q.s - o.u) < (q.w + o.w) / 2 + 0.3 && Math.abs(q.t - o.zc) < (q.h + o.h) / 2 + 0.3; })) continue;
      lst.push({ s: o.u, t: o.zc, w: o.w, h: o.h, pts: profilArrondi(o.u, o.zc, o.w, o.h, o.r, n || 6), id: g.id });
    }
    return res;
  }
  function piliersValides(d) {
    const res = [];
    for (const t of d.trous) {
      if (sdRR(t.x, t.y, d.cx, d.cy, d.Li / 2, d.Wi / 2, d.ri) > -(d.Rb + 0.25)) continue;
      if (res.some(function (q) { return Math.hypot(q.x - t.x, q.y - t.y) < 2 * d.Rb + 0.25; })) continue;
      res.push({ x: t.x, y: t.y, id: t.id });
    }
    return res;
  }
  function percagesValides(d) {
    const res = [];
    for (const g of d.comps) {
      const o = g.trouCouvercle; if (!o) continue;
      if (sdRR(o.x, o.y, d.cx, d.cy, d.li.hx, d.li.hy, d.li.r) > -(o.d / 2 + 0.3)) continue;
      if (res.some(function (q) { return Math.hypot(q.x - o.x, q.y - o.y) < (q.d + o.d) / 2 + 0.4; })) continue;
      res.push({ x: o.x, y: o.y, d: o.d, id: g.id });
    }
    return res;
  }

  const HAUT = [0, 0, 1], BAS = [0, 0, -1];
  function construireCorps(p, d, THREE, opt) {
    const n = (opt && opt.seg) || 10, m = (opt && opt.segCercle) || 32;
    const M = new Maillage();
    const Po = contourArrondi(d.cx, d.cy, d.Lo / 2, d.Wo / 2, d.ro, n);
    const Pi = contourArrondi(d.cx, d.cy, d.Li / 2, d.Wi / 2, d.ri, n);
    const N = Po.length, zf = d.zf, zt = d.zt;
    const dec = decoupesValides(d), pil = piliersValides(d);
    M.face(THREE, Po, [], function (q) { return [q[0], q[1], 0]; }, BAS);
    M.face(THREE, Pi, pil.map(function (b) { return cercle(b.x, b.y, d.Rb, m); }), function (q) { return [q[0], q[1], zf]; }, HAUT);
    for (let i = 0; i < N; i++) {
      const j = (i + 1) % N;
      M.quad([Po[i][0], Po[i][1], zt], [Po[j][0], Po[j][1], zt], [Pi[j][0], Pi[j][1], zt], [Pi[i][0], Pi[i][1], zt], HAUT);
    }
    const murDe = {};
    for (let k = 0; k < 4; k++) murDe[k * (n + 1) + n] = MURS[k];
    const fixe = function (mur, P) { const i = MURS.indexOf(mur) * (n + 1) + n; return (mur === 'N' || mur === 'S') ? P[i][1] : P[i][0]; };
    for (let i = 0; i < N; i++) {
      const j = (i + 1) % N, mur = murDe[i];
      for (let s = 0; s < 2; s++) {
        const P = s === 0 ? Po : Pi, za = s === 0 ? 0 : zf;
        const dx = P[j][0] - P[i][0], dy = P[j][1] - P[i][1], l = Math.hypot(dx, dy) || 1;
        const nrm = s === 0 ? [dy / l, -dx / l, 0] : [-dy / l, dx / l, 0];
        if (mur && dec[mur].length) {
          const hz = mur === 'N' || mur === 'S';
          const sA = hz ? P[i][0] : P[i][1], sB = hz ? P[j][0] : P[j][1], f = fixe(mur, P);
          const vers3 = hz ? function (q) { return [q[0], f, q[1]]; } : function (q) { return [f, q[0], q[1]]; };
          M.face(THREE, [[sA, za], [sB, za], [sB, zt], [sA, zt]], dec[mur].map(function (o) { return o.pts; }), vers3, nrm);
        } else {
          M.quad([P[i][0], P[i][1], za], [P[j][0], P[j][1], za], [P[j][0], P[j][1], zt], [P[i][0], P[i][1], zt], nrm);
        }
      }
    }
    for (const mur of MURS) for (const o of dec[mur]) {
      const hz = mur === 'N' || mur === 'S', fe = fixe(mur, Po), fi = fixe(mur, Pi), K = o.pts.length;
      const P3 = function (q, f) { return hz ? [q[0], f, q[1]] : [f, q[0], q[1]]; };
      for (let k = 0; k < K; k++) {
        const q1 = o.pts[k], q2 = o.pts[(k + 1) % K];
        const ms = (q1[0] + q2[0]) / 2 - o.s, mt = (q1[1] + q2[1]) / 2 - o.t, ln = Math.hypot(ms, mt) || 1;
        const nrm = hz ? [-ms / ln, 0, -mt / ln] : [0, -ms / ln, -mt / ln];
        M.quad(P3(q1, fe), P3(q2, fe), P3(q2, fi), P3(q1, fi), nrm);
      }
    }
    for (const b of pil) {
      const Co = cercle(b.x, b.y, d.Rb, m), Ci = cercle(b.x, b.y, d.rp, m), zb = d.zpb;
      for (let k = 0; k < m; k++) {
        const j = (k + 1) % m;
        const mx = (Co[k][0] + Co[j][0]) / 2 - b.x, my = (Co[k][1] + Co[j][1]) / 2 - b.y, l = Math.hypot(mx, my);
        M.quad([Co[k][0], Co[k][1], zf], [Co[j][0], Co[j][1], zf], [Co[j][0], Co[j][1], zb], [Co[k][0], Co[k][1], zb], [mx / l, my / l, 0]);
        M.quad([Co[k][0], Co[k][1], zb], [Co[j][0], Co[j][1], zb], [Ci[j][0], Ci[j][1], zb], [Ci[k][0], Ci[k][1], zb], HAUT);
        M.quad([Ci[k][0], Ci[k][1], zf], [Ci[j][0], Ci[j][1], zf], [Ci[j][0], Ci[j][1], zb], [Ci[k][0], Ci[k][1], zb], [-mx / l, -my / l, 0]);
        M.tri([b.x, b.y, zf], [Ci[k][0], Ci[k][1], zf], [Ci[j][0], Ci[j][1], zf], HAUT);
      }
    }
    return M.t;
  }

  function construireCouvercle(p, d, THREE, opt) {
    const n = (opt && opt.seg) || 10, m = (opt && opt.segCercle) || 32;
    const M = new Maillage();
    const Po = contourArrondi(d.cx, d.cy, d.Lo / 2, d.Wo / 2, d.ro, n);
    const Lo = contourArrondi(d.cx, d.cy, d.lo.hx, d.lo.hy, d.lo.r, n);
    const Li = contourArrondi(d.cx, d.cy, d.li.hx, d.li.hy, d.li.r, n);
    const zt = d.zt, zh = d.ztop, zl = d.zLevre, N = Po.length;
    const perc = percagesValides(d), trous = perc.map(function (o) { return cercle(o.x, o.y, o.d / 2, m); });
    M.face(THREE, Po, trous, function (q) { return [q[0], q[1], zh]; }, HAUT);
    M.face(THREE, Po, [Lo], function (q) { return [q[0], q[1], zt]; }, BAS);
    M.face(THREE, Li, trous, function (q) { return [q[0], q[1], zt]; }, BAS);
    for (let i = 0; i < N; i++) {
      const j = (i + 1) % N;
      let dx = Po[j][0] - Po[i][0], dy = Po[j][1] - Po[i][1], l = Math.hypot(dx, dy) || 1;
      M.quad([Po[i][0], Po[i][1], zt], [Po[j][0], Po[j][1], zt], [Po[j][0], Po[j][1], zh], [Po[i][0], Po[i][1], zh], [dy / l, -dx / l, 0]);
      dx = Lo[j][0] - Lo[i][0]; dy = Lo[j][1] - Lo[i][1]; l = Math.hypot(dx, dy) || 1;
      M.quad([Lo[i][0], Lo[i][1], zl], [Lo[j][0], Lo[j][1], zl], [Lo[j][0], Lo[j][1], zt], [Lo[i][0], Lo[i][1], zt], [dy / l, -dx / l, 0]);
      dx = Li[j][0] - Li[i][0]; dy = Li[j][1] - Li[i][1]; l = Math.hypot(dx, dy) || 1;
      M.quad([Li[i][0], Li[i][1], zl], [Li[j][0], Li[j][1], zl], [Li[j][0], Li[j][1], zt], [Li[i][0], Li[i][1], zt], [-dy / l, dx / l, 0]);
      M.quad([Lo[i][0], Lo[i][1], zl], [Lo[j][0], Lo[j][1], zl], [Li[j][0], Li[j][1], zl], [Li[i][0], Li[i][1], zl], BAS);
    }
    for (let h = 0; h < perc.length; h++) {
      const C = trous[h], o = perc[h];
      for (let k = 0; k < m; k++) {
        const j = (k + 1) % m;
        const mx = (C[k][0] + C[j][0]) / 2 - o.x, my = (C[k][1] + C[j][1]) / 2 - o.y, l = Math.hypot(mx, my);
        M.quad([C[k][0], C[k][1], zt], [C[j][0], C[j][1], zt], [C[j][0], C[j][1], zh], [C[k][0], C[k][1], zh], [-mx / l, -my / l, 0]);
      }
    }
    return M.t;
  }

  function volume(t) {
    let v = 0;
    for (let i = 0; i < t.length; i += 9) {
      v += t[i] * (t[i + 4] * t[i + 8] - t[i + 5] * t[i + 7]) - t[i + 1] * (t[i + 3] * t[i + 8] - t[i + 5] * t[i + 6]) + t[i + 2] * (t[i + 3] * t[i + 7] - t[i + 4] * t[i + 6]);
    }
    return v / 6;
  }
  function pourImpression(t, retourner, cx, cy) {
    const o = new Float64Array(t.length); let zmin = Infinity;
    for (let i = 0; i < t.length; i += 3) {
      let x = t[i] - cx, y = t[i + 1] - cy, z = t[i + 2];
      if (retourner) { y = -y; z = -z; }
      o[i] = x; o[i + 1] = y; o[i + 2] = z; if (z < zmin) zmin = z;
    }
    for (let i = 2; i < o.length; i += 3) o[i] -= zmin;
    return o;
  }
  function stl(t, nom) {
    const nt = t.length / 9, buf = new ArrayBuffer(84 + 50 * nt), dv = new DataView(buf);
    const entete = ('Promethee ' + nom).slice(0, 80);
    for (let i = 0; i < 80; i++) dv.setUint8(i, i < entete.length ? (entete.charCodeAt(i) & 0x7f) : 32);
    dv.setUint32(80, nt, true);
    let o = 84;
    for (let i = 0; i < t.length; i += 9) {
      const ux = t[i + 3] - t[i], uy = t[i + 4] - t[i + 1], uz = t[i + 5] - t[i + 2], vx = t[i + 6] - t[i], vy = t[i + 7] - t[i + 1], vz = t[i + 8] - t[i + 2];
      let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx; const l = Math.hypot(nx, ny, nz) || 1;
      nx /= l; ny /= l; nz /= l;
      dv.setFloat32(o, nx, true); dv.setFloat32(o + 4, ny, true); dv.setFloat32(o + 8, nz, true);
      for (let k = 0; k < 9; k++) dv.setFloat32(o + 12 + 4 * k, t[i + k], true);
      dv.setUint16(o + 48, 0, true); o += 50;
    }
    return new Uint8Array(buf);
  }

  // ---------- Nomenclature unique ----------
  function nomenclature(p, d, stats) {
    const c = p.carte, b = p.boitier, l = [];
    p.composants.slice().sort(function (a, e) { return a.ref.localeCompare(e.ref, 'fr', { numeric: true }); }).forEach(function (k) {
      l.push({ groupe: 'Électronique', ref: k.ref, designation: TYPES[k.type].nom + (k.valeur ? ', ' + k.valeur : ''), qte: 1, appro: 'Achat' });
    });
    l.push({ groupe: 'Pièces fabriquées', ref: 'PCB1', designation: 'Carte ' + fmt(c.L) + ' × ' + fmt(c.W) + ' mm, épaisseur ' + fmt(c.t) + ' mm', qte: 1, appro: 'Fabrication de circuits imprimés' });
    l.push({ groupe: 'Pièces fabriquées', ref: 'B1', designation: 'Boîtier ' + fmt(d.Lo) + ' × ' + fmt(d.Wo) + ' × ' + fmt(d.zt) + ' mm' + (stats ? ', ' + fmt(stats.volCorps / 1000) + ' cm³ de matière' : ''), qte: 1, appro: 'Impression 3D' });
    l.push({ groupe: 'Pièces fabriquées', ref: 'B2', designation: 'Couvercle ' + fmt(d.Lo) + ' × ' + fmt(d.Wo) + ' × ' + fmt(b.couvercle + LEVRE.h) + ' mm' + (stats ? ', ' + fmt(stats.volCouvercle / 1000) + ' cm³ de matière' : ''), qte: 1, appro: 'Impression 3D' });
    if (p.trous.length) l.push({ groupe: 'Visserie', ref: 'V1', designation: 'Vis autotaraudeuse ' + b.vis + ' × ' + d.longVis + ', tête cylindrique', qte: p.trous.length, appro: 'Achat' });
    return l;
  }
  function csv(lignes) {
    const esc = function (s) { s = String(s); return /[;"\r\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s; };
    const rows = ['Groupe;Référence;Désignation;Quantité;Approvisionnement'];
    for (const l of lignes) rows.push([l.groupe, l.ref, l.designation, l.qte, l.appro].map(esc).join(';'));
    return '\uFEFF' + rows.join('\r\n') + '\r\n';
  }

  // ---------- DXF (contour, perçages, empreintes) pour KiCad ----------
  function dxf(p, d) {
    const c = p.carte, ox = c.x0 - c.L / 2, oy = c.y0 - c.W / 2, L = [];
    const g = function (code, val) { L.push(String(code), String(val)); };
    const nb = function (v) { return (Math.round(v * 1e6) / 1e6).toString(); };
    const ligne = function (couche, x1, y1, x2, y2) { g(0, 'LINE'); g(8, couche); g(10, nb(x1)); g(20, nb(y1)); g(30, 0); g(11, nb(x2)); g(21, nb(y2)); g(31, 0); };
    const arc = function (couche, x, y, r, a1, a2) { g(0, 'ARC'); g(8, couche); g(10, nb(x)); g(20, nb(y)); g(30, 0); g(40, nb(r)); g(50, nb(a1)); g(51, nb(a2)); };
    const cerc = function (couche, x, y, r) { g(0, 'CIRCLE'); g(8, couche); g(10, nb(x)); g(20, nb(y)); g(30, 0); g(40, nb(r)); };
    g(0, 'SECTION'); g(2, 'HEADER'); g(9, '$ACADVER'); g(1, 'AC1009'); g(0, 'ENDSEC');
    g(0, 'SECTION'); g(2, 'TABLES');
    g(0, 'TABLE'); g(2, 'LTYPE'); g(70, 1); g(0, 'LTYPE'); g(2, 'CONTINUOUS'); g(70, 0); g(3, 'Solid line'); g(72, 65); g(73, 0); g(40, 0); g(0, 'ENDTAB');
    g(0, 'TABLE'); g(2, 'LAYER'); g(70, 3);
    [['CONTOUR', 7], ['PERCAGES', 1], ['COMPOSANTS', 8]].forEach(function (q) { g(0, 'LAYER'); g(2, q[0]); g(70, 0); g(62, q[1]); g(6, 'CONTINUOUS'); });
    g(0, 'ENDTAB'); g(0, 'ENDSEC');
    g(0, 'SECTION'); g(2, 'ENTITIES');
    const r = c.r, W = c.W, Lg = c.L;
    if (r > 1e-6) {
      ligne('CONTOUR', r, 0, Lg - r, 0); arc('CONTOUR', Lg - r, r, r, 270, 360);
      ligne('CONTOUR', Lg, r, Lg, W - r); arc('CONTOUR', Lg - r, W - r, r, 0, 90);
      ligne('CONTOUR', Lg - r, W, r, W); arc('CONTOUR', r, W - r, r, 90, 180);
      ligne('CONTOUR', 0, W - r, 0, r); arc('CONTOUR', r, r, r, 180, 270);
    } else {
      ligne('CONTOUR', 0, 0, Lg, 0); ligne('CONTOUR', Lg, 0, Lg, W); ligne('CONTOUR', Lg, W, 0, W); ligne('CONTOUR', 0, W, 0, 0);
    }
    for (const t of d.trous) cerc('PERCAGES', t.x - ox, t.y - oy, d.vis.trou / 2);
    for (const k of d.comps) {
      const x1 = k.x1 - ox, x2 = k.x2 - ox, y1 = k.y1 - oy, y2 = k.y2 - oy;
      ligne('COMPOSANTS', x1, y1, x2, y1); ligne('COMPOSANTS', x2, y1, x2, y2); ligne('COMPOSANTS', x2, y2, x1, y2); ligne('COMPOSANTS', x1, y2, x1, y1);
    }
    g(0, 'ENDSEC'); g(0, 'EOF');
    return L.join('\r\n') + '\r\n';
  }

  // ---------- Plan coté de la carte (SVG à l'échelle 1:1) ----------
  function echapXml(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }
  function svgPlan(p, d, date) {
    const c = p.carte, ox = c.x0 - c.L / 2, oy = c.y0 - c.W / 2;
    const mg = 12, cote = 14, tabW = 64, ligneT = 5;
    const X0 = mg + cote, Y0 = mg + 4;
    const tabX = X0 + c.L + 16;
    const hauteurTab = 7 + ligneT * (d.trous.length + 1);
    const hDessin = Math.max(c.W + cote, hauteurTab);
    const W = tabX + tabW + mg, cartY = Y0 + hDessin + 8, H = cartY + 22 + mg;
    const sx = function (x) { return X0 + (x - ox); }, sy = function (y) { return Y0 + c.W - (y - oy); };
    const o = [];
    o.push('<?xml version="1.0" encoding="UTF-8"?>');
    o.push('<svg xmlns="http://www.w3.org/2000/svg" width="' + fmt(W, 2).replace(',', '.') + 'mm" height="' + fmt(H, 2).replace(',', '.') + 'mm" viewBox="0 0 ' + W.toFixed(2) + ' ' + H.toFixed(2) + '" font-family="Archivo, Arial, sans-serif">');
    o.push('<defs><marker id="fl" viewBox="0 0 10 10" refX="10" refY="5" markerWidth="3" markerHeight="3" orient="auto-start-reverse" markerUnits="userSpaceOnUse"><path d="M0 1.5L10 5L0 8.5z" fill="#000"/></marker></defs>');
    o.push('<rect x="0" y="0" width="' + W.toFixed(2) + '" height="' + H.toFixed(2) + '" fill="#fff"/>');
    const r = c.r, x1 = sx(ox), x2 = sx(ox + c.L), y1 = sy(oy + c.W), y2 = sy(oy);
    o.push('<path d="M' + (x1 + r).toFixed(3) + ' ' + y1.toFixed(3) + 'H' + (x2 - r).toFixed(3) + (r > 0 ? 'A' + r + ' ' + r + ' 0 0 1 ' + x2.toFixed(3) + ' ' + (y1 + r).toFixed(3) : '') + 'V' + (y2 - r).toFixed(3) + (r > 0 ? 'A' + r + ' ' + r + ' 0 0 1 ' + (x2 - r).toFixed(3) + ' ' + y2.toFixed(3) : '') + 'H' + (x1 + r).toFixed(3) + (r > 0 ? 'A' + r + ' ' + r + ' 0 0 1 ' + x1.toFixed(3) + ' ' + (y2 - r).toFixed(3) : '') + 'V' + (y1 + r).toFixed(3) + (r > 0 ? 'A' + r + ' ' + r + ' 0 0 1 ' + (x1 + r).toFixed(3) + ' ' + y1.toFixed(3) : '') + 'Z" fill="none" stroke="#000" stroke-width="0.35"/>');
    for (const k of d.comps) {
      o.push('<rect x="' + sx(k.x1).toFixed(3) + '" y="' + sy(k.y2).toFixed(3) + '" width="' + (k.x2 - k.x1).toFixed(3) + '" height="' + (k.y2 - k.y1).toFixed(3) + '" fill="none" stroke="#8a8a8a" stroke-width="0.18"/>');
      o.push('<text x="' + sx(k.x).toFixed(3) + '" y="' + (sy(k.y) + 0.8).toFixed(3) + '" font-size="2.2" text-anchor="middle" fill="#6a6a6a">' + echapXml(k.ref) + '</text>');
    }
    const rt = d.vis.trou / 2;
    d.trous.forEach(function (t) {
      const X = sx(t.x), Y = sy(t.y);
      o.push('<circle cx="' + X.toFixed(3) + '" cy="' + Y.toFixed(3) + '" r="' + rt + '" fill="none" stroke="#000" stroke-width="0.25"/>');
      o.push('<path d="M' + (X - rt - 1.5).toFixed(3) + ' ' + Y.toFixed(3) + 'H' + (X + rt + 1.5).toFixed(3) + 'M' + X.toFixed(3) + ' ' + (Y - rt - 1.5).toFixed(3) + 'V' + (Y + rt + 1.5).toFixed(3) + '" stroke="#000" stroke-width="0.13" stroke-dasharray="2 0.6 0.3 0.6"/>');
      o.push('<text x="' + (X + rt + 0.8).toFixed(3) + '" y="' + (Y - rt - 0.6).toFixed(3) + '" font-size="2.5">' + echapXml(t.ref) + '</text>');
    });
    const yc = y2 + 9;
    o.push('<path d="M' + x1.toFixed(3) + ' ' + (y2 + 1.5).toFixed(3) + 'V' + (yc + 1.5).toFixed(3) + 'M' + x2.toFixed(3) + ' ' + (y2 + 1.5).toFixed(3) + 'V' + (yc + 1.5).toFixed(3) + '" stroke="#000" stroke-width="0.18"/>');
    o.push('<path d="M' + x1.toFixed(3) + ' ' + yc.toFixed(3) + 'H' + x2.toFixed(3) + '" stroke="#000" stroke-width="0.18" marker-start="url(#fl)" marker-end="url(#fl)"/>');
    o.push('<text x="' + ((x1 + x2) / 2).toFixed(3) + '" y="' + (yc - 1.2).toFixed(3) + '" font-size="3.5" text-anchor="middle">' + fmt(c.L, 2) + '</text>');
    const xc = x1 - 9;
    o.push('<path d="M' + (x1 - 1.5).toFixed(3) + ' ' + y1.toFixed(3) + 'H' + (xc - 1.5).toFixed(3) + 'M' + (x1 - 1.5).toFixed(3) + ' ' + y2.toFixed(3) + 'H' + (xc - 1.5).toFixed(3) + '" stroke="#000" stroke-width="0.18"/>');
    o.push('<path d="M' + xc.toFixed(3) + ' ' + y2.toFixed(3) + 'V' + y1.toFixed(3) + '" stroke="#000" stroke-width="0.18" marker-start="url(#fl)" marker-end="url(#fl)"/>');
    o.push('<text transform="translate(' + (xc - 1.2).toFixed(3) + ' ' + ((y1 + y2) / 2).toFixed(3) + ') rotate(-90)" font-size="3.5" text-anchor="middle">' + fmt(c.W, 2) + '</text>');
    o.push('<text x="' + x1.toFixed(3) + '" y="' + (y2 + 13.5).toFixed(3) + '" font-size="2.2" fill="#444">Origine des cotes : coin inférieur gauche de la carte. Rayon des angles ' + fmt(c.r, 2) + ' mm.</text>');
    let ty = Y0 + 4;
    o.push('<text x="' + tabX + '" y="' + ty.toFixed(2) + '" font-size="3" font-weight="600">Perçages, ' + echapXml(p.boitier.vis) + ' (Ø ' + fmt(d.vis.trou, 2) + ' mm)</text>');
    ty += 3;
    const col = [tabX, tabX + 14, tabX + 34, tabX + 54];
    o.push('<rect x="' + tabX + '" y="' + ty.toFixed(2) + '" width="' + tabW + '" height="' + (ligneT * (d.trous.length + 1)).toFixed(2) + '" fill="none" stroke="#000" stroke-width="0.25"/>');
    const entetes = ['Trou', 'X', 'Y', 'Ø'];
    entetes.forEach(function (e, i) { o.push('<text x="' + (col[i] + 1.5) + '" y="' + (ty + 3.5).toFixed(2) + '" font-size="2.5" font-weight="600">' + e + '</text>'); });
    d.trous.forEach(function (t, i) {
      const yy = ty + ligneT * (i + 1);
      o.push('<path d="M' + tabX + ' ' + yy.toFixed(2) + 'H' + (tabX + tabW) + '" stroke="#000" stroke-width="0.13"/>');
      const vals = [t.ref, fmt(t.x - ox, 2), fmt(t.y - oy, 2), fmt(d.vis.trou, 2)];
      vals.forEach(function (v, j) { o.push('<text x="' + (col[j] + 1.5) + '" y="' + (yy + 3.5).toFixed(2) + '" font-size="2.5">' + echapXml(v) + '</text>'); });
    });
    const cw = W - 2 * mg, cx0 = mg;
    o.push('<rect x="' + cx0 + '" y="' + cartY.toFixed(2) + '" width="' + cw.toFixed(2) + '" height="22" fill="none" stroke="#000" stroke-width="0.35"/>');
    const cellules = [['Projet', p.nom], ['Document', 'Plan de la carte'], ['Échelle', '1:1'], ['Unités', 'mm'], ['Date', date || '']];
    const larg = cw / cellules.length;
    cellules.forEach(function (q, i) {
      const xx = cx0 + i * larg;
      if (i) o.push('<path d="M' + xx.toFixed(2) + ' ' + cartY.toFixed(2) + 'v22" stroke="#000" stroke-width="0.25"/>');
      o.push('<text x="' + (xx + 2).toFixed(2) + '" y="' + (cartY + 6).toFixed(2) + '" font-size="2.2" fill="#555">' + echapXml(q[0]) + '</text>');
      o.push('<text x="' + (xx + 2).toFixed(2) + '" y="' + (cartY + 14).toFixed(2) + '" font-size="3.5">' + echapXml(String(q[1]).slice(0, Math.max(6, Math.floor(larg / 2)))) + '</text>');
    });
    o.push('<text x="' + (W - mg).toFixed(2) + '" y="' + (H - 3).toFixed(2) + '" font-size="2" text-anchor="end" fill="#777">Prométhée, jalon 0</text>');
    o.push('</svg>');
    return o.join('\n');
  }

  // ---------- Archive ZIP (méthode stockée) ----------
  let TABLE_CRC = null;
  function crc32(u8) {
    if (!TABLE_CRC) {
      TABLE_CRC = new Uint32Array(256);
      for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; TABLE_CRC[n] = c >>> 0; }
    }
    let c = 0xFFFFFFFF;
    for (let i = 0; i < u8.length; i++) c = TABLE_CRC[(c ^ u8[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }
  function zip(fichiers, quand) {
    const enc = new TextEncoder(), dt = quand || new Date();
    const heure = (dt.getHours() << 11) | (dt.getMinutes() << 5) | Math.floor(dt.getSeconds() / 2);
    const jour = ((Math.max(1980, dt.getFullYear()) - 1980) << 9) | ((dt.getMonth() + 1) << 5) | dt.getDate();
    const locaux = [], central = []; let decal = 0;
    for (const f of fichiers) {
      const nom = enc.encode(f.nom), data = typeof f.donnees === 'string' ? enc.encode(f.donnees) : f.donnees, crc = crc32(data);
      const lh = new DataView(new ArrayBuffer(30));
      lh.setUint32(0, 0x04034b50, true); lh.setUint16(4, 20, true); lh.setUint16(6, 0x0800, true); lh.setUint16(8, 0, true);
      lh.setUint16(10, heure, true); lh.setUint16(12, jour, true); lh.setUint32(14, crc, true);
      lh.setUint32(18, data.length, true); lh.setUint32(22, data.length, true); lh.setUint16(26, nom.length, true); lh.setUint16(28, 0, true);
      locaux.push(new Uint8Array(lh.buffer), nom, data);
      const ch = new DataView(new ArrayBuffer(46));
      ch.setUint32(0, 0x02014b50, true); ch.setUint16(4, 20, true); ch.setUint16(6, 20, true); ch.setUint16(8, 0x0800, true); ch.setUint16(10, 0, true);
      ch.setUint16(12, heure, true); ch.setUint16(14, jour, true); ch.setUint32(16, crc, true);
      ch.setUint32(20, data.length, true); ch.setUint32(24, data.length, true); ch.setUint16(28, nom.length, true);
      ch.setUint16(30, 0, true); ch.setUint16(32, 0, true); ch.setUint16(34, 0, true); ch.setUint16(36, 0, true); ch.setUint32(38, 0, true); ch.setUint32(42, decal, true);
      central.push(new Uint8Array(ch.buffer), nom);
      decal += 30 + nom.length + data.length;
    }
    const tailleCentral = central.reduce(function (s, a) { return s + a.length; }, 0);
    const fin = new DataView(new ArrayBuffer(22));
    fin.setUint32(0, 0x06054b50, true); fin.setUint16(8, fichiers.length, true); fin.setUint16(10, fichiers.length, true);
    fin.setUint32(12, tailleCentral, true); fin.setUint32(16, decal, true); fin.setUint16(20, 0, true);
    const morceaux = locaux.concat(central, [new Uint8Array(fin.buffer)]);
    const total = morceaux.reduce(function (s, a) { return s + a.length; }, 0), out = new Uint8Array(total);
    let o = 0; for (const a of morceaux) { out.set(a, o); o += a.length; }
    return out;
  }

  function lisezmoi(p, d, stats, pb) {
    const b = p.boitier, err = pb.filter(function (q) { return q.sev === 'erreur'; });
    return [
      'Dossier de fabrication : ' + p.nom,
      'Généré par Prométhée, jalon 0, le ' + new Date().toLocaleDateString('fr-FR') + '.',
      '',
      err.length ? 'ATTENTION : ' + err.length + ' erreur(s) restaient au moment de l\u2019export :' : 'Aucune erreur détectée au moment de l\u2019export.',
      err.map(function (q) { return '  - ' + q.msg; }).join('\r\n'),
      '',
      'Contenu',
      '  boitier.stl         Boîtier prêt à imprimer, fond sur le plateau (' + fmt(stats.volCorps / 1000) + ' cm³).',
      '  couvercle.stl       Couvercle retourné, face supérieure sur le plateau (' + fmt(stats.volCouvercle / 1000) + ' cm³).',
      '  carte_contour.dxf   Contour, perçages et empreintes de la carte, en mm, origine au coin inférieur gauche.',
      '                      Dans KiCad : Fichier > Importer > Graphiques, couche Edge.Cuts.',
      '  plan_carte.svg      Plan coté à l\u2019échelle 1:1, avec le tableau des perçages.',
      '  nomenclature.csv    Nomenclature unique : électronique, pièces fabriquées et visserie.',
      '  projet.prom.json    Le projet, à rouvrir dans Prométhée (format ouvert).',
      '',
      'Impression conseillée',
      '  PLA ou PETG, couches de 0,2 mm, 3 périmètres, remplissage de 20 %, sans support.',
      '  Le haut des découpes forme un pont court qui s\u2019imprime sans support.',
      '',
      'Montage',
      '  1. Visser la carte sur les ' + p.trous.length + ' piliers avec des vis autotaraudeuses ' + b.vis + ' × ' + d.longVis + '.',
      '  2. Emboîter le couvercle : sa lèvre entre dans le boîtier avec un jeu de ' + fmt(LEVRE.jeu, 2) + ' mm.',
      ''
    ].join('\r\n');
  }

  function dossier(p, THREE, date) {
    const d = deriver(p), pb = verifier(p, d);
    const corps = construireCorps(p, d, THREE, { seg: 12, segCercle: 48 });
    const couv = construireCouvercle(p, d, THREE, { seg: 12, segCercle: 48 });
    const stats = { volCorps: volume(corps), volCouvercle: volume(couv) };
    const lignes = nomenclature(p, d, stats);
    const fichiers = [
      { nom: 'LISEZMOI.txt', donnees: lisezmoi(p, d, stats, pb) },
      { nom: 'boitier.stl', donnees: stl(pourImpression(corps, false, d.cx, d.cy), 'boitier') },
      { nom: 'couvercle.stl', donnees: stl(pourImpression(couv, true, d.cx, d.cy), 'couvercle') },
      { nom: 'carte_contour.dxf', donnees: dxf(p, d) },
      { nom: 'plan_carte.svg', donnees: svgPlan(p, d, date || new Date().toLocaleDateString('fr-FR')) },
      { nom: 'nomenclature.csv', donnees: csv(lignes) },
      { nom: 'projet.prom.json', donnees: JSON.stringify(p, null, 2) }
    ];
    return { fichiers: fichiers, stats: stats, problemes: pb };
  }

  const API = {
    VIS: VIS, LEVRE: LEVRE, TYPES: TYPES, EPAISSEURS_PCB: EPAISSEURS_PCB, PAROI_PILIER: PAROI_PILIER,
    nombre: nombre, borne: borne, fmt: fmt, lireNombre: lireNombre, sdRR: sdRR, distRectPoint: distRectPoint,
    normaliser: normaliser, exemple: exemple, projetVide: projetVide, prochaineRef: prochaineRef,
    posTrou: posTrou, ancrerTrou: ancrerTrou, placerSurBord: placerSurBord, deriver: deriver, dansLevre: dansLevre,
    hauteurIdeale: hauteurIdeale, verifier: verifier, corriger: corriger, repousserTrou: repousserTrou,
    ajouterComposant: ajouterComposant, ajouterTrou: ajouterTrou,
    contourArrondi: contourArrondi, profilArrondi: profilArrondi, cercle: cercle,
    decoupesValides: decoupesValides, piliersValides: piliersValides, percagesValides: percagesValides,
    construireCorps: construireCorps, construireCouvercle: construireCouvercle, volume: volume,
    pourImpression: pourImpression, stl: stl, nomenclature: nomenclature, csv: csv, dxf: dxf, svgPlan: svgPlan,
    crc32: crc32, zip: zip, dossier: dossier
  };
  if (typeof module !== 'undefined' && module.exports) module.exports = API;
  else racine.Promethee = API;
})(typeof window !== 'undefined' ? window : globalThis);
