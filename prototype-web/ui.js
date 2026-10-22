/* Prométhée, jalon 0 : interface (vue Carte 2D, vue Boîtier 3D, panneau). Licence GPL-3.0-or-later. */
(function () {
  'use strict';
  const P = window.Promethee;
  const T3 = window.THREE || null;
  const doc = document;
  const $ = function (s) { return doc.querySelector(s); };
  const fmt = P.fmt;
  const CLE = 'promethee.jalon0.projet', CLE_ASTUCE = 'promethee.jalon0.astuce';
  const POLICE = '"Archivo", "Helvetica Neue", Arial, sans-serif';
  const mobile = window.matchMedia ? window.matchMedia('(max-width: 719px)') : { matches: false };

  function el(tag, cls, texte) {
    const e = doc.createElement(tag);
    if (cls) e.className = cls;
    if (texte !== undefined && texte !== null) e.textContent = texte;
    return e;
  }
  function borne(v, a, b) { return Math.min(b, Math.max(a, v)); }

  const E = {
    p: null, d: null, pb: [], sel: null, survol: null, annuler: [], retablir: [],
    vue: 'deux', couvercle: 'souleve', transparence: false, magnet: true, cotes: true, onglet: 'proprietes'
  };

  // ---------- Stockage local ----------
  function lireStockage(cle) { try { return window.localStorage.getItem(cle); } catch (e) { return null; } }
  function ecrireStockage(cle, v) { try { window.localStorage.setItem(cle, v); } catch (e) { /* stockage indisponible */ } }
  function projetInitial() {
    const s = lireStockage(CLE);
    if (s) { try { return P.normaliser(JSON.parse(s)); } catch (e) { /* projet illisible : exemple */ } }
    return P.exemple();
  }
  let minuteurSauve = 0;
  function sauverBientot() { clearTimeout(minuteurSauve); minuteurSauve = setTimeout(function () { ecrireStockage(CLE, JSON.stringify(E.p)); }, 400); }

  // ---------- Historique ----------
  function instant() { return JSON.stringify(E.p); }
  function apresHisto() {
    $('#b-annuler').disabled = !E.annuler.length;
    $('#b-retablir').disabled = !E.retablir.length;
    sauverBientot();
  }
  function engager(avant) {
    if (avant === instant()) return false;
    E.annuler.push(avant); if (E.annuler.length > 200) E.annuler.shift();
    E.retablir.length = 0; apresHisto(); return true;
  }
  function modifier(fn) { const avant = instant(); fn(E.p); changer(); engager(avant); }
  function remplacer(p, message) {
    const avant = instant();
    E.p = p; E.sel = null; E.survol = null;
    changer({ panneau: true }); engager(avant); recadrerTout();
    if (message) toast(message);
  }
  function annuler() {
    if (!E.annuler.length) return;
    E.retablir.push(instant()); E.p = P.normaliser(JSON.parse(E.annuler.pop()));
    changer({ panneau: true }); apresHisto();
  }
  function retablir() {
    if (!E.retablir.length) return;
    E.annuler.push(instant()); E.p = P.normaliser(JSON.parse(E.retablir.pop()));
    changer({ panneau: true }); apresHisto();
  }

  // ---------- Accès au modèle ----------
  function compModele(id) { return E.p.composants.find(function (k) { return k.id === id; }) || null; }
  function trouModele(id) { return E.p.trous.find(function (t) { return t.id === id; }) || null; }
  function geoComp(id) { return E.d.comps.find(function (g) { return g.id === id; }) || null; }
  function geoTrou(id) { return E.d.trous.find(function (t) { return t.id === id; }) || null; }
  function objet(c) {
    if (!c) return null;
    if (c.type === 'comp') return compModele(c.id);
    if (c.type === 'trou') return trouModele(c.id);
    if (c.type === 'carte') return E.p.carte;
    return null;
  }
  function cibleDe(id) {
    if (compModele(id)) return { type: 'comp', id: id };
    if (trouModele(id)) return { type: 'trou', id: id };
    return null;
  }
  function memeCible(a, b) { return !!a && !!b && a.type === b.type && a.id === b.id; }
  function estFocus(id) { return (E.sel && E.sel.id === id) || (E.survol && E.survol.id === id); }
  function origine() { const c = E.p.carte; return { x: c.x0 - c.L / 2, y: c.y0 - c.W / 2 }; }
  function positionObjet(c) {
    if (c.type === 'comp') { const g = geoComp(c.id); return g ? { x: g.x, y: g.y } : { x: 0, y: 0 }; }
    const t = geoTrou(c.id); return t ? { x: t.x, y: t.y } : { x: 0, y: 0 };
  }
  function selectionner(c) {
    if (memeCible(c, E.sel) || (!c && !E.sel)) return;
    E.sel = c || null;
    planifier({ panneau: true, d2: true, surbrillance: true });
    majPied();
  }
  function survoler(c) {
    if (memeCible(c, E.survol) || (!c && !E.survol)) return;
    E.survol = c || null;
    planifier({ d2: true, surbrillance: true });
  }

  // ---------- Modifications partagées par les deux vues ----------
  function contraindreBords(p) {
    const c = p.carte;
    for (const k of p.composants) {
      if (!k.bord) continue;
      const hz = k.bord === 'N' || k.bord === 'S', centre = hz ? c.x0 : c.y0, demi = (hz ? c.L : c.W) / 2;
      const lim = Math.max(0, demi - k.w / 2 - c.r);
      k.le_long = borne(k.le_long, centre - lim, centre + lim);
    }
  }
  function bougerModele(c, x, y, fin) {
    if (E.magnet) { x = Math.round(x * 2) / 2; y = Math.round(y * 2) / 2; }
    if (c.type === 'comp') {
      const k = compModele(c.id); if (!k) return;
      if (P.TYPES[k.type].bord) P.placerSurBord(E.p, k, x, y);
      else { k.x = x; k.y = y; }
    } else if (c.type === 'trou') {
      const t = trouModele(c.id); if (!t) return;
      if (fin) P.ancrerTrou(E.p, t, x, y);
      else { delete t.coin; delete t.ox; delete t.oy; t.x = x; t.y = y; }
    }
  }
  function deplacerObjet(c, x, y, fin) { bougerModele(c, x, y, fin); changer(); }
  function redimensionner(bord, delta, dep) {
    const c = E.p.carte, hz = bord === 'E' || bord === 'W';
    const depart = hz ? dep.L : dep.W;
    let n = depart + delta;
    if (E.magnet) n = Math.round(n * 2) / 2;
    n = borne(n, 10, 300);
    const dl = n - depart;
    if (hz) { c.L = n; c.x0 = dep.x0 + (bord === 'E' ? dl / 2 : -dl / 2); c.y0 = dep.y0; }
    else { c.W = n; c.y0 = dep.y0 + (bord === 'N' ? dl / 2 : -dl / 2); c.x0 = dep.x0; }
    c.r = Math.min(dep.r, Math.min(c.L, c.W) / 2 - 0.5);
    contraindreBords(E.p);
    changer();
  }
  function pivoter(id) {
    const k = compModele(id); if (!k) return;
    if (P.TYPES[k.type].bord) { toast('Un connecteur de bord s’oriente seul vers la paroi : fais-le glisser vers un autre bord.'); return; }
    modifier(function () { k.rot = (k.rot + 90) % 360; });
  }
  function supprimer(c) {
    if (!c || c.type === 'carte') return;
    const o = objet(c); if (!o) return;
    const ref = o.ref;
    modifier(function (p) {
      if (c.type === 'comp') p.composants = p.composants.filter(function (k) { return k.id !== c.id; });
      else p.trous = p.trous.filter(function (t) { return t.id !== c.id; });
    });
    E.sel = null; planifier({ panneau: true, d2: true, surbrillance: true });
    toast(ref + ' supprimé. Annuler pour le récupérer.');
  }
  function dupliquer(id) {
    const k = compModele(id); if (!k) return;
    let nouveau = null;
    modifier(function (p) {
      const ids = new Set(p.composants.map(function (q) { return q.id; }).concat(p.trous.map(function (q) { return q.id; })));
      nouveau = JSON.parse(JSON.stringify(k));
      do { nouveau.id = 'c' + Math.random().toString(36).slice(2, 8); } while (ids.has(nouveau.id));
      nouveau.ref = P.prochaineRef(p, P.TYPES[k.type].prefixe);
      if (nouveau.bord) nouveau.le_long += k.w + 3; else { nouveau.x += 5; nouveau.y -= 5; }
      p.composants.push(nouveau);
      contraindreBords(p);
    });
    if (nouveau) selectionner({ type: 'comp', id: nouveau.id });
  }
  function ajouter(type) {
    let cible = null;
    modifier(function (p) {
      if (type === 'trou') { const t = P.ajouterTrou(p); cible = { type: 'trou', id: t.id }; }
      else { const k = P.ajouterComposant(p, type); cible = { type: 'comp', id: k.id }; }
    });
    E.sel = null; selectionner(cible);
    const o = objet(cible);
    if (o) toast(o.ref + ' ajouté' + (E.pb.some(function (q) { return q.sev === 'erreur' && q.ids.indexOf(cible.id) >= 0; }) ? ' : il manque de place, déplace-le.' : '.'));
  }

  // ---------- Boucle de rafraîchissement ----------
  let aFaire = {}, idImage = 0;
  function planifier(q) {
    for (const k in q) if (q[k]) aFaire[k] = true;
    if (!idImage) idImage = requestAnimationFrame(executer);
  }
  function etape(fn) { try { fn(); } catch (e) { if (window.console) console.error(e); } }
  function executer() {
    idImage = 0;
    const f = aFaire; aFaire = {};
    if (f.d2) etape(dessiner2D);
    if (f.d3) etape(maj3D); else if (f.surbrillance) etape(surbrillance3D);
    if (f.panneau) etape(construirePanneau); else if (f.sync) etape(syncChamps);
    if (f.verifs) etape(majVerifs);
    if (f.nomen) etape(majNomenclature);
    if (!g2 && !g3) etape(garderEnVue);
  }
  function changer(q) {
    E.d = P.deriver(E.p);
    E.pb = P.verifier(E.p, E.d);
    if (E.sel && !objet(E.sel)) { E.sel = null; q = Object.assign({}, q, { panneau: true }); }
    if (E.survol && !objet(E.survol)) E.survol = null;
    planifier(Object.assign({ d2: true, d3: true, sync: true, verifs: true, nomen: true }, q || {}));
  }

  // ---------- Messages ----------
  let minuteurToast = 0;
  function toast(msg) {
    const t = $('#toast'); t.textContent = msg; t.classList.add('visible');
    clearTimeout(minuteurToast); minuteurToast = setTimeout(function () { t.classList.remove('visible'); }, 3400);
  }
  function ouvrirModale(titre, paragraphes, boutons) {
    return new Promise(function (resoudre) {
      const m = $('#modale'), b = $('#boite');
      const retour = doc.activeElement;
      b.textContent = '';
      const h = el('h2', null, titre); h.id = 'modale-titre'; b.appendChild(h);
      for (const t of paragraphes) b.appendChild(el('p', null, t));
      const r = el('div', 'rangee');
      function clavier(e) { if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); fermer(false); } }
      function fermer(v) {
        m.hidden = true; doc.removeEventListener('keydown', clavier, true); m.onclick = null;
        if (retour && retour.focus) retour.focus();
        resoudre(v);
      }
      boutons.forEach(function (o) {
        const bt = el('button', 'btn' + (o.primaire ? ' primaire' : ''), o.texte);
        bt.addEventListener('click', function () { fermer(o.valeur); });
        r.appendChild(bt);
      });
      b.appendChild(r);
      doc.addEventListener('keydown', clavier, true);
      m.onclick = function (e) { if (e.target === m) fermer(false); };
      m.hidden = false;
      const dernier = r.lastElementChild; if (dernier) dernier.focus();
    });
  }

  // ---------- Téléchargements ----------
  let promesseTelech = null;
  function obtenirTelech() {
    if (!promesseTelech) {
      promesseTelech = (async function () {
        try { if (window.claude && typeof window.claude.use === 'function') return await window.claude.use('downloads'); } catch (e) { /* indisponible */ }
        return null;
      })();
    }
    return promesseTelech;
  }
  async function enregistrer(nom, data, msgOk) {
    const t = await obtenirTelech();
    if (!t) { toast('L’enregistrement de fichiers n’est pas disponible dans cette vue.'); return false; }
    try {
      const r = await t.save({ filename: nom, data: data });
      if (!r || r.status === 'saved') toast(msgOk);
      return true;
    } catch (err) {
      const code = err && err.code;
      if (code === 'declined') return false;
      if (code === 'rate_limited') toast('Une demande d’enregistrement est déjà ouverte. Réessaie dans un instant.');
      else if (code === 'extension_not_enabled' || code === 'rejected_extension') toast('Le format .' + nom.split('.').pop() + ' n’est pas disponible au téléchargement ici.');
      else if (code === 'too_large') toast('Le fichier est trop lourd pour cette destination.');
      else if (code === 'bad_request' || code === 'transform_error') toast('Enregistrement impossible : ' + (err.message || 'requête refusée') + '.');
      else { afficherExports(false); toast('L’enregistrement de fichiers n’est pas disponible dans cette vue.'); }
      return false;
    }
  }
  function afficherExports(oui) {
    window.__exportPossible = oui;
    $('#b-dossier').hidden = !oui;
    doc.querySelectorAll('[data-export]').forEach(function (b) { b.hidden = !oui; });
    if (E.onglet === 'nomenclature') majNomenclature(); else nomenSale = true;
  }
  function nomFichier(s) {
    return (s || 'projet').normalize('NFD').replace(/[\u0300-\u036f]/g, '').replace(/[^a-zA-Z0-9]+/g, '-').replace(/^-+|-+$/g, '').toLowerCase().slice(0, 40) || 'projet';
  }
  async function exporterDossier() {
    const err = E.pb.filter(function (q) { return q.sev === 'erreur'; }).length;
    if (err) {
      const ok = await ouvrirModale(err > 1 ? 'Le projet contient ' + err + ' erreurs' : 'Le projet contient une erreur',
        ['Le dossier sera généré tel quel, avec la liste des erreurs dans le fichier LISEZMOI. Corrige-les d’abord pour ne pas imprimer une pièce inutilisable.'],
        [{ texte: 'Voir les erreurs', valeur: false }, { texte: 'Générer quand même', valeur: true, primaire: true }]);
      if (!ok) { ouvrirOnglet('verifs', true); return; }
    }
    let dz;
    try { dz = P.dossier(E.p, T3, new Date().toLocaleDateString('fr-FR')); }
    catch (e) { toast('La génération du dossier a échoué : ' + (e && e.message ? e.message : 'erreur inconnue') + '.'); return; }
    const blob = new Blob([P.zip(dz.fichiers)], { type: 'application/zip' });
    await enregistrer(nomFichier(E.p.nom) + '-fabrication.zip', blob, 'Dossier de fabrication enregistré.');
  }
  async function exporterProjet() {
    await enregistrer(nomFichier(E.p.nom) + '.prom.json', JSON.stringify(E.p, null, 2), 'Projet enregistré.');
  }
  async function exporterNomenclature() {
    await enregistrer(nomFichier(E.p.nom) + '-nomenclature.csv', P.csv(P.nomenclature(E.p, E.d, statistiques())), 'Nomenclature enregistrée.');
  }
  function ouvrirProjet() { const f = $('#fichier'); f.value = ''; f.click(); }
  function lireFichier(fichier) {
    if (!fichier) return;
    const lecteur = new FileReader();
    lecteur.onload = function () {
      try {
        const o = JSON.parse(String(lecteur.result));
        if (!o || typeof o !== 'object' || (o.format !== 'promethee-projet' && !o.carte)) throw new Error('format');
        remplacer(P.normaliser(o), 'Projet ouvert : ' + (o.nom || fichier.name) + '.');
      } catch (e) { toast('Ce fichier n’est pas un projet Prométhée lisible.'); }
    };
    lecteur.onerror = function () { toast('Lecture du fichier impossible.'); };
    lecteur.readAsText(fichier);
  }

  // =====================================================================
  // Vue Carte (2D)
  // =====================================================================
  const cv = $('#c2d');
  const ctx = cv.getContext ? cv.getContext('2d') : null;
  const V = { s: 6, ox: 0, oy: 0, w: 0, h: 0, dpr: 1, pret: false };
  let tok = {};
  function lireJetons() {
    const cs = getComputedStyle(doc.documentElement);
    ['plan', 'grille', 'grille-forte', 'pla-paroi', 'pla-fond', 'trait', 'masque', 'masque-bord', 'serigraphie', 'cuivre', 'cuivre-doux',
     'erreur', 'alerte', 'ok', 'encre', 'encre-2', 'encre-3', 'surface', 'filet', 'cote-carte'].forEach(function (n) { tok[n] = cs.getPropertyValue('--' + n).trim() || '#888888'; });
  }
  function sx(x) { return V.ox + x * V.s; }
  function sy(y) { return V.oy - y * V.s; }
  function mx(X) { return (X - V.ox) / V.s; }
  function my(Y) { return (V.oy - Y) / V.s; }
  function posLocale(e, cible) { const r = cible.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; }

  function taille2D() {
    const r = cv.getBoundingClientRect();
    const w = Math.max(1, Math.round(r.width)), h = Math.max(1, Math.round(r.height));
    const dpr = Math.min(window.devicePixelRatio || 1, 3);
    if (w === V.w && h === V.h && dpr === V.dpr) return;
    const premier = !V.pret || V.w < 4 || V.h < 4;
    const cxm = V.w ? mx(V.w / 2) : 0, cym = V.h ? my(V.h / 2) : 0;
    V.w = w; V.h = h; V.dpr = dpr;
    cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
    V.pret = w > 4 && h > 4;
    if (premier) recadrer2D(); else { V.ox = w / 2 - cxm * V.s; V.oy = h / 2 + cym * V.s; }
    planifier({ d2: true });
  }
  function recadrer2D() {
    if (!E.d || V.w < 4 || V.h < 4) return;
    const d = E.d, haut = 50, bas = 30, cote = E.cotes ? 24 : 4;
    const lw = d.Lo + cote, lh = d.Wo + cote;
    V.s = borne(Math.min((V.w - 28) / lw, (V.h - haut - bas) / lh), 0.4, 80);
    const decal = E.cotes ? 10 : 0;
    V.ox = V.w / 2 - (d.cx - decal / 2) * V.s;
    V.oy = (haut + V.h - bas) / 2 + (d.cy - decal / 2) * V.s;
    planifier({ d2: true });
  }
  function zoom2D(X, Y, f) {
    const wx = mx(X), wy = my(Y);
    V.s = borne(V.s * f, 0.4, 80);
    V.ox = X - wx * V.s; V.oy = Y + wy * V.s;
    planifier({ d2: true });
  }

  function chemRR(cx, cy, hx, hy, r) {
    const x0 = sx(cx - hx), x1 = sx(cx + hx), y0 = sy(cy + hy), y1 = sy(cy - hy);
    const R = Math.max(0, Math.min(r * V.s, (x1 - x0) / 2, (y1 - y0) / 2));
    ctx.beginPath();
    ctx.moveTo(x0 + R, y0); ctx.lineTo(x1 - R, y0);
    if (R > 0) ctx.arc(x1 - R, y0 + R, R, -Math.PI / 2, 0);
    ctx.lineTo(x1, y1 - R);
    if (R > 0) ctx.arc(x1 - R, y1 - R, R, 0, Math.PI / 2);
    ctx.lineTo(x0 + R, y1);
    if (R > 0) ctx.arc(x0 + R, y1 - R, R, Math.PI / 2, Math.PI);
    ctx.lineTo(x0, y0 + R);
    if (R > 0) ctx.arc(x0 + R, y0 + R, R, Math.PI, 1.5 * Math.PI);
    ctx.closePath();
  }
  function cercle2D(x, y, r) { ctx.beginPath(); ctx.arc(sx(x), sy(y), Math.max(0.5, r * V.s), 0, Math.PI * 2); }
  function fleche(x, y, ang) {
    ctx.beginPath(); ctx.moveTo(x, y);
    ctx.lineTo(x - 7 * Math.cos(ang - 0.38), y - 7 * Math.sin(ang - 0.38));
    ctx.lineTo(x - 7 * Math.cos(ang + 0.38), y - 7 * Math.sin(ang + 0.38));
    ctx.closePath(); ctx.fill();
  }
  function ligneCote(X1, Y1, X2, Y2, texte, couleur, fond) {
    ctx.strokeStyle = couleur; ctx.fillStyle = couleur; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(X1, Y1); ctx.lineTo(X2, Y2); ctx.stroke();
    const a = Math.atan2(Y2 - Y1, X2 - X1);
    if (Math.hypot(X2 - X1, Y2 - Y1) > 18) { fleche(X2, Y2, a); fleche(X1, Y1, a + Math.PI); }
    ctx.save();
    ctx.translate((X1 + X2) / 2, (Y1 + Y2) / 2);
    let ang = a; if (ang > Math.PI / 2 || ang < -Math.PI / 2) ang += Math.PI;
    ctx.rotate(ang);
    ctx.font = '600 11.5px ' + POLICE; ctx.textAlign = 'center'; ctx.textBaseline = 'bottom';
    if (fond) { const w = ctx.measureText(texte).width + 8; ctx.fillStyle = fond; ctx.fillRect(-w / 2, -17, w, 15); ctx.fillStyle = couleur; }
    ctx.fillText(texte, 0, -4);
    ctx.restore();
  }

  function grille() {
    const pas = V.s >= 7 ? 1 : V.s >= 2.2 ? 5 : 10;
    const i0 = Math.floor(mx(0) / pas), i1 = Math.ceil(mx(V.w) / pas), j0 = Math.floor(my(V.h) / pas), j1 = Math.ceil(my(0) / pas);
    if (i1 - i0 > 2000 || j1 - j0 > 2000) return;
    ctx.lineWidth = 1;
    for (let passe = 0; passe < 2; passe++) {
      ctx.beginPath();
      for (let i = i0; i <= i1; i++) { const x = i * pas; if ((x % 10 === 0) !== (passe === 1)) continue; const X = Math.round(sx(x)) + 0.5; ctx.moveTo(X, 0); ctx.lineTo(X, V.h); }
      for (let j = j0; j <= j1; j++) { const y = j * pas; if ((y % 10 === 0) !== (passe === 1)) continue; const Y = Math.round(sy(y)) + 0.5; ctx.moveTo(0, Y); ctx.lineTo(V.w, Y); }
      ctx.strokeStyle = passe ? tok['grille-forte'] : tok.grille; ctx.stroke();
    }
  }

  const COUL_LED = [['rouge', '#D23A2C'], ['vert', '#2E9F58'], ['bleu', '#2F6BD0'], ['jaune', '#E2B22E'], ['orange', '#E07B26'], ['blanc', '#EEEEE6']];
  function couleurLed(v) { const s = (v || '').toLowerCase(); for (const q of COUL_LED) if (s.indexOf(q[0]) >= 0) return q[1]; return '#D23A2C'; }

  function dessinerComp(g) {
    const k = compModele(g.id); if (!k) return;
    const T = P.TYPES[k.type], s = V.s;
    const lx = (T.bord ? k.d : k.w) * s, ly = (T.bord ? k.w : k.d) * s;
    ctx.save();
    ctx.translate(sx(g.x), sy(g.y)); ctx.rotate(-g.rot * Math.PI / 180);
    function rect(x, y, w, h, rempli, trait) {
      ctx.beginPath(); ctx.rect(x, -y - h, w, h);
      if (rempli) { ctx.fillStyle = rempli; ctx.fill(); }
      if (trait) { ctx.strokeStyle = trait; ctx.lineWidth = 1; ctx.stroke(); }
    }
    function disque(x, y, r, rempli, trait) {
      ctx.beginPath(); ctx.arc(x, -y, Math.max(0.5, r), 0, Math.PI * 2);
      if (rempli) { ctx.fillStyle = rempli; ctx.fill(); }
      if (trait) { ctx.strokeStyle = trait; ctx.lineWidth = 1; ctx.stroke(); }
    }
    switch (k.type) {
      case 'module': {
        rect(-lx / 2, -ly / 2, lx, ly, '#1E3A55', '#0F2235');
        const ls = ly * 0.72;
        rect(-lx / 2 + 0.7 * s, -ly / 2 + 0.5 * s, lx - 1.4 * s, ls, '#B8BFC5', '#8E979E');
        const y0 = -ly / 2 + 0.5 * s + ls + 0.9 * s, y1 = ly / 2 - 0.8 * s;
        if (y1 - y0 > 3) {
          ctx.beginPath(); ctx.strokeStyle = '#C98A45'; ctx.lineWidth = Math.max(1, 0.35 * s);
          const n = 5, xa = -lx / 2 + 1.5 * s, xb = lx / 2 - 1.5 * s;
          for (let i = 0; i <= n; i++) { const x = xa + (xb - xa) * i / n; ctx.lineTo(x, -(i % 2 ? y1 : y0)); ctx.lineTo(x + (xb - xa) / n * 0.5, -(i % 2 ? y1 : y0)); }
          ctx.stroke();
        }
        break;
      }
      case 'ic':
        rect(-lx / 2, -ly / 2, lx, ly, '#202325', '#0E1011');
        disque(-lx / 2 + 0.9 * s, ly / 2 - 0.9 * s, 0.35 * s, '#7A8086');
        break;
      case 'capteur':
        rect(-lx / 2, -ly / 2, lx, ly, '#2A2D30', '#111');
        rect(-lx * 0.4, -ly * 0.4, lx * 0.8, ly * 0.8, '#AEB4B9');
        disque(lx * 0.15, ly * 0.15, 0.12 * s, '#333');
        break;
      case 'usbc':
        rect(-lx / 2, -ly / 2, lx, ly, '#C2C7CB', '#80888F');
        rect(lx / 2 - 1.0 * s, -ly / 2 + 0.8 * s, 1.0 * s, ly - 1.6 * s, '#3B4045');
        for (let i = 0; i < 6; i++) rect(-lx / 2 - 0.6 * s, -ly / 2 + (1 + i * (k.w - 2) / 5) * s - 0.25 * s, 0.9 * s, 0.5 * s, '#C98A45');
        break;
      case 'jack':
        rect(-lx / 2, -ly / 2, lx, ly, '#26292B', '#0D0E0F');
        rect(lx / 2 - 1.4 * s, -ly * 0.3, 1.4 * s, ly * 0.6, '#4C5054');
        disque(lx / 2 - 3.2 * s, 0, 1 * s, '#6B7075');
        break;
      case 'jst':
        rect(-lx / 2, -ly / 2, lx, ly, '#E8DFC8', '#A89C7C');
        rect(-lx / 2 + 0.6 * s, -ly / 2 + 0.6 * s, lx - 1.2 * s, ly - 1.4 * s, '#D3C7A6');
        rect(-1.35 * s, -0.3 * s, 0.7 * s, 0.7 * s, '#C9A64A');
        rect(0.65 * s, -0.3 * s, 0.7 * s, 0.7 * s, '#C9A64A');
        break;
      case 'led': {
        const r = Math.min(lx, ly) / 2, coul = couleurLed(k.valeur);
        disque(0, 0, r, coul, 'rgba(0,0,0,.35)');
        ctx.beginPath(); ctx.moveTo(-r * 0.62, -r * 0.78); ctx.lineTo(-r * 0.62, r * 0.78); ctx.strokeStyle = 'rgba(0,0,0,.35)'; ctx.lineWidth = 1; ctx.stroke();
        disque(r * 0.25, r * 0.28, r * 0.25, 'rgba(255,255,255,.55)');
        break;
      }
      case 'bouton':
        rect(-lx / 2, -ly / 2, lx, ly, '#2B2D2F', '#111');
        [[-1, -1], [1, -1], [1, 1], [-1, 1]].forEach(function (q) { rect(q[0] * lx / 2 - (q[0] > 0 ? 0.2 * s : 0.8 * s), q[1] * (ly / 2 - 1.1 * s) - 0.4 * s, s, 0.8 * s, '#9AA0A5'); });
        disque(0, 0, 1.75 * s, '#6A6E72', '#8D9195');
        break;
      case 'condo': {
        const r = Math.min(lx, ly) / 2;
        disque(0, 0, r, '#22315A', '#121B33');
        ctx.save(); ctx.beginPath(); ctx.arc(0, 0, r, 0, Math.PI * 2); ctx.clip();
        ctx.fillStyle = '#A8B6D8'; ctx.fillRect(-r, -r, r * 0.42, 2 * r); ctx.restore();
        ctx.beginPath(); ctx.strokeStyle = 'rgba(220,226,232,.75)'; ctx.lineWidth = 1;
        ctx.moveTo(-r * 0.3, 0); ctx.lineTo(r * 0.55, 0); ctx.moveTo(r * 0.12, -r * 0.42); ctx.lineTo(r * 0.12, r * 0.42); ctx.stroke();
        break;
      }
      default:
        rect(-lx / 2, -ly / 2, lx, ly, '#444', '#222');
    }
    ctx.restore();
    etiquette(g);
  }
  function etiquette(g) {
    const taille = borne(1.7 * V.s, 9, 13);
    if (V.s < 2.2) return;
    ctx.font = '650 ' + taille + 'px ' + POLICE; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    const largeurTxt = ctx.measureText(g.ref).width;
    const wpx = (g.x2 - g.x1) * V.s, hpx = (g.y2 - g.y1) * V.s;
    const dedans = { module: '#2b3036', usbc: '#2b3036', jst: '#4b4430', ic: '#F4F1E6', jack: '#F4F1E6', bouton: null, condo: null, led: null, capteur: null }[g.type];
    if (dedans && wpx > largeurTxt + 6 && hpx > taille + 4) {
      let y = sy(g.y);
      if (g.type === 'module') { const k = compModele(g.id); if (k && (k.rot === 0 || k.rot === 180)) y = sy(g.y + (k.rot === 0 ? -1 : 1) * k.d * 0.1); }
      ctx.fillStyle = dedans; ctx.fillText(g.ref, sx(g.x), y);
    } else {
      ctx.fillStyle = tok.serigraphie; ctx.fillText(g.ref, sx(g.x), sy(g.y2) - taille * 0.75);
    }
  }
  function dessinerTrou(t) {
    const d = E.d;
    cercle2D(t.x, t.y, d.vis.tete / 2); ctx.fillStyle = '#C98A45'; ctx.fill();
    cercle2D(t.x, t.y, d.vis.trou / 2); ctx.fillStyle = tok['pla-paroi']; ctx.fill();
    cercle2D(t.x, t.y, d.rp); ctx.fillStyle = 'rgba(0,0,0,.42)'; ctx.fill();
    if (V.s >= 2.2) {
      const c = E.p.carte, dx = Math.sign(c.x0 - t.x) || 1, dy = Math.sign(c.y0 - t.y) || 1;
      ctx.font = '650 ' + borne(1.6 * V.s, 9, 12) + 'px ' + POLICE; ctx.textAlign = dx > 0 ? 'left' : 'right'; ctx.textBaseline = 'middle';
      ctx.fillStyle = tok.serigraphie;
      ctx.fillText(t.ref, sx(t.x + dx * (d.vis.tete / 2 + 0.8)), sy(t.y + dy * (d.vis.tete / 2 + 0.6)));
    }
  }
  function rectDecoupe(o) {
    const d = E.d;
    if (o.mur === 'N' || o.mur === 'S') {
      const s = o.mur === 'N' ? 1 : -1;
      return { x1: o.u - o.w / 2, x2: o.u + o.w / 2, y1: Math.min(d.cy + s * d.Wi / 2, d.cy + s * d.Wo / 2), y2: Math.max(d.cy + s * d.Wi / 2, d.cy + s * d.Wo / 2) };
    }
    const s = o.mur === 'E' ? 1 : -1;
    return { y1: o.u - o.w / 2, y2: o.u + o.w / 2, x1: Math.min(d.cx + s * d.Li / 2, d.cx + s * d.Lo / 2), x2: Math.max(d.cx + s * d.Li / 2, d.cx + s * d.Lo / 2) };
  }
  function dessinerDecoupe(g) {
    const r = rectDecoupe(g.decoupe), focus = estFocus(g.id);
    const X = sx(r.x1), Y = sy(r.y2), W = (r.x2 - r.x1) * V.s, H = (r.y2 - r.y1) * V.s;
    ctx.save();
    ctx.fillStyle = tok.cuivre; ctx.globalAlpha = focus ? 0.7 : 0.28; ctx.fillRect(X, Y, W, H);
    ctx.globalAlpha = 1; ctx.setLineDash([4, 3]); ctx.lineWidth = focus ? 1.6 : 1; ctx.strokeStyle = tok.cuivre; ctx.strokeRect(X + 0.5, Y + 0.5, W - 1, H - 1);
    ctx.restore();
  }
  function dessinerPercage(g) {
    const o = g.trouCouvercle, focus = estFocus(g.id);
    ctx.save(); ctx.setLineDash(focus ? [] : [3, 3]); ctx.lineWidth = focus ? 2 : 1.2; ctx.strokeStyle = tok.cuivre;
    cercle2D(o.x, o.y, o.d / 2); ctx.stroke(); ctx.restore();
  }
  function poigneesListe() {
    const c = E.p.carte;
    return [{ bord: 'E', x: c.x0 + c.L / 2, y: c.y0 }, { bord: 'W', x: c.x0 - c.L / 2, y: c.y0 }, { bord: 'N', x: c.x0, y: c.y0 + c.W / 2 }, { bord: 'S', x: c.x0, y: c.y0 - c.W / 2 }];
  }
  function dessinerCotes() {
    const d = E.d, c = E.p.carte;
    const yb = sy(c.y0 - c.W / 2), ybx = sy(d.cy - d.Wo / 2);
    const xl = sx(c.x0 - c.L / 2), xlx = sx(d.cx - d.Lo / 2);
    const Y1 = ybx + 18, Y2 = ybx + 38, X1 = xlx - 18, X2 = xlx - 38;
    ctx.save(); ctx.lineWidth = 1;
    ctx.strokeStyle = tok['encre-3'];
    ctx.beginPath();
    ctx.moveTo(sx(c.x0 - c.L / 2), yb + 3); ctx.lineTo(sx(c.x0 - c.L / 2), Y1 + 4); ctx.moveTo(sx(c.x0 + c.L / 2), yb + 3); ctx.lineTo(sx(c.x0 + c.L / 2), Y1 + 4);
    ctx.moveTo(sx(d.cx - d.Lo / 2), ybx + 3); ctx.lineTo(sx(d.cx - d.Lo / 2), Y2 + 4); ctx.moveTo(sx(d.cx + d.Lo / 2), ybx + 3); ctx.lineTo(sx(d.cx + d.Lo / 2), Y2 + 4);
    ctx.moveTo(xl - 3, sy(c.y0 - c.W / 2)); ctx.lineTo(X1 - 4, sy(c.y0 - c.W / 2)); ctx.moveTo(xl - 3, sy(c.y0 + c.W / 2)); ctx.lineTo(X1 - 4, sy(c.y0 + c.W / 2));
    ctx.moveTo(xlx - 3, sy(d.cy - d.Wo / 2)); ctx.lineTo(X2 - 4, sy(d.cy - d.Wo / 2)); ctx.moveTo(xlx - 3, sy(d.cy + d.Wo / 2)); ctx.lineTo(X2 - 4, sy(d.cy + d.Wo / 2));
    ctx.stroke();
    ligneCote(sx(c.x0 - c.L / 2), Y1, sx(c.x0 + c.L / 2), Y1, fmt(c.L, 2), tok['cote-carte']);
    ligneCote(sx(d.cx - d.Lo / 2), Y2, sx(d.cx + d.Lo / 2), Y2, fmt(d.Lo, 2), tok['encre-2']);
    ligneCote(X1, sy(c.y0 - c.W / 2), X1, sy(c.y0 + c.W / 2), fmt(c.W, 2), tok['cote-carte']);
    ligneCote(X2, sy(d.cy - d.Wo / 2), X2, sy(d.cy + d.Wo / 2), fmt(d.Wo, 2), tok['encre-2']);
    ctx.restore();
  }
  function cotesTrou(t) {
    const c = E.p.carte;
    const g = t.x - (c.x0 - c.L / 2), dr = c.x0 + c.L / 2 - t.x, b = t.y - (c.y0 - c.W / 2), h = c.y0 + c.W / 2 - t.y;
    ctx.save();
    if (g <= dr) ligneCote(sx(c.x0 - c.L / 2), sy(t.y), sx(t.x), sy(t.y), fmt(g, 2), tok.cuivre, tok.surface);
    else ligneCote(sx(t.x), sy(t.y), sx(c.x0 + c.L / 2), sy(t.y), fmt(dr, 2), tok.cuivre, tok.surface);
    if (b <= h) ligneCote(sx(t.x), sy(c.y0 - c.W / 2), sx(t.x), sy(t.y), fmt(b, 2), tok.cuivre, tok.surface);
    else ligneCote(sx(t.x), sy(t.y), sx(t.x), sy(c.y0 + c.W / 2), fmt(h, 2), tok.cuivre, tok.surface);
    ctx.restore();
  }
  function contourFocus(c, fort) {
    if (!c) return;
    ctx.save(); ctx.lineWidth = fort ? 2 : 1.5; ctx.strokeStyle = tok.cuivre; ctx.globalAlpha = fort ? 1 : 0.65;
    if (c.type === 'comp') {
      const g = geoComp(c.id);
      if (g) ctx.strokeRect(sx(g.x1) - 3, sy(g.y2) - 3, (g.x2 - g.x1) * V.s + 6, (g.y2 - g.y1) * V.s + 6);
    } else if (c.type === 'trou') {
      const t = geoTrou(c.id);
      if (t) { cercle2D(t.x, t.y, E.d.Rb + 2.5 / V.s); ctx.stroke(); }
    } else if (c.type === 'carte') {
      const k = E.p.carte; chemRR(k.x0, k.y0, k.L / 2 + 2.5 / V.s, k.W / 2 + 2.5 / V.s, k.r + 2.5 / V.s); ctx.stroke();
    }
    ctx.restore();
  }
  function dessiner2D() {
    if (!ctx || !V.pret || !E.d) return;
    const d = E.d, c = E.p.carte;
    ctx.setTransform(V.dpr, 0, 0, V.dpr, 0, 0);
    ctx.fillStyle = tok.plan; ctx.fillRect(0, 0, V.w, V.h);
    grille();
    chemRR(d.cx, d.cy, d.Lo / 2, d.Wo / 2, d.ro); ctx.fillStyle = tok['pla-paroi']; ctx.fill(); ctx.lineWidth = 1; ctx.strokeStyle = tok.trait; ctx.stroke();
    chemRR(d.cx, d.cy, d.Li / 2, d.Wi / 2, d.ri); ctx.fillStyle = tok['pla-fond']; ctx.fill(); ctx.stroke();
    for (const g of d.comps) if (g.decoupe) dessinerDecoupe(g);
    for (const t of d.trous) { cercle2D(t.x, t.y, d.Rb); ctx.fillStyle = tok['pla-paroi']; ctx.fill(); ctx.lineWidth = 1; ctx.strokeStyle = tok.trait; ctx.stroke(); }
    chemRR(c.x0, c.y0, c.L / 2, c.W / 2, c.r); ctx.fillStyle = tok.masque; ctx.fill(); ctx.lineWidth = 1.2; ctx.strokeStyle = tok['masque-bord']; ctx.stroke();
    ctx.save(); ctx.setLineDash([4, 3]); ctx.lineWidth = 1; ctx.strokeStyle = 'rgba(244,241,230,.5)';
    for (const t of d.trous) { cercle2D(t.x, t.y, d.Rb); ctx.stroke(); }
    ctx.restore();
    for (const g of d.comps) dessinerComp(g);
    for (const t of d.trous) dessinerTrou(t);
    for (const g of d.comps) if (g.trouCouvercle) dessinerPercage(g);
    const niveaux = {};
    for (const q of E.pb) for (const id of q.ids) if (niveaux[id] !== 'erreur') niveaux[id] = q.sev;
    ctx.save(); ctx.lineWidth = 1.6;
    for (const id in niveaux) {
      if (E.sel && E.sel.id === id) continue;
      ctx.strokeStyle = niveaux[id] === 'erreur' ? tok.erreur : tok.alerte;
      const g = geoComp(id);
      if (g) { ctx.strokeRect(sx(g.x1) - 2, sy(g.y2) - 2, (g.x2 - g.x1) * V.s + 4, (g.y2 - g.y1) * V.s + 4); continue; }
      const t = geoTrou(id);
      if (t) { cercle2D(t.x, t.y, d.Rb + 1.5 / V.s); ctx.stroke(); }
    }
    ctx.restore();
    if (E.cotes) dessinerCotes();
    if (E.survol && !memeCible(E.survol, E.sel)) contourFocus(E.survol, false);
    contourFocus(E.sel, true);
    if (E.sel && E.sel.type === 'trou') { const t = geoTrou(E.sel.id); if (t) cotesTrou(t); }
    if (E.sel && E.sel.type === 'carte') {
      const tactile = !window.matchMedia || window.matchMedia('(pointer: coarse)').matches;
      for (const h of poigneesListe()) {
        ctx.beginPath(); ctx.arc(sx(h.x), sy(h.y), tactile ? 9 : 6.5, 0, Math.PI * 2);
        ctx.fillStyle = tok.surface; ctx.fill(); ctx.lineWidth = 2; ctx.strokeStyle = tok.cuivre; ctx.stroke();
      }
    }
  }

  function toucher(xm, ym, tactile) {
    const tol = (tactile ? 14 : 6) / V.s, d = E.d, c = E.p.carte;
    if (E.sel && E.sel.type === 'carte') {
      for (const h of poigneesListe()) if (Math.hypot(h.x - xm, h.y - ym) <= tol * 1.2 + 3 / V.s) return { type: 'poignee', bord: h.bord };
    }
    for (let i = d.trous.length - 1; i >= 0; i--) {
      const t = d.trous[i];
      if (Math.hypot(t.x - xm, t.y - ym) <= Math.max(d.vis.tete / 2, 1.5) + tol * 0.5) return { type: 'trou', id: t.id };
    }
    let meilleur = null, aire = Infinity;
    for (const g of d.comps) {
      const m = tol * 0.5;
      if (xm >= g.x1 - m && xm <= g.x2 + m && ym >= g.y1 - m && ym <= g.y2 + m) {
        const a = (g.x2 - g.x1) * (g.y2 - g.y1);
        if (a < aire) { aire = a; meilleur = { type: 'comp', id: g.id }; }
      }
    }
    if (meilleur) return meilleur;
    if (P.sdRR(xm, ym, c.x0, c.y0, c.L / 2, c.W / 2, c.r) <= 0) return { type: 'carte', id: 'carte' };
    return null;
  }

  function majPied() {
    const pied = $('#pied2d'); if (!pied) return;
    const o = objet(E.sel);
    if (E.sel && o && E.sel.type !== 'carte') {
      const q = positionObjet(E.sel), or = origine();
      pied.textContent = o.ref + ' à X ' + fmt(q.x - or.x, 2) + ' mm, Y ' + fmt(q.y - or.y, 2) + ' mm';
    } else if (E.sel && E.sel.type === 'carte') {
      pied.textContent = 'Tire une poignée pour agrandir la carte d’un côté';
    } else pied.textContent = '';
  }

  const pts2 = new Map();
  let g2 = null, dernierTap = { t: 0, id: null };
  function brancher2D() {
    cv.addEventListener('pointerdown', function (e) {
      if (cv.setPointerCapture) { try { cv.setPointerCapture(e.pointerId); } catch (x) { /* ignore */ } }
      const q = posLocale(e, cv);
      pts2.set(e.pointerId, q);
      if (pts2.size === 2) {
        finirGeste2D(true);
        const v = Array.from(pts2.values()), a = v[0], b = v[1];
        g2 = { type: 'pince', d0: Math.hypot(a.x - b.x, a.y - b.y) || 1, s0: V.s, wx: mx((a.x + b.x) / 2), wy: my((a.y + b.y) / 2) };
        return;
      }
      if (pts2.size > 2) return;
      const xm = mx(q.x), ym = my(q.y);
      const panne = e.pointerType === 'mouse' && e.button > 0;
      const cible = panne ? null : toucher(xm, ym, e.pointerType !== 'mouse');
      if (cible && (cible.type === 'comp' || cible.type === 'trou')) {
        selectionner(cible);
        const pos = positionObjet(cible);
        g2 = { type: 'objet', cible: cible, x0: xm, y0: ym, px: q.x, py: q.y, ox: pos.x, oy: pos.y, avant: instant(), bouge: false };
      } else if (cible && cible.type === 'poignee') {
        g2 = { type: 'poignee', bord: cible.bord, x0: xm, y0: ym, px: q.x, py: q.y, dep: Object.assign({}, E.p.carte), avant: instant(), bouge: false };
      } else {
        g2 = { type: 'pan', px: q.x, py: q.y, ox: V.ox, oy: V.oy, cible: cible, bouge: false };
      }
      e.preventDefault();
    });
    cv.addEventListener('pointermove', function (e) {
      const q = posLocale(e, cv);
      if (!pts2.has(e.pointerId)) { if (e.pointerType === 'mouse') survol2D(q); return; }
      pts2.set(e.pointerId, q);
      if (!g2) return;
      if (g2.type === 'pince') {
        if (pts2.size < 2) return;
        const v = Array.from(pts2.values()), a = v[0], b = v[1];
        const s = borne(g2.s0 * (Math.hypot(a.x - b.x, a.y - b.y) || 1) / g2.d0, 0.4, 80);
        V.s = s; V.ox = (a.x + b.x) / 2 - g2.wx * s; V.oy = (a.y + b.y) / 2 + g2.wy * s;
        planifier({ d2: true }); return;
      }
      if (g2.type === 'rien') return;
      if (!g2.bouge && Math.hypot(q.x - g2.px, q.y - g2.py) < (e.pointerType === 'mouse' ? 3 : 7)) return;
      g2.bouge = true;
      if (g2.type === 'objet') { deplacerObjet(g2.cible, g2.ox + mx(q.x) - g2.x0, g2.oy + my(q.y) - g2.y0, false); majPied(); }
      else if (g2.type === 'poignee') {
        const xm = mx(q.x), ym = my(q.y);
        const delta = { E: xm - g2.x0, W: g2.x0 - xm, N: ym - g2.y0, S: g2.y0 - ym }[g2.bord];
        redimensionner(g2.bord, delta, g2.dep);
      } else if (g2.type === 'pan') { V.ox = g2.ox + q.x - g2.px; V.oy = g2.oy + q.y - g2.py; planifier({ d2: true }); }
    });
    function fin(e) {
      if (!pts2.has(e.pointerId)) return;
      pts2.delete(e.pointerId);
      if (g2 && (g2.type === 'pince' || g2.type === 'rien')) { g2 = pts2.size ? { type: 'rien' } : null; return; }
      if (pts2.size === 0) finirGeste2D(e.type === 'pointercancel');
    }
    cv.addEventListener('pointerup', fin);
    cv.addEventListener('pointercancel', fin);
    cv.addEventListener('pointerleave', function (e) { if (e.pointerType === 'mouse' && !g2) { survoler(null); cv.style.cursor = ''; } });
    cv.addEventListener('wheel', function (e) {
      e.preventDefault();
      const q = posLocale(e, cv);
      zoom2D(q.x, q.y, Math.exp(-e.deltaY * (e.deltaMode === 1 ? 0.05 : 0.0016)));
    }, { passive: false });
    cv.addEventListener('contextmenu', function (e) { e.preventDefault(); });
  }
  function finirGeste2D(interrompu) {
    const g = g2; g2 = null;
    if (!g) return;
    if (g.type === 'objet') {
      if (g.bouge) {
        if (g.cible.type === 'trou') { const t = trouModele(g.cible.id); if (t) { const pos = P.posTrou(E.p, t); P.ancrerTrou(E.p, t, pos.x, pos.y); changer(); } }
        engager(g.avant);
      } else if (!interrompu && g.cible.type === 'comp') {
        const now = Date.now();
        if (dernierTap.id === g.cible.id && now - dernierTap.t < 380) { pivoter(g.cible.id); dernierTap = { t: 0, id: null }; }
        else dernierTap = { t: now, id: g.cible.id };
      }
    } else if (g.type === 'poignee') {
      if (g.bouge) { engager(g.avant); planifier({ d2: true }); }
    } else if (g.type === 'pan') {
      if (!g.bouge && !interrompu) selectionner(g.cible || null);
    }
  }
  function survol2D(q) {
    const c = toucher(mx(q.x), my(q.y), false);
    if (c && c.type === 'poignee') { cv.style.cursor = c.bord === 'E' || c.bord === 'W' ? 'ew-resize' : 'ns-resize'; survoler(null); }
    else if (c && (c.type === 'comp' || c.type === 'trou')) { cv.style.cursor = 'move'; survoler(c); }
    else { cv.style.cursor = ''; survoler(null); }
    const pied = $('#pied2d');
    if (pied && !E.sel) { const or = origine(); pied.textContent = 'X ' + fmt(mx(q.x) - or.x, 1) + ' mm, Y ' + fmt(my(q.y) - or.y, 1) + ' mm'; }
  }
  function montrer(c) {
    if (mobile.matches) $('#panneau').dataset.ouvert = 'false';
    if (E.vue === 'boitier' && mobile.matches) changerVue('deux');
    const q = positionObjet(c), X = sx(q.x), Y = sy(q.y);
    if (X < 30 || X > V.w - 30 || Y < 50 || Y > V.h - 30) { V.ox += V.w / 2 - X; V.oy += V.h / 2 - Y; planifier({ d2: true }); }
  }

  // =====================================================================
  // Vue Boîtier (3D)
  // =====================================================================
  const R = { ok: false, sig: {}, compsMap: new Map(), w: 0, h: 0 };
  const CAM = { th: -2.15, ph: 0.62, r: 220, cx: 0, cy: 0, cz: 10 };
  function lin(hex) { return new T3.Color(hex).convertSRGBToLinear(); }
  function init3D() {
    const hote = $('#c3d');
    if (!T3) { $('#sans-webgl').hidden = false; return; }
    let rd = null;
    try { rd = new T3.WebGLRenderer({ antialias: true, alpha: true }); } catch (e) { rd = null; }
    if (!rd || !rd.getContext || !rd.getContext()) { $('#sans-webgl').hidden = false; return; }
    R.rd = rd;
    rd.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    rd.outputEncoding = T3.sRGBEncoding;
    rd.setClearColor(0x000000, 0);
    hote.appendChild(rd.domElement);
    R.scene = new T3.Scene();
    R.cam = new T3.PerspectiveCamera(32, 1, 1, 8000); R.cam.up.set(0, 0, 1); R.scene.add(R.cam);
    R.scene.add(new T3.HemisphereLight(0xffffff, 0x6f7c76, 0.72));
    const phare = new T3.DirectionalLight(0xffffff, 0.62); phare.position.set(-0.45, 0.65, 0.3);
    R.cam.add(phare); R.cam.add(phare.target); phare.target.position.set(0, 0, -1);
    const contre = new T3.DirectionalLight(0xffffff, 0.22); contre.position.set(1, -1.2, 0.8); R.scene.add(contre);
    const std = function (hex, o) { return new T3.MeshStandardMaterial(Object.assign({ color: lin(hex), roughness: 0.6, metalness: 0 }, o || {})); };
    R.mat = {
      pla: std('#D3D8D4', { roughness: 0.8, polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1 }),
      couv: std('#D9DDD9', { roughness: 0.8, polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1 }),
      arete: new T3.LineBasicMaterial({ color: lin('#56615B'), transparent: true, opacity: 0.6 }),
      pcb: std('#2E6A50', { roughness: 0.55 }),
      cuivre: std('#C98A45', { roughness: 0.35, metalness: 0.75 }),
      noir: std('#202325', { roughness: 0.45 }),
      metal: std('#C3C8CC', { roughness: 0.3, metalness: 0.8 }),
      sombre: std('#383D42', { roughness: 0.6 }),
      modulePcb: std('#1E3A55'),
      creme: std('#E8DFC8', { roughness: 0.7 }),
      condo: std('#22315A', { roughness: 0.5 }),
      gris: std('#6A6E72'),
      surbr: new T3.MeshBasicMaterial({ color: lin('#C2702F'), transparent: true, opacity: 0.55, depthWrite: false, side: T3.DoubleSide }),
      surbrDoux: new T3.MeshBasicMaterial({ color: lin('#C2702F'), transparent: true, opacity: 0.28, depthWrite: false, side: T3.DoubleSide }),
      ligneSel: new T3.LineBasicMaterial({ color: lin('#C2702F') }),
      ligneSurvol: new T3.LineBasicMaterial({ color: lin('#C2702F'), transparent: true, opacity: 0.55 }),
      ligneErr: new T3.LineBasicMaterial({ color: lin('#C2402E') }),
      proxy: new T3.MeshBasicMaterial({ transparent: true, opacity: 0, depthWrite: false })
    };
    R.leds = {};
    R.racine = new T3.Group(); R.scene.add(R.racine);
    R.corps = new T3.Mesh(new T3.BufferGeometry(), R.mat.pla);
    R.corpsA = new T3.LineSegments(new T3.BufferGeometry(), R.mat.arete);
    R.couv = new T3.Group();
    R.couvM = new T3.Mesh(new T3.BufferGeometry(), R.mat.couv);
    R.couvA = new T3.LineSegments(new T3.BufferGeometry(), R.mat.arete);
    R.couv.add(R.couvM); R.couv.add(R.couvA);
    R.carte = new T3.Mesh(new T3.BufferGeometry(), R.mat.pcb);
    R.pastilles = new T3.Group(); R.comps = new T3.Group(); R.prox = new T3.Group(); R.marques = new T3.Group();
    [R.corps, R.corpsA, R.couv, R.carte, R.pastilles, R.comps, R.prox, R.marques].forEach(function (o) { R.racine.add(o); });
    R.ray = new T3.Raycaster();
    R.ok = true;
    brancher3D(rd.domElement);
  }
  function disposer(o) {
    o.traverse(function (n) { if (n.geometry) n.geometry.dispose(); });
  }
  function vider(g) { while (g.children.length) { const c = g.children[g.children.length - 1]; g.remove(c); disposer(c); } }
  function remplacerGeo(mesh, lignes, tris) {
    const g = new T3.BufferGeometry();
    g.setAttribute('position', new T3.BufferAttribute(new Float32Array(tris), 3));
    g.computeVertexNormals();
    mesh.geometry.dispose(); mesh.geometry = g;
    lignes.geometry.dispose(); lignes.geometry = new T3.EdgesGeometry(g, 28);
  }
  function formeRR(cx, cy, hx, hy, r) {
    const s = new T3.Shape();
    r = Math.max(0, Math.min(r, hx - 0.01, hy - 0.01));
    s.moveTo(cx - hx + r, cy - hy); s.lineTo(cx + hx - r, cy - hy);
    if (r > 0) s.absarc(cx + hx - r, cy - hy + r, r, -Math.PI / 2, 0, false);
    s.lineTo(cx + hx, cy + hy - r);
    if (r > 0) s.absarc(cx + hx - r, cy + hy - r, r, 0, Math.PI / 2, false);
    s.lineTo(cx - hx + r, cy + hy);
    if (r > 0) s.absarc(cx - hx + r, cy + hy - r, r, Math.PI / 2, Math.PI, false);
    s.lineTo(cx - hx, cy - hy + r);
    if (r > 0) s.absarc(cx - hx + r, cy - hy + r, r, Math.PI, Math.PI * 1.5, false);
    return s;
  }
  function construireCarte3D() {
    const c = E.p.carte, d = E.d;
    const sh = formeRR(c.x0, c.y0, c.L / 2, c.W / 2, c.r);
    for (const t of d.trous) {
      if (P.sdRR(t.x, t.y, c.x0, c.y0, c.L / 2, c.W / 2, c.r) > -(d.vis.trou / 2 + 0.05)) continue;
      const h = new T3.Path(); h.absarc(t.x, t.y, d.vis.trou / 2, 0, Math.PI * 2, true); sh.holes.push(h);
    }
    const geo = new T3.ExtrudeGeometry(sh, { depth: c.t, bevelEnabled: false, curveSegments: 10 });
    R.carte.geometry.dispose(); R.carte.geometry = geo; R.carte.position.z = d.zpb;
    vider(R.pastilles);
    for (const t of d.trous) {
      const m = new T3.Mesh(new T3.RingGeometry(d.vis.trou / 2, d.vis.tete / 2, 32), R.mat.cuivre);
      m.position.set(t.x, t.y, d.zpt + 0.02); R.pastilles.add(m);
    }
  }
  function matLed(v) {
    const hex = couleurLed(v);
    if (!R.leds[hex]) R.leds[hex] = new T3.MeshStandardMaterial({ color: lin(hex), roughness: 0.25, transparent: true, opacity: 0.9, emissive: lin(hex), emissiveIntensity: 0.22 });
    return R.leds[hex];
  }
  function modeleComp(k) {
    const grp = new T3.Group(), T = P.TYPES[k.type];
    const lx = T.bord ? k.d : k.w, ly = T.bord ? k.w : k.d, h = k.h;
    function boite(a, b, c, x, y, z, mat) { const m = new T3.Mesh(new T3.BoxGeometry(a, b, c), mat); m.position.set(x, y, z); grp.add(m); return m; }
    function cyl(r, haut, x, y, z, mat, axe) {
      const g = new T3.CylinderGeometry(r, r, haut, 24);
      if (axe === 'x') g.rotateZ(Math.PI / 2); else g.rotateX(Math.PI / 2);
      const m = new T3.Mesh(g, mat); m.position.set(x, y, z); grp.add(m); return m;
    }
    switch (k.type) {
      case 'module': {
        boite(lx, ly, 0.8, 0, 0, 0.4, R.mat.modulePcb);
        const ls = ly * 0.72;
        boite(lx - 1.4, ls, Math.max(0.2, h - 0.8), 0, -ly / 2 + 0.5 + ls / 2, 0.8 + Math.max(0.2, h - 0.8) / 2, R.mat.metal);
        break;
      }
      case 'ic': boite(lx, ly, h, 0, 0, h / 2, R.mat.noir); break;
      case 'capteur': boite(lx, ly, Math.min(0.4, h), 0, 0, Math.min(0.4, h) / 2, R.mat.noir); boite(lx * 0.86, ly * 0.86, Math.max(0.1, h - 0.4), 0, 0, 0.4 + Math.max(0.1, h - 0.4) / 2, R.mat.metal); break;
      case 'usbc': boite(lx, ly, h, 0, 0, h / 2, R.mat.metal); boite(0.3, Math.max(0.5, ly - 1.6), Math.max(0.4, h - 1.2), lx / 2 + 0.02, 0, h / 2, R.mat.sombre); break;
      case 'jack': boite(lx, ly, h, 0, 0, h / 2, R.mat.noir); cyl(Math.min(3, ly / 3), 0.4, lx / 2 + 0.05, 0, Math.min(k.zc, h - 1), R.mat.sombre, 'x'); break;
      case 'jst': boite(lx, ly, h, 0, 0, h / 2, R.mat.creme); boite(Math.max(0.5, lx - 1.2), Math.max(0.5, ly - 1.2), 0.3, 0, 0, h - 0.1, R.mat.sombre); break;
      case 'led': {
        const r = Math.min(lx, ly) / 2;
        cyl(r * 1.12, 0.9, 0, 0, 0.45, matLed(k.valeur));
        cyl(r * 0.95, Math.max(0.2, h - r * 0.95 - 0.9), 0, 0, 0.9 + Math.max(0.2, h - r * 0.95 - 0.9) / 2, matLed(k.valeur));
        const s = new T3.SphereGeometry(r * 0.95, 20, 10, 0, Math.PI * 2, 0, Math.PI / 2); s.rotateX(Math.PI / 2);
        const m = new T3.Mesh(s, matLed(k.valeur)); m.position.set(0, 0, Math.max(1.1, h - r * 0.95)); grp.add(m);
        break;
      }
      case 'bouton': {
        const hb = Math.min(3.5, h);
        boite(lx, ly, hb, 0, 0, hb / 2, R.mat.noir);
        if (h > hb) cyl(1.75, h - hb, 0, 0, hb + (h - hb) / 2, R.mat.gris);
        break;
      }
      case 'condo': { const r = Math.min(lx, ly) / 2; cyl(r, h, 0, 0, h / 2, R.mat.condo); cyl(r * 0.9, 0.12, 0, 0, h + 0.06, R.mat.metal); break; }
      default: boite(lx, ly, h, 0, 0, h / 2, R.mat.noir);
    }
    grp.traverse(function (o) { if (o.isMesh) o.userData.cible = { type: 'comp', id: k.id }; });
    return grp;
  }
  function majComps3D() {
    const d = E.d, vus = new Set();
    for (const g of d.comps) {
      const k = compModele(g.id); if (!k) continue;
      vus.add(g.id);
      const sig = [k.type, k.w, k.d, k.h, k.valeur, k.zc || 0].join('|');
      let ent = R.compsMap.get(g.id);
      if (!ent || ent.sig !== sig) {
        if (ent) { R.comps.remove(ent.grp); disposer(ent.grp); }
        ent = { sig: sig, grp: modeleComp(k) }; R.compsMap.set(g.id, ent); R.comps.add(ent.grp);
      }
      ent.grp.position.set(g.x, g.y, d.zpt);
      ent.grp.rotation.z = g.rot * Math.PI / 180;
    }
    R.compsMap.forEach(function (ent, id) { if (!vus.has(id)) { R.comps.remove(ent.grp); disposer(ent.grp); R.compsMap.delete(id); } });
  }
  function majProx3D() {
    vider(R.prox);
    const d = E.d, h = d.zpt - d.zf;
    for (const t of d.trous) {
      const g = new T3.CylinderGeometry(d.Rb, d.Rb, h, 20); g.rotateX(Math.PI / 2);
      const m = new T3.Mesh(g, R.mat.proxy); m.position.set(t.x, t.y, d.zf + h / 2);
      m.userData.cible = { type: 'trou', id: t.id }; R.prox.add(m);
    }
  }
  function hauteurSoulevee() { const d = E.d; return Math.max(16, 0.42 * Math.max(d.Lo, d.Wo)); }
  function regler3D() {
    const souleve = E.couvercle === 'souleve';
    R.couv.visible = E.couvercle !== 'masque';
    R.couv.position.z = souleve ? hauteurSoulevee() : 0;
    const mc = R.mat.couv, tc = souleve || E.transparence, oc = souleve ? 0.5 : (E.transparence ? 0.22 : 1);
    if (mc.transparent !== tc || mc.opacity !== oc) { mc.transparent = tc; mc.opacity = oc; mc.depthWrite = !tc; mc.needsUpdate = true; }
    const mp = R.mat.pla, op = E.transparence ? 0.22 : 1;
    if (mp.transparent !== E.transparence || mp.opacity !== op) { mp.transparent = E.transparence; mp.opacity = op; mp.depthWrite = !E.transparence; mp.needsUpdate = true; }
    R.corps.renderOrder = E.transparence ? 2 : 0; R.corpsA.renderOrder = 3; R.couvM.renderOrder = 4; R.couvA.renderOrder = 5;
  }
  function maj3D() {
    if (!R.ok || !E.d) return;
    const p = E.p, d = E.d;
    const dec = P.decoupesValides(d), pil = P.piliersValides(d), perc = P.percagesValides(d);
    const sigCorps = JSON.stringify([d.Lo, d.Wo, d.ro, d.Li, d.Wi, d.ri, d.zf, d.zt, d.zpb, d.Rb, d.rp, d.cx, d.cy, dec, pil]);
    if (sigCorps !== R.sig.corps) {
      R.sig.corps = sigCorps;
      const t = P.construireCorps(p, d, T3, { seg: 8, segCercle: 28 });
      R.volCorps = P.volume(t); remplacerGeo(R.corps, R.corpsA, t);
    }
    const sigCouv = JSON.stringify([d.Lo, d.Wo, d.ro, d.lo, d.li, d.zt, d.ztop, d.zLevre, d.cx, d.cy, perc]);
    if (sigCouv !== R.sig.couv) {
      R.sig.couv = sigCouv;
      const t = P.construireCouvercle(p, d, T3, { seg: 8, segCercle: 28 });
      R.volCouv = P.volume(t); remplacerGeo(R.couvM, R.couvA, t);
    }
    const sigCarte = JSON.stringify([p.carte, d.zpb, d.trous, p.boitier.vis]);
    if (sigCarte !== R.sig.carte) { R.sig.carte = sigCarte; construireCarte3D(); }
    majComps3D();
    majProx3D();
    regler3D();
    surbrillance3D();
  }
  function volumeDecoupe(o, fort) {
    const d = E.d, e = E.p.boitier.paroi;
    const pts = P.profilArrondi(o.u, o.zc, o.w, o.h, o.r, 8);
    const sh = new T3.Shape(pts.map(function (q) { return new T3.Vector2(q[0], q[1]); }));
    const geo = new T3.ExtrudeGeometry(sh, { depth: e + 0.6, bevelEnabled: false, curveSegments: 6 });
    const m = new T3.Matrix4();
    if (o.mur === 'N' || o.mur === 'S') {
      const sg = o.mur === 'N' ? 1 : -1, y0 = d.cy + sg * (d.Wi / 2 - 0.3);
      m.set(1, 0, 0, 0, 0, 0, sg, y0, 0, 1, 0, 0, 0, 0, 0, 1);
    } else {
      const sg = o.mur === 'E' ? 1 : -1, x0 = d.cx + sg * (d.Li / 2 - 0.3);
      m.set(0, 0, sg, x0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1);
    }
    geo.applyMatrix4(m);
    return new T3.Mesh(geo, fort ? R.mat.surbr : R.mat.surbrDoux);
  }
  function planMur(code, actif) {
    const bord = code.charAt(0), ext = code.indexOf(':ext') > 0, d = E.d, hz = bord === 'N' || bord === 'S';
    const z0 = ext ? 0 : d.zf, haut = Math.max(0.5, d.zt - z0);
    const geo = new T3.PlaneGeometry(Math.max(0.5, hz ? 2 * d.sb : 2 * d.sa), haut);
    geo.rotateX(Math.PI / 2);
    if (!hz) geo.rotateZ(Math.PI / 2);
    const m = new T3.Mesh(geo, actif ? R.mat.surbr : R.mat.surbrDoux);
    const dx = (ext ? d.Lo : d.Li) / 2 + (ext ? 0.25 : -0.25), dy = (ext ? d.Wo : d.Wi) / 2 + (ext ? 0.25 : -0.25), zc = z0 + haut / 2;
    if (bord === 'N') m.position.set(d.cx, d.cy + dy, zc);
    else if (bord === 'S') m.position.set(d.cx, d.cy - dy, zc);
    else if (bord === 'E') m.position.set(d.cx + dx, d.cy, zc);
    else m.position.set(d.cx - dx, d.cy, zc);
    return m;
  }
  function boiteContour(g, k, mat, marge) {
    const T = P.TYPES[k.type], lx = (T.bord ? k.d : k.w) + marge, ly = (T.bord ? k.w : k.d) + marge;
    const b = new T3.LineSegments(new T3.EdgesGeometry(new T3.BoxGeometry(lx, ly, k.h + marge / 2)), mat);
    b.position.set(g.x, g.y, E.d.zpt + k.h / 2); b.rotation.z = g.rot * Math.PI / 180;
    return b;
  }
  function surbrillance3D() {
    if (!R.ok || !E.d) return;
    vider(R.marques);
    const d = E.d;
    const erreurs = new Set();
    for (const q of E.pb) if (q.sev === 'erreur') q.ids.forEach(function (i) { erreurs.add(i); });
    for (const g of d.comps) {
      if (!erreurs.has(g.id) || (E.sel && E.sel.id === g.id)) continue;
      const k = compModele(g.id); if (k) R.marques.add(boiteContour(g, k, R.mat.ligneErr, 0.5));
    }
    function focus(c, fort) {
      if (!c) return;
      if (c.type === 'comp') {
        const g = geoComp(c.id), k = compModele(c.id); if (!g || !k) return;
        R.marques.add(boiteContour(g, k, fort ? R.mat.ligneSel : R.mat.ligneSurvol, 0.9));
        if (g.decoupe) R.marques.add(volumeDecoupe(g.decoupe, fort));
        if (g.trouCouvercle && R.couv.visible) {
          const o = g.trouCouvercle, e = E.p.boitier.couvercle;
          const cg = new T3.CylinderGeometry(o.d / 2 + 0.05, o.d / 2 + 0.05, e + 0.8, 28, 1, true); cg.rotateX(Math.PI / 2);
          const m = new T3.Mesh(cg, fort ? R.mat.surbr : R.mat.surbrDoux);
          m.position.set(o.x, o.y, d.zt + e / 2 + R.couv.position.z); R.marques.add(m);
        }
      } else if (c.type === 'trou') {
        const t = geoTrou(c.id); if (!t) return;
        const h = d.zpt - d.zf + 0.3;
        const cg = new T3.CylinderGeometry(d.Rb + 0.3, d.Rb + 0.3, h, 32, 1, true); cg.rotateX(Math.PI / 2);
        const m = new T3.Mesh(cg, fort ? R.mat.surbr : R.mat.surbrDoux); m.position.set(t.x, t.y, d.zf + h / 2 - 0.1); R.marques.add(m);
      } else if (c.type === 'carte') {
        const k = E.p.carte, pts = P.contourArrondi(k.x0, k.y0, k.L / 2 + 0.4, k.W / 2 + 0.4, k.r + 0.4, 8);
        const geo = new T3.BufferGeometry().setFromPoints(pts.map(function (q) { return new T3.Vector3(q[0], q[1], d.zpt + 0.05); }));
        R.marques.add(new T3.LineLoop(geo, R.mat.ligneSel));
      }
    }
    if (E.survol && !memeCible(E.survol, E.sel)) focus(E.survol, false);
    focus(E.sel, true);
    const mur = R.murActif || R.murSurvol;
    if (mur) R.marques.add(planMur(mur, !!R.murActif));
    rendre3D();
  }
  function placerCam() {
    const cp = Math.cos(CAM.ph);
    R.cam.position.set(CAM.cx + CAM.r * cp * Math.cos(CAM.th), CAM.cy + CAM.r * cp * Math.sin(CAM.th), CAM.cz + CAM.r * Math.sin(CAM.ph));
    R.cam.lookAt(CAM.cx, CAM.cy, CAM.cz);
    R.cam.updateMatrixWorld();
  }
  function recadrer3D() {
    if (!R.ok || !E.d || R.w < 4 || R.h < 4) return;
    const d = E.d, hz = d.ztop + (E.couvercle === 'souleve' ? hauteurSoulevee() : 0);
    CAM.cx = d.cx; CAM.cy = d.cy; CAM.cz = hz * 0.45;
    const rayon = 0.5 * Math.sqrt(d.Lo * d.Lo + d.Wo * d.Wo + hz * hz);
    const vf = R.cam.fov * Math.PI / 360, hf = Math.atan(Math.tan(vf) * R.cam.aspect);
    CAM.r = rayon / Math.sin(Math.min(vf, hf)) * 1.04;
    placerCam(); rendre3D();
  }
  let idRendu = 0;
  function rendre3D() {
    if (!R.ok || idRendu) return;
    idRendu = requestAnimationFrame(function () { idRendu = 0; if (R.w > 1 && R.h > 1) R.rd.render(R.scene, R.cam); });
  }
  function taille3D() {
    if (!R.ok) return;
    const r = $('#c3d').getBoundingClientRect();
    const w = Math.max(1, Math.round(r.width)), h = Math.max(1, Math.round(r.height));
    if (w === R.w && h === R.h) return;
    R.w = w; R.h = h;
    R.rd.setSize(w, h, false);
    R.rd.domElement.style.width = '100%'; R.rd.domElement.style.height = '100%';
    R.cam.aspect = w / h; R.cam.updateProjectionMatrix();
    if (!R.cadre && w > 4 && h > 4) { R.cadre = true; recadrer3D(); }
    rendre3D();
  }
  function rayon3D(q) { R.ray.setFromCamera({ x: (q.x / R.w) * 2 - 1, y: -(q.y / R.h) * 2 + 1 }, R.cam); return R.ray; }
  function viser(q) {
    const ray = rayon3D(q), d = E.d;
    const hc = ray.intersectObjects([R.comps, R.prox], true).find(function (h) { return h.object.userData && h.object.userData.cible; });
    const hb = ray.intersectObject(R.corps, false)[0];
    if (hc && (E.transparence || !hb || hc.distance <= hb.distance + 0.05)) return { cible: hc.object.userData.cible, point: hc.point };
    if (hb && hb.face) {
      const n = hb.face.normal, pt = hb.point;
      if (Math.abs(n.z) < 0.3) {
        if (Math.abs(n.x) > 0.9 && Math.abs(pt.x - d.cx) >= d.Li / 2 - 0.05 && Math.abs(pt.y - d.cy) <= d.sa + 0.01) return { mur: (pt.x > d.cx ? 'E' : 'W') + (Math.abs(pt.x - d.cx) > (d.Li + d.Lo) / 4 ? ':ext' : ':int'), point: pt };
        if (Math.abs(n.y) > 0.9 && Math.abs(pt.y - d.cy) >= d.Wi / 2 - 0.05 && Math.abs(pt.x - d.cx) <= d.sb + 0.01) return { mur: (pt.y > d.cy ? 'N' : 'S') + (Math.abs(pt.y - d.cy) > (d.Wi + d.Wo) / 4 ? ':ext' : ':int'), point: pt };
      }
      return { corps: true, point: pt };
    }
    return null;
  }
  function surPlan(q, z) {
    const ray = rayon3D(q).ray, out = new T3.Vector3();
    return ray.intersectPlane(new T3.Plane(new T3.Vector3(0, 0, 1), -z), out) ? out : null;
  }
  function pan3D(dx, dy) {
    const k = 2 * CAM.r * Math.tan(R.cam.fov * Math.PI / 360) / R.h;
    const dr = new T3.Vector3().setFromMatrixColumn(R.cam.matrixWorld, 0), ht = new T3.Vector3().setFromMatrixColumn(R.cam.matrixWorld, 1);
    CAM.cx += (-dx * dr.x + dy * ht.x) * k; CAM.cy += (-dx * dr.y + dy * ht.y) * k; CAM.cz += (-dx * dr.z + dy * ht.z) * k;
    placerCam(); rendre3D();
  }
  const pts3 = new Map();
  let g3 = null, dernierTap3 = { t: 0, id: null };
  function brancher3D(c3) {
    c3.addEventListener('pointerdown', function (e) {
      if (c3.setPointerCapture) { try { c3.setPointerCapture(e.pointerId); } catch (x) { /* ignore */ } }
      const q = posLocale(e, c3);
      pts3.set(e.pointerId, q);
      if (pts3.size === 2) {
        finirGeste3D(true);
        const v = Array.from(pts3.values());
        g3 = { type: 'pince', d0: Math.hypot(v[0].x - v[1].x, v[0].y - v[1].y) || 1, r0: CAM.r, mx: (v[0].x + v[1].x) / 2, my: (v[0].y + v[1].y) / 2 };
        return;
      }
      if (pts3.size > 2) return;
      const panne = e.pointerType === 'mouse' && (e.button === 1 || e.button === 2 || e.shiftKey);
      const v = panne ? null : viser(q);
      if (v && v.cible) {
        selectionner(v.cible);
        const pos = positionObjet(v.cible), z = E.d.zpt;
        g3 = { type: 'objet', cible: v.cible, z: z, p0: surPlan(q, z), ox: pos.x, oy: pos.y, px: q.x, py: q.y, avant: instant(), bouge: false };
      } else if (v && v.mur) {
        g3 = { type: 'mur', bord: v.mur.charAt(0), z: v.point.z, p0: surPlan(q, v.point.z), dep: Object.assign({}, E.p.carte), px: q.x, py: q.y, avant: instant(), bouge: false };
        R.murActif = v.mur; planifier({ surbrillance: true });
      } else {
        g3 = { type: panne ? 'pan' : 'orbite', px: q.x, py: q.y, lx: q.x, ly: q.y, bouge: false, corps: !!(v && v.corps) };
      }
      e.preventDefault();
    });
    c3.addEventListener('pointermove', function (e) {
      const q = posLocale(e, c3);
      if (!pts3.has(e.pointerId)) { if (e.pointerType === 'mouse') survol3D(q, c3); return; }
      pts3.set(e.pointerId, q);
      if (!g3) return;
      if (g3.type === 'pince') {
        if (pts3.size < 2) return;
        const v = Array.from(pts3.values()), dd = Math.hypot(v[0].x - v[1].x, v[0].y - v[1].y) || 1;
        CAM.r = borne(g3.r0 * g3.d0 / dd, 8, 6000);
        const nx = (v[0].x + v[1].x) / 2, ny = (v[0].y + v[1].y) / 2;
        placerCam(); pan3D(nx - g3.mx, ny - g3.my); g3.mx = nx; g3.my = ny;
        return;
      }
      if (g3.type === 'rien') return;
      if (!g3.bouge && Math.hypot(q.x - g3.px, q.y - g3.py) < (e.pointerType === 'mouse' ? 3 : 7)) return;
      g3.bouge = true;
      if (g3.type === 'objet') {
        const pt = surPlan(q, g3.z);
        if (pt && g3.p0) deplacerObjet(g3.cible, g3.ox + pt.x - g3.p0.x, g3.oy + pt.y - g3.p0.y, false);
        majPied();
      } else if (g3.type === 'mur') {
        const pt = surPlan(q, g3.z);
        if (pt && g3.p0) {
          const delta = { E: pt.x - g3.p0.x, W: g3.p0.x - pt.x, N: pt.y - g3.p0.y, S: g3.p0.y - pt.y }[g3.bord];
          redimensionner(g3.bord, delta, g3.dep);
        }
      } else if (g3.type === 'orbite') {
        CAM.th -= (q.x - g3.lx) * 0.008; CAM.ph = borne(CAM.ph + (q.y - g3.ly) * 0.008, -0.2, 1.52);
        g3.lx = q.x; g3.ly = q.y; placerCam(); rendre3D();
      } else if (g3.type === 'pan') {
        pan3D(q.x - g3.lx, q.y - g3.ly); g3.lx = q.x; g3.ly = q.y;
      }
    });
    function fin(e) {
      if (!pts3.has(e.pointerId)) return;
      pts3.delete(e.pointerId);
      if (g3 && (g3.type === 'pince' || g3.type === 'rien')) { g3 = pts3.size ? { type: 'rien' } : null; return; }
      if (pts3.size === 0) finirGeste3D(e.type === 'pointercancel');
    }
    c3.addEventListener('pointerup', fin);
    c3.addEventListener('pointercancel', fin);
    c3.addEventListener('pointerleave', function (e) {
      if (e.pointerType === 'mouse' && !g3) { c3.style.cursor = ''; if (R.murSurvol) { R.murSurvol = null; planifier({ surbrillance: true }); } survoler(null); }
    });
    c3.addEventListener('wheel', function (e) {
      e.preventDefault();
      CAM.r = borne(CAM.r * Math.exp(e.deltaY * (e.deltaMode === 1 ? 0.05 : 0.0012)), 8, 6000);
      placerCam(); rendre3D();
    }, { passive: false });
    c3.addEventListener('contextmenu', function (e) { e.preventDefault(); });
  }
  function finirGeste3D(interrompu) {
    const g = g3; g3 = null;
    if (!g) return;
    if (g.type === 'objet') {
      if (g.bouge) {
        if (g.cible.type === 'trou') { const t = trouModele(g.cible.id); if (t) { const pos = P.posTrou(E.p, t); P.ancrerTrou(E.p, t, pos.x, pos.y); changer(); } }
        engager(g.avant);
      } else if (!interrompu && g.cible.type === 'comp') {
        const now = Date.now();
        if (dernierTap3.id === g.cible.id && now - dernierTap3.t < 380) { pivoter(g.cible.id); dernierTap3 = { t: 0, id: null }; }
        else dernierTap3 = { t: now, id: g.cible.id };
      }
    } else if (g.type === 'mur') {
      R.murActif = null;
      if (g.bouge) engager(g.avant);
      planifier({ surbrillance: true, d2: true });
    } else if (g.type === 'orbite' && !g.bouge && !interrompu) {
      selectionner(g.corps ? { type: 'carte', id: 'carte' } : null);
    }
  }
  function survol3D(q, c3) {
    const v = viser(q);
    const mur = v && v.mur ? v.mur : null;
    if (mur !== (R.murSurvol || null)) { R.murSurvol = mur; planifier({ surbrillance: true }); }
    if (v && v.cible) { c3.style.cursor = 'move'; survoler(v.cible); }
    else { c3.style.cursor = mur ? (mur.charAt(0) === 'E' || mur.charAt(0) === 'W' ? 'ew-resize' : 'ns-resize') : 'grab'; survoler(null); }
  }

  // =====================================================================
  // Panneau
  // =====================================================================
  let champs = [], nChamp = 0;
  function tete(hote, titreFn, sousTitre) {
    const t = el('div', 'tete'), b = el('div');
    const h = el('h2'); b.appendChild(h); champs.push({ lecture: titreFn, el: h });
    if (sousTitre) b.appendChild(el('p', null, sousTitre));
    t.appendChild(b); hote.appendChild(t);
  }
  function groupe(hote, titre) {
    const g = el('section', 'groupe');
    if (titre) g.appendChild(el('h3', null, titre));
    hote.appendChild(g); return g;
  }
  function valeurAff(o) {
    const v = o.get();
    if (o.choix) return String(v);
    if (o.texte) return v === null || v === undefined ? '' : String(v);
    return fmt(v, o.dec === undefined ? 2 : o.dec);
  }
  function champ(g, o) {
    const l = el('div', o.texte ? 'ligne texte' : 'ligne'), id = 'ch' + (++nChamp);
    const lab = el('label', null, o.label); lab.htmlFor = id; l.appendChild(lab);
    const box = el('div', 'champ');
    let inp;
    if (o.choix) {
      inp = el('select');
      for (const c of o.choix) { const op = el('option', null, c[1]); op.value = String(c[0]); inp.appendChild(op); }
      inp.addEventListener('change', function () { appliquer(o, inp.value, inp); });
    } else {
      inp = el('input'); inp.type = 'text'; inp.autocomplete = 'off'; inp.spellcheck = false;
      if (o.texte) inp.className = 'texte'; else inp.inputMode = 'decimal';
      inp.addEventListener('change', function () { appliquer(o, inp.value, inp); });
      inp.addEventListener('keydown', function (e) {
        if (e.key === 'Enter') { e.preventDefault(); inp.blur(); }
        else if (e.key === 'Escape') { inp.value = valeurAff(o); inp.removeAttribute('aria-invalid'); inp.blur(); }
      });
      if (!o.texte) inp.addEventListener('focus', function () { try { inp.select(); } catch (x) { /* ignore */ } });
    }
    inp.id = id; box.appendChild(inp);
    if (o.unite) box.appendChild(el('span', 'unite', o.unite));
    l.appendChild(box); g.appendChild(l);
    champs.push({ o: o, inp: inp });
    return inp;
  }
  function appliquer(o, brut, inp) {
    if (o.choix || o.texte) {
      if (String(o.get()) === String(brut)) return;
      modifier(function () { o.set(brut); });
      return;
    }
    const v = P.lireNombre(brut);
    if (!Number.isFinite(v)) { inp.setAttribute('aria-invalid', 'true'); toast('Saisis un nombre, par exemple 12,5.'); return; }
    inp.removeAttribute('aria-invalid');
    const b = borne(v, o.min === undefined ? -1e6 : o.min, o.max === undefined ? 1e6 : o.max);
    if (Math.abs(b - v) > 1e-9) toast(o.label + ' : ramené à ' + fmt(b, 2) + (o.unite ? ' ' + o.unite : '') + '.');
    if (Math.abs(b - o.get()) < 1e-9) { inp.value = valeurAff(o); return; }
    modifier(function () { o.set(b); });
  }
  function lecture(g, fn, cls) { const p = el('p', cls || 'lecture'); g.appendChild(p); champs.push({ lecture: fn, el: p }); return p; }
  function interrupteur(g, libelle, get, set) {
    const l = el('label', 'interrupteur'), c = el('input');
    c.type = 'checkbox'; l.appendChild(c); l.appendChild(doc.createTextNode(libelle));
    c.addEventListener('change', function () { modifier(function () { set(c.checked); }); });
    g.appendChild(l); champs.push({ coche: get, inp: c });
  }
  function boutons(hote, liste) {
    const r = el('div', 'rangee');
    for (const b of liste) { const bt = el('button', 'btn' + (b.danger ? ' danger' : ''), b.texte); bt.type = 'button'; bt.addEventListener('click', b.action); r.appendChild(bt); }
    hote.appendChild(r);
  }
  function syncChamps() {
    for (const c of champs) {
      if (c.lecture) { const t = c.lecture(); if (c.el.textContent !== t) c.el.textContent = t; continue; }
      if (c.coche) { c.inp.checked = !!c.coche(); continue; }
      if (doc.activeElement === c.inp) continue;
      const v = valeurAff(c.o); if (c.inp.value !== v) c.inp.value = v;
    }
  }
  function construirePanneau() {
    const hote = $('#p-proprietes');
    hote.textContent = ''; champs = [];
    const s = E.sel, o = objet(s);
    if (s && s.type === 'comp' && o) panneauComp(hote, o);
    else if (s && s.type === 'trou' && o) panneauTrou(hote, o);
    else panneauProjet(hote);
    syncChamps();
  }
  function panneauProjet(hote) {
    const p = function () { return E.p; };
    tete(hote, function () { return E.p.nom; }, null);
    lecture(hote, function () { const d = E.d, c = E.p.carte; return 'Carte de ' + fmt(c.L) + ' × ' + fmt(c.W) + ' mm dans un boîtier de ' + fmt(d.Lo) + ' × ' + fmt(d.Wo) + ' × ' + fmt(d.ztop) + ' mm, couvercle compris.'; });
    let g = groupe(hote, null);
    champ(g, { label: 'Nom du projet', texte: true, get: function () { return p().nom; }, set: function (v) { const t = String(v).trim().slice(0, 80); if (t) E.p.nom = t; } });
    g = groupe(hote, 'Carte');
    champ(g, { label: 'Longueur', unite: 'mm', min: 10, max: 300, get: function () { return p().carte.L; }, set: function (v) { E.p.carte.L = v; E.p.carte.r = Math.min(E.p.carte.r, Math.min(E.p.carte.L, E.p.carte.W) / 2 - 0.5); contraindreBords(E.p); } });
    champ(g, { label: 'Largeur', unite: 'mm', min: 10, max: 300, get: function () { return p().carte.W; }, set: function (v) { E.p.carte.W = v; E.p.carte.r = Math.min(E.p.carte.r, Math.min(E.p.carte.L, E.p.carte.W) / 2 - 0.5); contraindreBords(E.p); } });
    champ(g, { label: 'Rayon des angles', unite: 'mm', min: 0, max: 50, get: function () { return p().carte.r; }, set: function (v) { E.p.carte.r = Math.min(v, Math.min(E.p.carte.L, E.p.carte.W) / 2 - 0.5); } });
    champ(g, { label: 'Épaisseur', choix: P.EPAISSEURS_PCB.map(function (t) { return [t, fmt(t, 1) + ' mm']; }), get: function () { return p().carte.t; }, set: function (v) { E.p.carte.t = parseFloat(v); E.p.boitier.hauteur = Math.max(E.p.boitier.hauteur, E.p.boitier.entretoise + E.p.carte.t + 1); } });
    g = groupe(hote, 'Boîtier');
    champ(g, { label: 'Longueur intérieure', unite: 'mm', min: 11, max: 320, get: function () { return E.d.Li; }, set: function (v) { E.p.carte.L = borne(v - 2 * E.p.boitier.jeu, 10, 300); contraindreBords(E.p); } });
    champ(g, { label: 'Largeur intérieure', unite: 'mm', min: 11, max: 320, get: function () { return E.d.Wi; }, set: function (v) { E.p.carte.W = borne(v - 2 * E.p.boitier.jeu, 10, 300); contraindreBords(E.p); } });
    lecture(g, function () { return 'Liées à la carte : intérieur = carte + 2 × ' + fmt(E.p.boitier.jeu, 2) + ' mm de jeu.'; }, 'lien');
    champ(g, { label: 'Jeu autour de la carte', unite: 'mm', min: 0.2, max: 10, get: function () { return p().boitier.jeu; }, set: function (v) { E.p.boitier.jeu = v; } });
    champ(g, { label: 'Épaisseur des parois', unite: 'mm', min: 0.8, max: 8, get: function () { return p().boitier.paroi; }, set: function (v) { E.p.boitier.paroi = v; } });
    champ(g, { label: 'Épaisseur du fond', unite: 'mm', min: 0.8, max: 8, get: function () { return p().boitier.fond; }, set: function (v) { E.p.boitier.fond = v; } });
    champ(g, { label: 'Hauteur des entretoises', unite: 'mm', min: 1, max: 30, get: function () { return p().boitier.entretoise; }, set: function (v) { E.p.boitier.entretoise = v; E.p.boitier.hauteur = Math.max(E.p.boitier.hauteur, v + E.p.carte.t + 1); } });
    champ(g, { label: 'Hauteur intérieure', unite: 'mm', min: 3, max: 200, get: function () { return E.d.H; }, set: function (v) { E.p.boitier.hauteur = Math.max(v, E.p.boitier.entretoise + E.p.carte.t + 1); E.p.boitier.hauteurAuto = false; } });
    interrupteur(g, 'Suivre les composants', function () { return E.p.boitier.hauteurAuto; }, function (on) { if (!on) E.p.boitier.hauteur = E.d.H; E.p.boitier.hauteurAuto = on; });
    champ(g, { label: 'Épaisseur du couvercle', unite: 'mm', min: 0.8, max: 8, get: function () { return p().boitier.couvercle; }, set: function (v) { E.p.boitier.couvercle = v; } });
    g = groupe(hote, 'Fixation de la carte');
    champ(g, { label: 'Vis autotaraudeuse', choix: Object.keys(P.VIS).map(function (v) { return [v, v.replace('.', ',')]; }), get: function () { return p().boitier.vis; }, set: function (v) { if (P.VIS[v]) E.p.boitier.vis = v; } });
    lecture(g, function () { const d = E.d; return E.p.trous.length + (E.p.trous.length > 1 ? ' trous' : ' trou') + ' de Ø ' + fmt(d.vis.trou, 2) + ' mm. Piliers de Ø ' + fmt(2 * d.Rb, 2) + ' mm avec avant-trou de Ø ' + fmt(d.vis.avant, 2) + ' mm, vis ' + E.p.boitier.vis + ' × ' + d.longVis + '.'; });
    boutons(hote, [{ texte: 'Ajouter un trou de fixation', action: function () { ajouter('trou'); } }]);
  }
  function panneauComp(hote, k) {
    const T = P.TYPES[k.type], id = k.id;
    const K = function () { return compModele(id) || k; }, G = function () { return geoComp(id); };
    tete(hote, function () { return K().ref; }, T.nom);
    let g = groupe(hote, null);
    champ(g, { label: 'Repère', texte: true, get: function () { return K().ref; }, set: function (v) { const t = String(v).trim().slice(0, 12); if (t) K().ref = t; } });
    champ(g, { label: 'Valeur', texte: true, get: function () { return K().valeur; }, set: function (v) { K().valeur = String(v).slice(0, 60); } });
    g = groupe(hote, 'Position');
    if (T.bord) {
      champ(g, { label: 'Bord', choix: [['W', 'Gauche'], ['E', 'Droite'], ['N', 'Haut'], ['S', 'Bas']], get: function () { return K().bord; },
        set: function (v) { const q = K(), c = E.p.carte, ancien = q.bord, hzA = ancien === 'N' || ancien === 'S', hzN = v === 'N' || v === 'S'; q.bord = v; if (hzA !== hzN) q.le_long = hzN ? c.x0 : c.y0; contraindreBords(E.p); } });
      champ(g, { label: 'Le long du bord', unite: 'mm', min: 0, max: 300, get: function () { const q = K(), o = origine(); return q.le_long - (q.bord === 'N' || q.bord === 'S' ? o.x : o.y); },
        set: function (v) { const q = K(), o = origine(); q.le_long = v + (q.bord === 'N' || q.bord === 'S' ? o.x : o.y); contraindreBords(E.p); } });
      champ(g, { label: 'Écart à la paroi', unite: 'mm', min: 0, max: 10, get: function () { return K().ecart; }, set: function (v) { K().ecart = v; } });
      lecture(g, function () { return 'Mesuré depuis le coin ' + (K().bord === 'N' || K().bord === 'S' ? 'gauche' : 'bas') + ' de la carte. Le connecteur s’oriente seul vers la paroi.'; }, 'lien');
    } else {
      champ(g, { label: 'X', unite: 'mm', min: -500, max: 500, get: function () { return G().x - origine().x; }, set: function (v) { K().x = v + origine().x; } });
      champ(g, { label: 'Y', unite: 'mm', min: -500, max: 500, get: function () { return G().y - origine().y; }, set: function (v) { K().y = v + origine().y; } });
      champ(g, { label: 'Rotation', choix: [[0, '0°'], [90, '90°'], [180, '180°'], [270, '270°']], get: function () { return K().rot; }, set: function (v) { K().rot = parseInt(v, 10) || 0; } });
      lecture(g, function () { return 'Position du centre, depuis le coin inférieur gauche de la carte.'; }, 'lien');
    }
    g = groupe(hote, 'Dimensions');
    champ(g, { label: T.bord ? 'Largeur' : 'Longueur', unite: 'mm', min: 0.5, max: 150, get: function () { return K().w; }, set: function (v) { K().w = v; contraindreBords(E.p); } });
    champ(g, { label: T.bord ? 'Profondeur' : 'Largeur', unite: 'mm', min: 0.5, max: 150, get: function () { return K().d; }, set: function (v) { K().d = v; } });
    champ(g, { label: 'Hauteur', unite: 'mm', min: 0.2, max: 100, get: function () { return K().h; }, set: function (v) { K().h = v; } });
    lecture(g, function () {
      const q = G(); if (!q) return '';
      if (T.bouton) return q.top >= E.d.zt - 0.3 ? 'La tige dépasse de ' + fmt(q.top - E.d.zt, 2) + ' mm dans le couvercle, on peut l’appuyer.' : 'Il manque ' + fmt(E.d.zt - q.top, 2) + ' mm pour atteindre le couvercle.';
      const marge = E.d.zt - q.top;
      return marge >= 0 ? 'Marge sous le couvercle : ' + fmt(marge, 2) + ' mm.' : 'Dépasse le couvercle de ' + fmt(-marge, 2) + ' mm.';
    });
    if (T.bord) {
      g = groupe(hote, 'Découpe dans la paroi');
      champ(g, { label: 'Largeur', unite: 'mm', min: 1, max: 100, get: function () { return K().decoupe.w; }, set: function (v) { const q = K(); q.decoupe.w = v; q.decoupe.r = Math.min(q.decoupe.r, Math.min(q.decoupe.w, q.decoupe.h) / 2); } });
      champ(g, { label: 'Hauteur', unite: 'mm', min: 1, max: 100, get: function () { return K().decoupe.h; }, set: function (v) { const q = K(); q.decoupe.h = v; q.decoupe.r = Math.min(q.decoupe.r, Math.min(q.decoupe.w, q.decoupe.h) / 2); } });
      champ(g, { label: 'Rayon des angles', unite: 'mm', min: 0, max: 50, get: function () { return K().decoupe.r; }, set: function (v) { const q = K(); q.decoupe.r = Math.min(v, Math.min(q.decoupe.w, q.decoupe.h) / 2); } });
      champ(g, { label: 'Axe au-dessus de la carte', unite: 'mm', min: 0, max: 100, get: function () { return K().zc; }, set: function (v) { K().zc = v; } });
      lecture(g, function () { const q = G(); if (!q) return ''; const mur = { W: 'gauche', E: 'droite', N: 'du haut', S: 'du bas' }[q.bord]; return 'La découpe suit le connecteur dans la paroi ' + mur + ', son centre à ' + fmt(q.decoupe.zc, 2) + ' mm du dessous du boîtier.'; });
    }
    if (T.couvercle) {
      g = groupe(hote, 'Perçage du couvercle');
      champ(g, { label: 'Diamètre', unite: 'mm', min: 0.5, max: 30, get: function () { return K().trou_couvercle; }, set: function (v) { K().trou_couvercle = v; } });
      lecture(g, function () { return 'Le perçage suit le composant quand tu le déplaces.'; }, 'lien');
    }
    const actions = [];
    if (!T.bord) actions.push({ texte: 'Pivoter de 90°', action: function () { pivoter(id); } });
    actions.push({ texte: 'Dupliquer', action: function () { dupliquer(id); } });
    actions.push({ texte: 'Supprimer', danger: true, action: function () { supprimer({ type: 'comp', id: id }); } });
    boutons(hote, actions);
  }
  function panneauTrou(hote, t) {
    const id = t.id, Tm = function () { return trouModele(id) || t; }, G = function () { return geoTrou(id); };
    tete(hote, function () { return Tm().ref; }, 'Trou de fixation');
    let g = groupe(hote, 'Position');
    champ(g, { label: 'X', unite: 'mm', min: -500, max: 500, get: function () { return G().x - origine().x; }, set: function (v) { const q = G(); P.ancrerTrou(E.p, Tm(), v + origine().x, q.y); } });
    champ(g, { label: 'Y', unite: 'mm', min: -500, max: 500, get: function () { return G().y - origine().y; }, set: function (v) { const q = G(); P.ancrerTrou(E.p, Tm(), q.x, v + origine().y); } });
    lecture(g, function () {
      const q = Tm();
      if (!q.coin) return 'Position libre sur la carte.';
      const nom = { NE: 'haut droit', NW: 'haut gauche', SE: 'bas droit', SW: 'bas gauche' }[q.coin];
      return 'Suit le coin ' + nom + ' de la carte quand elle change de taille.';
    }, 'lien');
    lecture(g, function () { const q = G(), c = E.p.carte; if (!q) return ''; return 'À ' + fmt(-P.sdRR(q.x, q.y, c.x0, c.y0, c.L / 2, c.W / 2, c.r), 2) + ' mm du bord de la carte.'; });
    g = groupe(hote, 'Pilier dans le boîtier');
    lecture(g, function () { const d = E.d; return 'Pilier de Ø ' + fmt(2 * d.Rb, 2) + ' × ' + fmt(E.p.boitier.entretoise, 2) + ' mm, avant-trou de Ø ' + fmt(d.vis.avant, 2) + ' mm pour une vis ' + E.p.boitier.vis + ' × ' + d.longVis + '. Il suit le trou.'; });
    boutons(hote, [{ texte: 'Supprimer', danger: true, action: function () { supprimer({ type: 'trou', id: id }); } }]);
  }

  let sigVerifs = '';
  function majVerifs() {
    const err = E.pb.filter(function (q) { return q.sev === 'erreur'; }), al = E.pb.filter(function (q) { return q.sev === 'alerte'; });
    const niveau = err.length ? 'erreur' : al.length ? 'alerte' : 'ok';
    const badge = $('#badge');
    badge.textContent = E.pb.length ? String(err.length || al.length) : '';
    badge.dataset.niveau = niveau === 'ok' ? '' : niveau; badge.hidden = !E.pb.length;
    const etat = $('#etat');
    etat.dataset.niveau = niveau;
    etat.textContent = err.length ? err.length + (err.length > 1 ? ' erreurs' : ' erreur') : al.length ? al.length + (al.length > 1 ? ' alertes' : ' alerte') : 'Tout est cohérent';
    const sig = JSON.stringify(E.pb.map(function (q) { return [q.sev, q.msg, q.fix ? q.fix.libelle : '']; }));
    if (sig === sigVerifs) return;
    sigVerifs = sig;
    const hote = $('#p-verifs'); hote.textContent = '';
    if (!E.pb.length) {
      const v = el('div', 'vide');
      v.appendChild(el('strong', null, 'Tout est cohérent'));
      v.appendChild(el('span', null, 'La carte entre dans le boîtier, et les piliers, les découpes et les perçages tombent juste. Les vérifications tournent à chaque modification.'));
      hote.appendChild(v); return;
    }
    const ul = el('ul', 'verifs');
    for (const q of err.concat(al)) {
      const li = el('li'); li.dataset.sev = q.sev;
      const sym = el('span', 'sym', '!'); sym.setAttribute('aria-hidden', 'true'); li.appendChild(sym);
      const corps = el('div'), txt = el('p');
      const lecteur = el('span', null, q.sev === 'erreur' ? 'Erreur : ' : 'Alerte : ');
      lecteur.style.cssText = 'position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0)';
      txt.appendChild(lecteur); txt.appendChild(doc.createTextNode(q.msg)); corps.appendChild(txt);
      const r = el('div', 'rangee');
      const cible = q.ids.length ? cibleDe(q.ids[0]) : null;
      if (cible) { const b = el('button', 'btn', 'Montrer'); b.type = 'button'; b.addEventListener('click', function () { selectionner(cible); montrer(cible); }); r.appendChild(b); }
      if (q.fix) { const fx = q.fix, b = el('button', 'btn', fx.libelle); b.type = 'button'; b.addEventListener('click', function () { modifier(function (p) { P.corriger(p, fx); }); }); r.appendChild(b); }
      if (r.childNodes.length) corps.appendChild(r);
      li.appendChild(corps); ul.appendChild(li);
    }
    hote.appendChild(ul);
  }

  let cacheStats = { sig: '', v: null };
  function statistiques() {
    const d = E.d;
    const sig = JSON.stringify([d.Lo, d.Wo, d.ro, d.Li, d.Wi, d.ri, d.zf, d.zt, d.ztop, d.zpb, d.Rb, d.lo, d.li, d.cx, d.cy, P.decoupesValides(d), P.piliersValides(d), P.percagesValides(d)]);
    if (sig !== cacheStats.sig && T3) {
      cacheStats = { sig: sig, v: { volCorps: P.volume(P.construireCorps(E.p, d, T3, { seg: 8, segCercle: 28 })), volCouvercle: P.volume(P.construireCouvercle(E.p, d, T3, { seg: 8, segCercle: 28 })) } };
    }
    return cacheStats.v;
  }
  let nomenSale = true;
  function majNomenclature() {
    if (E.onglet !== 'nomenclature') { nomenSale = true; return; }
    nomenSale = false;
    const hote = $('#p-nomenclature'); hote.textContent = '';
    const st = statistiques();
    const lignes = P.nomenclature(E.p, E.d, st);
    const tab = el('table', 'nomen'), th = el('thead'), tr = el('tr');
    ['Repère', 'Désignation', 'Qté'].forEach(function (t, i) { const c = el('th', null, t); if (i === 2) c.style.textAlign = 'right'; tr.appendChild(c); });
    th.appendChild(tr); tab.appendChild(th);
    const tb = el('tbody'); let groupeCourant = '';
    for (const l of lignes) {
      if (l.groupe !== groupeCourant) { groupeCourant = l.groupe; const g = el('tr', 'g'), c = el('td', null, l.groupe); c.colSpan = 3; g.appendChild(c); tb.appendChild(g); }
      const r = el('tr');
      r.appendChild(el('td', null, l.ref));
      const ds = el('td', null, l.designation); ds.appendChild(el('span', 'src', l.appro)); r.appendChild(ds);
      const q = el('td', 'q', String(l.qte)); r.appendChild(q);
      tb.appendChild(r);
    }
    tab.appendChild(tb); hote.appendChild(tab);
    if (st) {
      const masse = (st.volCorps + st.volCouvercle) / 1000 * 1.24;
      hote.appendChild(el('p', 'note', 'Matière imprimée : ' + fmt((st.volCorps + st.volCouvercle) / 1000) + ' cm³, soit ' + fmt(masse, 0) + ' g de PLA au plus, pièces pleines.'));
    }
    hote.appendChild(el('p', 'note', 'Une seule nomenclature pour l’électronique, la visserie et les pièces fabriquées, tirée du même modèle. Le répertoire fournisseurs est encore vide.'));
    if (window.__exportPossible) boutons(hote, [{ texte: 'Exporter la nomenclature (.csv)', action: exporterNomenclature }]);
  }

  // ---------- Onglets, tiroir, vues ----------
  function ouvrirOnglet(nom, deplier) {
    E.onglet = nom;
    ['proprietes', 'verifs', 'nomenclature'].forEach(function (n) {
      $('#t-' + n).setAttribute('aria-selected', String(n === nom));
      $('#p-' + n).hidden = n !== nom;
    });
    if (nom === 'nomenclature' && nomenSale) majNomenclature();
    if (deplier && mobile.matches) $('#panneau').dataset.ouvert = 'true';
  }
  function changerVue(v) {
    E.vue = v;
    $('#espace').dataset.vue = v;
    doc.querySelectorAll('#seg-vues button').forEach(function (b) { b.setAttribute('aria-pressed', String(b.dataset.vue === v)); });
    requestAnimationFrame(function () { taille2D(); taille3D(); });
  }
  function recadrerTout() { recadrer2D(); recadrer3D(); }
  let dimsVues = '';
  function garderEnVue() {
    if (!E.d) return;
    const d = E.d, dims = [d.Lo, d.Wo, d.ztop, d.cx, d.cy].map(function (v) { return v.toFixed(2); }).join(' ');
    if (dims === dimsVues) return;
    const premier = !dimsVues; dimsVues = dims;
    if (premier) return;
    if (V.pret) {
      const m = E.cotes ? 46 : 6;
      if (sx(d.cx - d.Lo / 2) - m < 0 || sx(d.cx + d.Lo / 2) + 6 > V.w || sy(d.cy + d.Wo / 2) - 6 < 44 || sy(d.cy - d.Wo / 2) + m > V.h) recadrer2D();
    }
    if (R.ok && R.w > 4 && R.h > 4) {
      const zmax = d.ztop + (E.couvercle === 'souleve' ? hauteurSoulevee() : 0), v = new T3.Vector3();
      let deborde = false;
      for (const sx3 of [-1, 1]) for (const sy3 of [-1, 1]) for (const z of [0, zmax]) {
        v.set(d.cx + sx3 * d.Lo / 2, d.cy + sy3 * d.Wo / 2, z).project(R.cam);
        if (Math.abs(v.x) > 0.97 || Math.abs(v.y) > 0.94 || v.z > 1) deborde = true;
      }
      if (deborde) recadrer3D();
    }
  }
  function basculer(quoi) {
    E[quoi] = !E[quoi];
    $(quoi === 'cotes' ? '#o-cotes' : '#o-magnet').setAttribute('aria-pressed', String(E[quoi]));
    const item = doc.querySelector('#menu-plus [data-action="' + quoi + '"]');
    if (item) { item.setAttribute('aria-checked', String(E[quoi])); item.querySelector('[data-etat]').textContent = E[quoi] ? 'oui' : 'non'; }
    if (quoi === 'cotes') planifier({ d2: true });
    else toast(E.magnet ? 'Magnétisme activé : les positions s’alignent sur 0,5 mm.' : 'Magnétisme désactivé : positions libres.');
  }

  // ---------- Menus et boutons ----------
  const fermetures = [];
  function fermerMenus() { fermetures.forEach(function (f) { f(); }); }
  function brancherMenu(bouton, menu) {
    function fermer() { menu.hidden = true; bouton.setAttribute('aria-expanded', 'false'); }
    fermetures.push(fermer);
    bouton.addEventListener('click', function (e) {
      e.stopPropagation();
      const ouvrir = menu.hidden;
      fermerMenus();
      if (ouvrir) { menu.hidden = false; bouton.setAttribute('aria-expanded', 'true'); const p = menu.querySelector('button:not([hidden])'); if (p) p.focus(); }
    });
    menu.addEventListener('click', function (e) { e.stopPropagation(); });
    menu.addEventListener('keydown', function (e) {
      const items = Array.from(menu.querySelectorAll('button:not([hidden])')), i = items.indexOf(doc.activeElement);
      if (e.key === 'ArrowDown') { e.preventDefault(); items[(i + 1) % items.length].focus(); }
      else if (e.key === 'ArrowUp') { e.preventDefault(); items[(i - 1 + items.length) % items.length].focus(); }
      else if (e.key === 'Escape') { e.stopPropagation(); fermer(); bouton.focus(); }
    });
  }
  function construireMenuAjout() {
    const m = $('#menu-ajouter');
    const entrees = [['trou', 'Trou de fixation', 'suit un coin'], null, ['usbc', 'Connecteur USB-C', 'perce la paroi'], ['jack', 'Jack d’alimentation', 'perce la paroi'],
      ['led', 'LED 3 mm', 'perce le couvercle'], ['bouton', 'Bouton poussoir', 'perce le couvercle'], null,
      ['module', 'Module radio'], ['ic', 'Circuit intégré'], ['capteur', 'Capteur'], ['jst', 'Connecteur JST-PH'], ['condo', 'Condensateur']];
    for (const e of entrees) {
      if (!e) { m.appendChild(el('hr')); continue; }
      const b = el('button'); b.type = 'button'; b.setAttribute('role', 'menuitem');
      b.appendChild(doc.createTextNode(e[1])); if (e[2]) b.appendChild(el('small', null, e[2]));
      b.addEventListener('click', function () { fermerMenus(); ajouter(e[0]); });
      m.appendChild(b);
    }
  }
  function aide() {
    ouvrirModale('Comment ça marche', [
      'La carte et le boîtier sont un seul modèle. Les piliers suivent les trous de fixation, les découpes suivent les connecteurs, les perçages du couvercle suivent les LED et les boutons, et la hauteur du boîtier suit le composant le plus haut.',
      'Vue Carte : glisse un composant, un trou, ou une poignée du bord de la carte après l’avoir touchée. Pince ou utilise la molette pour zoomer, glisse dans le vide pour te déplacer, touche deux fois un composant pour le pivoter.',
      'Vue Boîtier : glisse dans le vide pour tourner autour, pince ou utilise la molette pour zoomer, clic droit ou Maj pour te déplacer. Tire une paroi pour agrandir le boîtier, la carte suit. Glisse un pilier ou un composant pour le déplacer.',
      'Les vérifications tournent à chaque geste. Le dossier de fabrication rassemble les fichiers STL prêts à imprimer, le contour de la carte en DXF pour KiCad, un plan coté, la nomenclature et le projet.',
      'Raccourcis : Ctrl+Z pour annuler, Ctrl+Maj+Z pour rétablir, flèches pour décaler de 0,5 mm (5 mm avec Maj), R pour pivoter, Suppr pour supprimer, Échap pour tout désélectionner.'
    ], [{ texte: 'Fermer', valeur: true, primaire: true }]);
  }
  function brancherInterface() {
    $('#b-annuler').addEventListener('click', annuler);
    $('#b-retablir').addEventListener('click', retablir);
    construireMenuAjout();
    brancherMenu($('#b-ajouter'), $('#menu-ajouter'));
    brancherMenu($('#b-plus'), $('#menu-plus'));
    doc.addEventListener('click', fermerMenus);
    $('#menu-plus').addEventListener('click', function (e) {
      const b = e.target.closest ? e.target.closest('button[data-action]') : null; if (!b) return;
      fermerMenus();
      const a = b.dataset.action;
      if (a === 'nouveau') remplacer(P.projetVide(), 'Nouveau projet. Annuler pour revenir au précédent.');
      else if (a === 'exemple') remplacer(P.exemple(), 'Exemple rechargé. Annuler pour revenir au précédent.');
      else if (a === 'ouvrir') ouvrirProjet();
      else if (a === 'enregistrer') exporterProjet();
      else if (a === 'csv') exporterNomenclature();
      else if (a === 'aide') aide();
      else if (a === 'cotes' || a === 'magnet') basculer(a);
    });
    $('#fichier').addEventListener('change', function (e) { lireFichier(e.target.files && e.target.files[0]); });
    $('#b-dossier').addEventListener('click', exporterDossier);
    $('#o-cotes').addEventListener('click', function () { basculer('cotes'); });
    $('#o-magnet').addEventListener('click', function () { basculer('magnet'); });
    $('#o-cadre2d').addEventListener('click', recadrer2D);
    $('#o-cadre3d').addEventListener('click', recadrer3D);
    $('#o-transp').addEventListener('click', function () { E.transparence = !E.transparence; this.setAttribute('aria-pressed', String(E.transparence)); planifier({ d3: true }); });
    doc.querySelectorAll('#seg-couvercle button').forEach(function (b) {
      b.addEventListener('click', function () {
        E.couvercle = b.dataset.mode;
        doc.querySelectorAll('#seg-couvercle button').forEach(function (x) { x.setAttribute('aria-pressed', String(x === b)); });
        planifier({ d3: true });
      });
    });
    doc.querySelectorAll('#seg-vues button').forEach(function (b) { b.addEventListener('click', function () { changerVue(b.dataset.vue); }); });
    doc.querySelectorAll('.onglets button').forEach(function (b) {
      b.addEventListener('click', function () {
        const pan = $('#panneau'), nom = b.dataset.onglet;
        if (mobile.matches) {
          if (pan.dataset.ouvert !== 'true') { pan.dataset.ouvert = 'true'; ouvrirOnglet(nom); }
          else if (E.onglet === nom) pan.dataset.ouvert = 'false';
          else ouvrirOnglet(nom);
        } else ouvrirOnglet(nom);
      });
    });
    $('.poignee').addEventListener('click', function () { const pan = $('#panneau'); pan.dataset.ouvert = pan.dataset.ouvert === 'true' ? 'false' : 'true'; });
    $('#etat').addEventListener('click', function () { ouvrirOnglet('verifs', true); });
    if (lireStockage(CLE_ASTUCE) !== 'vu') $('#astuce').hidden = false;
    $('#b-astuce').addEventListener('click', function () { $('#astuce').hidden = true; ecrireStockage(CLE_ASTUCE, 'vu'); });
    doc.addEventListener('keydown', function (e) {
      const t = e.target, tag = t && t.tagName;
      if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA' || (t && t.isContentEditable)) return;
      if (!$('#modale').hidden) return;
      const mod = e.ctrlKey || e.metaKey;
      if (mod && (e.key === 'z' || e.key === 'Z')) { e.preventDefault(); if (e.shiftKey) retablir(); else annuler(); return; }
      if (mod && (e.key === 'y' || e.key === 'Y')) { e.preventDefault(); retablir(); return; }
      if (mod || e.altKey) return;
      const s = E.sel;
      if (e.key === 'Escape') { fermerMenus(); selectionner(null); return; }
      if ((e.key === 'Delete' || e.key === 'Backspace') && s && s.type !== 'carte') { e.preventDefault(); supprimer(s); return; }
      if ((e.key === 'r' || e.key === 'R') && s && s.type === 'comp') { e.preventDefault(); pivoter(s.id); return; }
      if (e.key.indexOf('Arrow') === 0 && s && (s.type === 'comp' || s.type === 'trou')) {
        e.preventDefault();
        const pas = e.shiftKey ? 5 : 0.5;
        const dx = e.key === 'ArrowLeft' ? -pas : e.key === 'ArrowRight' ? pas : 0, dy = e.key === 'ArrowDown' ? -pas : e.key === 'ArrowUp' ? pas : 0;
        const pos = positionObjet(s);
        modifier(function () { bougerModele(s, pos.x + dx, pos.y + dy, true); });
        majPied();
      }
    });
    const themes = window.matchMedia ? window.matchMedia('(prefers-color-scheme: dark)') : null;
    const surTheme = function () { lireJetons(); planifier({ d2: true }); };
    if (themes) { if (themes.addEventListener) themes.addEventListener('change', surTheme); else if (themes.addListener) themes.addListener(surTheme); }
    if (window.MutationObserver) new MutationObserver(surTheme).observe(doc.documentElement, { attributes: true, attributeFilter: ['data-theme', 'class', 'style'] });
    if (doc.fonts && doc.fonts.ready) doc.fonts.ready.then(function () { planifier({ d2: true }); });
    if (window.ResizeObserver) {
      new ResizeObserver(function () { taille2D(); }).observe(cv);
      new ResizeObserver(function () { taille3D(); }).observe($('#c3d'));
    }
    window.addEventListener('resize', function () { taille2D(); taille3D(); });
  }

  function demarrer() {
    E.p = projetInitial();
    E.d = P.deriver(E.p); E.pb = P.verifier(E.p, E.d);
    lireJetons();
    brancher2D();
    etape(init3D);
    brancherInterface();
    taille2D(); taille3D();
    changer({ panneau: true });
    obtenirTelech().then(function (t) { afficherExports(!!t); });
  }
  window.PrometheeUI = { etat: E, vue2D: V, vue3D: R, camera: CAM };
  if (doc.readyState === 'loading') doc.addEventListener('DOMContentLoaded', demarrer); else demarrer();
})();
