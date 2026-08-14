'use strict';
/*
 * USBSID-Pico is a RPi Pico/PicoW (RP2040) & Pico2/Pico2W (RP2350) based board
 * for interfacing one or two MOS SID chips and/or hardware SID emulators over
 * (WEB)USB with your computer, phone or ASID supporting player
 *
 * usbsid-sidbrowser.js
 * This file is part of USBSID-Pico (https://github.com/LouDnl/USBSID-Pico)
 * File author: LouD
 *
 * Copyright (c) 2024-2026 LouD
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The SID library browser.
 *
 * Two panes. Directories on the left as a tree you walk into, the current
 * directory's tunes on the right grouped by SID type, with a row of filter
 * buttons over them. Under 700px the two stack and the directory list stays a
 * list: a tree is walked one level at a time, each level is a handful of rows,
 * and on a phone tapping a row is the obvious thing to do. It was a <select>
 * here at first, from when the pane held every directory flat.
 *
 * It reads SID/sidfilelist.json, written by SID/filelist.bb. If that is not
 * there it falls back to the old flat sidfilelist.txt, so a deployment that has
 * not run the generator yet still shows a library, only without the types and
 * without the search over titles and authors.
 *
 * Two things here exist because the library is large. A full HVSC is 65 000
 * tunes in 400 directories, and the naive version of each was unusable:
 *
 *   The directory list was every directory, flat, including every subdirectory
 *   of every set: hundreds of rows of `HVSC85/DEMOS/UNKNOWN/Master_Composer`
 *   to scroll past. It is a tree now, one level at a time.
 *
 *   The file list rendered every row it had. 64 372 tunes came to 194 380 DOM
 *   nodes, which is seconds of layout on a desktop and much worse on a phone.
 *   Rows are rendered a screenful at a time and appended as you scroll.
 */

const SidBrowser = (function () {

  /* Neither is a directory on the server, so both get names no real directory
   * can collide with. */
  const LOCAL_DIR = '★ local';
  const ALL_DIR   = '★ all';

  /* How many rows go into the DOM at once, and how close to the bottom the
   * scroll has to get before the next batch is added. 120 rows is more than a
   * tall desktop pane shows, so the list never looks short. */
  const PAGE      = 120;
  const NEAR_END  = 400;   /* px */

  let _index   = null;   /* the parsed sidfilelist.json */
  let _dirs    = [];     /* [{ dir, count, groups: [{ type, count, files }] }] */
  let _byDir   = null;   /* dir name -> that entry, for the tree */
  let _tree    = null;   /* the directory tree, see buildTree() */
  let _path    = [];     /* where we are in the tree, as segments */
  let _view    = [];     /* the tunes currently listed, in display order */
  let _display = [];     /* the same, with group headings interleaved */
  let _drawn   = 0;      /* how much of _display is in the DOM */
  let _openDir = null;   /* which directory's files are shown, by name */
  let _group   = null;   /* type filter in force, or null for all of them */
  let _query   = '';
  let _current = -1;     /* index into _view of what is loaded, -1 for none */
  let _local   = [];     /* local playlist entries, see setLocalFiles() */
  let _all     = null;   /* every tune, flat, built once for searching */
  let _ready   = false;
  let _searchTimer = 0;
  /* Shuffle plays the list that is on screen, in a random order, once each.
   * `_order` is that order as indices into `_view`, and `_orderPos` is how far
   * through it we are. Cleared whenever the list changes, since an order over
   * the previous directory means nothing here. */
  let _shuffle = false;
  let _order = [];
  let _orderPos = -1;

  /* ------------------------------------------------------------- helpers -- */

  const $ = (id) => document.getElementById(id);
  const ci = (s) => (s || '').toLowerCase();

  function log(...a) {
    if (typeof usbsidLog === 'function') usbsidLog(...a);
    else console.log('[SidBrowser]', ...a);
  }

  /* The group a tune belongs to inside its directory. An RSID is a real C64
   * program rather than a driver loaded tune, which is what a digi tune is, so
   * it groups by that whatever its chip count. The row still shows a 2SID badge
   * beside the author, so a two chip digi tune does not lose that.
   *
   * Kept in step with type-label in SID/filelist.bb, which writes the same
   * labels into the JSON; this copy is for the flat list fallback and for the
   * local playlist, neither of which comes through the generator. */
  const DIGITUNES = 'Digitunes';
  const TYPE_RANK = { '1SID': 1, '2SID': 2, '3SID': 3, '4SID': 4, 'Digitunes': 5 };

  function typeLabel(e) {
    if (typeof e === 'number') return (e || 1) + 'SID';
    return (e && e.t === 'RSID') ? DIGITUNES : ((e && e.s) || 1) + 'SID';
  }

  function typeRank(label) {
    return TYPE_RANK[label] !== undefined ? TYPE_RANK[label] : 9;
  }

  /* What the search box looks at: the file name, the tune's title and its
   * author. Built once per entry rather than per keystroke, since 65 000
   * entries times three fields on every input event is work for nothing.
   *
   * The release field and the path used to be in here too, and that is what
   * made a search for "Commando" return Alpha, Big Red and Bolero: their
   * release is `1987 Commando Frontier` and `1986 The Commandos`. Real matches,
   * but on a field the row does not show, so the result looked like nonsense.
   * A search should be explicable from what is on screen. */
  function searchKey(e) {
    if (e._k === undefined) {
      e._k = ci([e.n, e.ti, e.au].filter(Boolean).join(' '));
    }
    return e._k;
  }

  /* --------------------------------------------------------------- data -- */

  /**
   * The old flat list, as entries.
   *
   * `<-- Single SID -->` style headings become the chip count, which is the best
   * this format can do. A file appearing under two headings, which the old
   * generator did produce, is kept once.
   */
  function parseFlatList(text) {
    const seen = new Set();
    const out = [];
    let sids = 1;
    for (const raw of text.split(/\r?\n/)) {
      const line = raw.trim();
      if (!line) continue;
      if (line.startsWith('<--')) {
        sids = /dual/i.test(line) ? 2 : /triple/i.test(line) ? 3
             : /quad/i.test(line) ? 4 : 1;
        continue;
      }
      if (!/\.sid$/i.test(line) || seen.has(line)) continue;
      seen.add(line);
      const slash = line.lastIndexOf('/');
      out.push({
        n: slash < 0 ? line : line.slice(slash + 1),
        p: line,
        d: slash < 0 ? '.' : line.slice(0, slash),
        t: '', s: sids, ti: '', au: '', re: '',
      });
    }
    return out;
  }

  /** Group a flat entry list the way the JSON index already is. */
  function groupEntries(entries) {
    const byDir = new Map();
    for (const e of entries) {
      if (!byDir.has(e.d)) byDir.set(e.d, []);
      byDir.get(e.d).push(e);
    }
    const dirs = [];
    for (const [dir, es] of byDir) {
      const byType = new Map();
      for (const e of es) {
        const label = typeLabel(e);
        if (!byType.has(label)) byType.set(label, []);
        byType.get(label).push(e);
      }
      const groups = Array.from(byType, ([type, files]) => ({
        type,
        count: files.length,
        files: files.sort((a, b) => ci(a.n).localeCompare(ci(b.n))),
      })).sort((a, b) => typeRank(a.type) - typeRank(b.type));
      dirs.push({ dir, count: es.length, groups });
    }
    return dirs.sort((a, b) => ci(a.dir).localeCompare(ci(b.dir)));
  }

  async function loadIndex() {
    const base = (typeof SID_PATH === 'string') ? SID_PATH : 'SID/';
    try {
      const resp = await fetch(base + 'sidfilelist.json');
      if (resp.ok) {
        _index = await resp.json();
        _dirs = _index.dirs || [];
        log('SID library:', _index.count, 'tunes in', _dirs.length, 'directories');
        return true;
      }
    } catch (_) { /* fall through to the flat list */ }

    try {
      const resp = await fetch(base + 'sidfilelist.txt');
      if (!resp.ok) return false;
      _dirs = groupEntries(parseFlatList(await resp.text()));
      const n = _dirs.reduce((a, d) => a + d.count, 0);
      log('SID library:', n, 'tunes, from the flat list.',
          'Run SID/filelist.bb for types, titles and authors.');
      return true;
    } catch (e) {
      log('SID library not available:', e && e.message ? e.message : e);
      return false;
    }
  }

  /* ---------------------------------------------------------------- tree -- */

  /**
   * The directories as a tree, from their paths.
   *
   * `HVSC85/DEMOS/0-9` is three levels, not one name. Listing every such path
   * flat gave hundreds of rows on a full HVSC, most of them subdirectories of
   * something the user had not asked about. A node carries:
   *
   *   name      the one segment
   *   full      the whole path, which is the key into _byDir
   *   kids      child nodes by name
   *   own       how many tunes are in this directory itself
   *   total     how many are in it and everything under it
   */
  function buildTree() {
    const root = { name: '', full: '', kids: new Map(), own: 0, total: 0 };
    _byDir = new Map();
    for (const d of _dirs) {
      _byDir.set(d.dir, d);
      const segs = (d.dir === '.' || d.dir === '')
                 ? []
                 : d.dir.split('/').filter(Boolean);
      let node = root;
      node.total += d.count;
      let full = '';
      for (const seg of segs) {
        full = full ? (full + '/' + seg) : seg;
        if (!node.kids.has(seg)) {
          node.kids.set(seg, {
            name: seg, full, kids: new Map(), own: 0, total: 0,
          });
        }
        node = node.kids.get(seg);
        node.total += d.count;
      }
      node.own += d.count;
    }
    return root;
  }

  /** The node at `_path`, or the root when the path has gone stale. */
  function nodeAt(path) {
    let node = _tree;
    for (const seg of path) {
      if (!node || !node.kids.has(seg)) return _tree;
      node = node.kids.get(seg);
    }
    return node || _tree;
  }

  /** Child directories of a node, sorted, as rows for the pane. */
  function childrenOf(node) {
    return Array.from(node.kids.values())
      .sort((a, b) => ci(a.name).localeCompare(ci(b.name)));
  }

  /* ---------------------------------------------------------- selection -- */

  /**
   * The whole served library as one directory, grouped by type.
   *
   * The local playlist is deliberately not in it: it is the user's own files
   * and folding it in would make "all" mean something different depending on
   * whether a folder happened to be open.
   *
   * Built once and kept: on a full HVSC this merges 65 000 entries and doing it
   * per render was the second most expensive thing here.
   */
  function allDir() {
    if (allDir._cache) return allDir._cache;
    const byType = new Map();
    let count = 0;
    for (const d of _dirs) {
      for (const g of d.groups) {
        if (!byType.has(g.type)) byType.set(g.type, []);
        const into = byType.get(g.type);
        for (const f of g.files) { into.push(f); count++; }
      }
    }
    const groups = Array.from(byType, ([type, files]) => ({
      type,
      count: files.length,
      files: files.sort((a, b) => ci(a.n).localeCompare(ci(b.n))),
    })).sort((a, b) => typeRank(a.type) - typeRank(b.type));
    allDir._cache = { dir: ALL_DIR, count, groups, all: true };
    return allDir._cache;
  }

  function localDir() {
    if (!_local.length) return null;
    /* Every local entry carries the same `d`, so this is one directory. */
    const grouped = groupEntries(_local);
    return {
      dir: LOCAL_DIR,
      count: _local.length,
      local: true,
      groups: grouped.length ? grouped[0].groups : [],
    };
  }

  function dirByName(name) {
    if (name === ALL_DIR) return allDir();
    if (name === LOCAL_DIR) return localDir();
    return (_byDir && _byDir.get(name)) || null;
  }

  /** Every tune, flat, for searching across the whole library. Built once. */
  function everything() {
    if (!_all) {
      _all = [];
      for (const d of _dirs) {
        for (const g of d.groups) for (const f of g.files) _all.push(f);
      }
    }
    return _local.length ? _local.concat(_all) : _all;
  }

  /* ------------------------------------------------------------- render -- */

  function dirRow(label, count, onClick, extraClass) {
    const row = document.createElement('button');
    row.type = 'button';
    row.className = 'sid-dir' + (extraClass ? ' ' + extraClass : '');
    row.innerHTML =
      '<span class="sid-dir-name"></span><span class="sid-dir-count"></span>';
    row.querySelector('.sid-dir-name').textContent = label;
    row.querySelector('.sid-dir-count').textContent =
      (count === null || count === undefined) ? '' : count;
    row.addEventListener('click', onClick);
    return row;
  }

  function renderDirPane() {
    const pane = $('sid-dir-list');
    const crumb = $('sid-crumb');
    if (!pane) return;

    const node = nodeAt(_path);
    const kids = childrenOf(node);
    pane.innerHTML = '';

    /* Where we are, and a way back to the top. */
    if (crumb) {
      crumb.textContent = _path.length ? ('/' + _path.join('/')) : '';
      crumb.style.display = _path.length ? '' : 'none';
    }

    /* At the top: the two pseudo directories. Below the top: a way up. */
    if (!_path.length) {
      const local = localDir();
      if (local) {
        pane.appendChild(dirRow(LOCAL_DIR, local.count, () => openDir(LOCAL_DIR),
                                _openDir === LOCAL_DIR ? 'sid-dir-open' : ''));
      }
      if (_dirs.length) {
        const all = allDir();
        pane.appendChild(dirRow(ALL_DIR, all.count, () => openDir(ALL_DIR),
                                _openDir === ALL_DIR ? 'sid-dir-open' : ''));
      }
    } else {
      pane.appendChild(dirRow('.. up', null, () => leaveDir(), 'sid-dir-up'));
    }

    /* This directory's own tunes, when it has any as well as children. Without
     * a row of its own they would be unreachable from a node that also has
     * subdirectories. */
    if (node.own > 0 && _path.length) {
      const full = node.full;
      pane.appendChild(dirRow('. this directory', node.own, () => openDir(full),
                              _openDir === full ? 'sid-dir-open' : ''));
    }

    for (const kid of kids) {
      const label = kid.name + (kid.kids.size ? '/' : '');
      pane.appendChild(dirRow(label, kid.total, () => enterDir(kid),
                              (_openDir === kid.full && !kid.kids.size)
                                ? 'sid-dir-open' : ''));
    }

  }

  /** The row for one tune. */
  function fileRow(entry, idx) {
    const row = document.createElement('button');
    row.type = 'button';
    row.className = 'sid-file' + (idx === _current ? ' sid-file-on' : '');
    row.dataset.idx = idx;

    const name = document.createElement('span');
    name.className = 'sid-file-title';
    /* The title from the header when there is one: it is what the tune is
     * called, where the file name is what someone saved it as. */
    name.textContent = entry.ti || entry.n.replace(/\.sid$/i, '');
    row.appendChild(name);

    const sub = document.createElement('span');
    sub.className = 'sid-file-sub';
    const bits = [];
    if (entry.au) bits.push(entry.au);
    /* Where it was found. Worth saying when the list is not one directory. */
    if ((_query || _openDir === ALL_DIR) && entry.d) bits.push(entry.d);
    if ((entry.s || 1) > 1) bits.push(typeLabel(entry.s));
    if (entry.t === 'RSID') bits.push('RSID');
    sub.textContent = bits.join('  ·  ') || entry.n;
    row.appendChild(sub);

    row.addEventListener('click', () => pick(idx));
    return row;
  }

  /**
   * Put the next batch of rows into the DOM.
   *
   * The list can be 65 000 long and every row is three elements, so rendering
   * all of it is 195 000 nodes and seconds of layout. `_display` holds what the
   * list *is*; this decides how much of it exists as HTML.
   */
  function draw(upTo) {
    const pane = $('sid-file-list');
    if (!pane) return;
    const want = Math.min(_display.length,
                          (upTo === undefined) ? _drawn + PAGE : upTo);
    const frag = document.createDocumentFragment();
    for (let i = _drawn; i < want; i++) {
      const item = _display[i];
      if (item.h !== undefined) {
        const head = document.createElement('div');
        head.className = 'sid-group-head';
        head.textContent = item.h;
        frag.appendChild(head);
      } else {
        frag.appendChild(fileRow(item.e, item.i));
      }
    }
    _drawn = want;
    /* The "more to come" marker goes after the batch, and moves with it. */
    const old = pane.querySelector('.sid-more');
    if (old) old.remove();
    pane.appendChild(frag);
    if (_drawn < _display.length) {
      const more = document.createElement('div');
      more.className = 'sid-more';
      more.textContent = _drawn + ' of ' + _view.length + ', scroll for more';
      pane.appendChild(more);
    }
  }

  /** Render far enough that the row for `idx` exists, for PREV and NEXT. */
  function ensureDrawn(idx) {
    if (idx < 0) return;
    for (let i = 0; i < _display.length; i++) {
      if (_display[i].i === idx) {
        if (i >= _drawn) draw(i + PAGE);
        return;
      }
    }
  }

  function onPaneScroll() {
    const pane = $('sid-file-list');
    if (!pane || _drawn >= _display.length) return;
    if (pane.scrollTop + pane.clientHeight + NEAR_END >= pane.scrollHeight) {
      draw();
    }
  }

  function renderFilePane() {
    const pane = $('sid-file-list');
    const chips = $('sid-group-chips');
    const count = $('sid-list-count');
    if (!pane) return;

    pane.innerHTML = '';
    if (chips) chips.innerHTML = '';
    pane.scrollTop = 0;
    _view = [];
    _display = [];
    _drawn = 0;
    _order = [];
    _orderPos = -1;

    /* Searching looks everywhere, on purpose: a search that only covered the
     * open directory would make the user walk the whole tree to find a tune. */
    if (_query) {
      const q = ci(_query);
      const hits = everything().filter(e => searchKey(e).includes(q));
      hits.sort((a, b) => ci(a.n).localeCompare(ci(b.n)));
      _view = hits;
      if (count) count.textContent = '(' + hits.length + ' found)';
      if (!hits.length) {
        const none = document.createElement('div');
        none.className = 'sid-empty';
        none.textContent = 'Nothing matches "' + _query + '"';
        pane.appendChild(none);
        return;
      }
      hits.forEach((e, i) => _display.push({ e, i }));
      draw();
      return;
    }

    const d = dirByName(_openDir);
    if (!d) {
      const none = document.createElement('div');
      none.className = 'sid-empty';
      none.textContent = _path.length || _dirs.length
        ? 'Choose a directory'
        : 'No library';
      pane.appendChild(none);
      if (count) count.textContent = '';
      return;
    }

    /* The type buttons filter, they do not scroll.
     *
     * They were jump buttons and that was wrong twice over. They were read as
     * a filter, and they could only ever jump *forwards*: `offsetTop` on a
     * `position: sticky` header returns its stuck position, not its layout
     * position, so once a header was stuck to the top of the pane its offsetTop
     * equalled the current scrollTop and setting scrollTop to it did nothing.
     * Filtering has no such trap and needs no scrolling on a phone. */
    const groups = (_group === null)
      ? d.groups
      : d.groups.filter(g => g.type === _group);

    const shown = groups.reduce((n, g) => n + g.count, 0);
    if (count) {
      count.textContent = (_group === null)
        ? '(' + d.count + ' files)'
        : '(' + shown + ' of ' + d.count + ')';
    }

    /* A directory with one group needs no filter: the only button would be the
     * one already in force, beside an ALL that does the same thing. */
    if (chips && d.groups.length > 1) {
      const addChip = (label, value) => {
        const chip = document.createElement('button');
        chip.type = 'button';
        chip.className = 'c64-btn c64-btn-sm sid-chip' +
                         (_group === value ? ' sid-chip-on' : '');
        chip.textContent = label;
        chip.addEventListener('click', () => {
          /* Clicking the one already in force clears it, so a filter can
           * always be undone with the button that set it. */
          _group = (_group === value) ? null : value;
          renderFilePane();
        });
        chips.appendChild(chip);
      };
      addChip('ALL', null);
      for (const g of d.groups) addChip(g.type, g.type);
    }

    for (const g of groups) {
      /* The heading stays even when filtered to one group: it carries the
       * count, and without it a filtered list has nothing saying what it is. */
      if (d.groups.length > 1) _display.push({ h: g.type + '  (' + g.count + ')' });
      for (const e of g.files) {
        const i = _view.length;
        _view.push(e);
        _display.push({ e, i });
      }
    }
    draw();
  }

  function render() {
    renderDirPane();
    renderFilePane();
  }

  /** Repaint just the selected row, which is all that changes when playing. */
  function markCurrent() {
    const pane = $('sid-file-list');
    if (!pane) return;
    ensureDrawn(_current);
    for (const row of pane.querySelectorAll('.sid-file')) {
      row.classList.toggle('sid-file-on',
                           Number(row.dataset.idx) === _current);
    }
    /* offsetTop is pane relative: the pane is `position: relative`, which makes
     * it the offsetParent of its contents. */
    const on = pane.querySelector('.sid-file-on');
    if (on && on.offsetTop < pane.scrollTop) {
      pane.scrollTop = on.offsetTop;
    } else if (on && on.offsetTop + on.offsetHeight >
                     pane.scrollTop + pane.clientHeight) {
      pane.scrollTop = on.offsetTop - pane.clientHeight + on.offsetHeight;
    }
  }

  /* -------------------------------------------------------------- action -- */

  /** Walk into a subdirectory. Its own tunes are shown when it has any. */
  function enterDir(node) {
    _path = node.full.split('/').filter(Boolean);
    _group = null;
    clearSearchBox();
    /* A leaf's tunes are what the user wanted. A node with children as well
     * shows them too, since there is nothing else to look at. */
    _openDir = (node.own > 0) ? node.full : null;
    remember();
    render();
  }

  function leaveDir() {
    _path = _path.slice(0, -1);
    _group = null;
    clearSearchBox();
    const node = nodeAt(_path);
    _openDir = (_path.length && node.own > 0) ? node.full : null;
    remember();
    render();
  }

  /** Show one directory's tunes without moving in the tree. */
  function openDir(name) {
    _openDir = name;
    /* A 3SID filter carried into a directory with no 3SID files would show an
     * empty list and no obvious reason for it. */
    _group = null;
    clearSearchBox();
    if (name !== ALL_DIR && name !== LOCAL_DIR) {
      _path = name.split('/').filter(Boolean);
    }
    remember();
    render();
  }

  function clearSearchBox() {
    _query = '';
    const box = $('sid-search');
    if (box) box.value = '';
  }

  function remember() {
    try {
      localStorage.setItem('usbsid_sid_dir', _openDir || '');
      localStorage.setItem('usbsid_sid_path', _path.join('/'));
    } catch (_) {}
  }

  function search(q) {
    _query = (q || '').trim();
    /* Search looks across every directory, so a type filter belonging to one
     * of them has nothing to mean here. */
    if (_query) _group = null;
    renderFilePane();
  }

  /**
   * Load the tune at `idx` in the current view.
   *
   * A library tune is a URL under SID/. A local one is a File the page was
   * handed, which has no URL until one is made for it, and the old one is
   * revoked here rather than left to accumulate over a long listening session.
   */
  async function pick(idx) {
    if (idx < 0 || idx >= _view.length) return;
    _current = idx;
    markCurrent();
    const e = _view[idx];

    /* `e.l` is the milliseconds per song out of sidfilelist.json, when the
     * generator found the tune in songlengths.md5. A local file has none and
     * the app looks it up from the bytes after loading. */
    if (e.file) {
      if (pick._url) URL.revokeObjectURL(pick._url);
      pick._url = URL.createObjectURL(e.file);
      await doLoadSID(pick._url, e.n, undefined, null);
      return;
    }
    /* Percent encode the path, segment by segment.
     *
     * 294 of the tunes here have a space, an ampersand, a bracket or a letter
     * with a diacritic in their name: `Kickin' Balls.sid`, `Chris Hülsbeck_-_
     * Compilation_III.sid`, `Hakodate_Sunrise_[8580].sid`. Concatenated raw,
     * those are not valid URLs, and while a browser repairs some of them it is
     * not something to depend on. Every one of them fetches correctly encoded.
     *
     * Per segment, so the separators survive: encodeURIComponent would turn the
     * slashes into %2F and ask the server for one very oddly named file. */
    const base = (typeof SID_PATH === 'string') ? SID_PATH : 'SID/';
    const url = base + e.p.split('/').map(encodeURIComponent).join('/');
    await doLoadSID(url, e.n, undefined, e.l || null);
  }

  /**
   * A random order over the current list, every tune once.
   *
   * A shuffle that picks a random index each time replays tunes it has just
   * played and misses others entirely; over a directory of thirty that is
   * noticeable within a minute. This is a Fisher-Yates shuffle of the whole
   * list, walked in order and reshuffled when it runs out, so everything is
   * heard once per pass.
   */
  function ensureOrder() {
    if (_order.length === _view.length && _order.length) return;
    _order = _view.map((_, i) => i);
    for (let i = _order.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      const t = _order[i]; _order[i] = _order[j]; _order[j] = t;
    }
    /* Carry on from whatever is playing rather than jumping elsewhere the
     * moment shuffle is switched on. */
    _orderPos = _order.indexOf(_current);
  }

  /** PREV and NEXT walk what is on screen, which is what the user is looking at. */
  async function step(delta) {
    if (!_view.length) return;

    if (_shuffle) {
      ensureOrder();
      _orderPos += (delta >= 0 ? 1 : -1);
      if (_orderPos >= _order.length) {
        /* A pass is done. Reshuffle so the next one is a different order, not
         * the same sequence again. */
        _order = [];
        ensureOrder();
        _orderPos = 0;
      }
      if (_orderPos < 0) _orderPos = _order.length - 1;
      await pick(_order[_orderPos]);
      return;
    }

    let idx = _current + delta;
    if (idx < 0) idx = _view.length - 1;
    if (idx >= _view.length) idx = 0;
    await pick(idx);
  }

  /**
   * Turn shuffle on or off, or flip it.
   *
   * It applies to whatever the list is showing, which is the point: shuffling
   * one directory, or a search, or all 63 000 tunes, are all the same thing
   * from here.
   */
  function setShuffle(on) {
    _shuffle = (on === undefined) ? !_shuffle : !!on;
    _order = [];
    _orderPos = -1;
    try { localStorage.setItem('usbsid_shuffle', _shuffle ? '1' : '0'); } catch (_) {}
    paintShuffle();
    return _shuffle;
  }

  function paintShuffle() {
    const btn = $('btn-shuffle');
    if (!btn) return;
    btn.classList.toggle('active', _shuffle);
    btn.setAttribute('aria-pressed', _shuffle ? 'true' : 'false');
    btn.title = _shuffle
      ? 'Shuffle is on: the list plays in a random order, each tune once'
      : 'Shuffle is off: the list plays in order';
  }

  /* ------------------------------------------------------ local playlist -- */

  /**
   * Adopt a list of local files as a directory of their own.
   *
   * @param {Array<{name: string, file: File}>} files
   */
  function setLocalFiles(files) {
    _local = files.map(f => ({
      n: f.name,
      p: f.name,
      d: LOCAL_DIR,
      t: '', s: detectSids(f.name), ti: '', au: '', re: '',
      file: f.file,
    }));
    if (_local.length) {
      _path = [];
      _openDir = LOCAL_DIR;
    } else if (_openDir === LOCAL_DIR) {
      /* Clearing the playlist while looking at it would leave the pane open on
       * a directory that no longer exists. Back to the everything view, not to
       * whichever directory happens to sort first. */
      _openDir = _dirs.length ? ALL_DIR : null;
      _group = null;
    }
    _current = -1;
    render();
  }

  /* The header is not read here: that would mean reading every file the user
   * picked, hundreds of them, before showing a list. The name is what there is,
   * and the player reads the real header when the tune is loaded. */
  function detectSids(name) {
    const u = name.toUpperCase();
    return u.includes('4SID') ? 4 : u.includes('3SID') ? 3
         : u.includes('2SID') ? 2 : 1;
  }

  function localCount() { return _local.length; }

  /* ---------------------------------------------------------------- init -- */

  async function init() {
    const ok = await loadIndex();
    const box = $('sid-browser');
    if (!ok) {
      if (box) box.style.display = 'none';
      return;
    }
    _tree = buildTree();
    _ready = true;

    let savedDir = null, savedPath = null;
    try {
      savedDir  = localStorage.getItem('usbsid_sid_dir');
      savedPath = localStorage.getItem('usbsid_sid_path');
    } catch (_) {}
    /* "all" by default: it is the view that needs no decision from someone who
     * has just arrived, and the filter buttons work from it. */
    if (savedDir && (savedDir === ALL_DIR || _byDir.has(savedDir))) {
      _openDir = savedDir;
      _path = (savedDir === ALL_DIR)
            ? (savedPath ? savedPath.split('/').filter(Boolean) : [])
            : savedDir.split('/').filter(Boolean);
    } else {
      _openDir = _dirs.length ? ALL_DIR : null;
      _path = [];
    }

    const search_ = $('sid-search');
    if (search_) {
      /* Debounced. A keystroke scans every tune in the library, and on a full
       * HVSC that is 65 000 string searches: doing it on every keypress makes
       * typing feel like wading. 140ms is below the threshold where a search
       * box feels sluggish and well above a fast typist's gap between keys. */
      search_.addEventListener('input', () => {
        clearTimeout(_searchTimer);
        _searchTimer = setTimeout(() => search(search_.value), 140);
      });
    }
    const clear = $('sid-search-clear');
    if (clear) {
      clear.addEventListener('click', () => {
        if (search_) search_.value = '';
        search('');
        if (search_) search_.focus();
      });
    }
    const pane = $('sid-file-list');
    if (pane) pane.addEventListener('scroll', onPaneScroll, { passive: true });

    const shuf = $('btn-shuffle');
    if (shuf) shuf.addEventListener('click', () => setShuffle());
    try { _shuffle = localStorage.getItem('usbsid_shuffle') === '1'; } catch (_) {}
    paintShuffle();

    render();
  }

  return {
    init,
    render,
    openDir,
    search,
    pick,
    next: () => step(1),
    prev: () => step(-1),
    setShuffle,
    shuffling: () => _shuffle,
    setLocalFiles,
    localCount,
    ready: () => _ready,
    LOCAL_DIR,
    ALL_DIR,
  };
})();
