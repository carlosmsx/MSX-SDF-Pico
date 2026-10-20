// Pico simulada con la API de archivos para probar SD.COM en WebMSX (ver
// tools/prepara_webmsx.py). Se pega en la consola despues de
// tools/webmsx_arnes.js: conecta los puertos 00h y 01h a un SDF-Pico de
// mentira que contesta CMD_FSCD ... CMD_FSFREE (docs/ARCHIVOS.md) sobre una SD
// en memoria. Sigue a firmware/src/archivos.c, sin la FAT: los nombres cortos
// se inventan (NOMBRE~1) y la fecha es fija.
//
//   boot(); typ("b:\r", 100); typ("sd dir\r", 600)
//   fs.raiz               el arbol: {nombre, corto, dir, hijos | datos}
//   fs.archivo("\\JUEGOS\\KNIGHT~1.ROM")   los bytes de un archivo de la SD
//   fs.montadas           los nombres cortos de las imagenes montadas

(function () {
    const azar = (n, semilla) => {
        const b = new Uint8Array(n);
        let x = semilla;
        for (let i = 0; i < n; i++) { x = (x * 1103515245 + 12345) >>> 0; b[i] = x >>> 24; }
        return b;
    };
    const FECHA = (2026 - 1980) << 9 | 10 << 5 | 19, HORA = 12 << 11;
    const E = { ILEGAL: 5, NOEXISTE: 53, ENUSO: 54, NOMBRE: 56, NOABIERTO: 59, YAEXISTE: 65,
                LLENA: 66, SOLOLECT: 68, SINDIR: 80, NOVACIO: 81, ESDIR: 82 };

    let cortos = 0;
    function nodo(nombre, datos) {
        const n = { nombre, dir: !datos, hijos: datos ? null : [], datos: datos || null, atrib: datos ? 0x20 : 0x10 };
        n.corto = /^[A-Z0-9_]{1,8}(\.[A-Z0-9_]{1,3})?$/i.test(nombre) ? nombre.toUpperCase()
            : nombre.toUpperCase().replace(/[^A-Z0-9_.]/g, '').split('.')[0].slice(0, 6) + '~' + (++cortos)
              + (nombre.includes('.') ? '.' + nombre.split('.').pop().toUpperCase().slice(0, 3) : '');
        return n;
    }
    function dir(nombre, ...hijos) { const d = nodo(nombre); d.hijos.push(...hijos); return d; }
    const arch = (nombre, n, semilla) => nodo(nombre, azar(n, semilla));

    const F = window.fs = {
        cmd: 0, par: [], q: [], busy: 0, datos: null,
        raiz: dir('',
            dir('JUEGOS', arch('Knightmare.rom', 32768, 1), arch('NEMESIS.ROM', 131072, 2),
                dir('MSX2', arch('ALESTE2.DSK', 737280, 3))),
            dir('Juegos de Konami', arch('Penguin Adventure.rom', 131072, 4)),
            arch('JUEGOS.DSK', 737280, 5), arch('LEEME.TXT', 300, 6),
            (() => { const n = arch('OCULTO.SYS', 10, 7); n.atrib |= 0x02; return n; })(),
            (() => { const n = arch('FIJO.TXT', 10, 8); n.atrib |= 0x01; return n; })()),
        actual: [], tabla: [], abierto: null, montadas: ['JUEGOS.DSK'], log: [],
    };

    const igual = (n, t) => n.nombre.toUpperCase() === t.toUpperCase() || n.corto === t.toUpperCase();
    const ruta = camino => camino.length ? '\\' + camino.map(n => n.corto).join('\\') : '\\';

    // Como juntar() + canonica(): el camino de nodos hasta el ultimo tramo, y el
    // ultimo tramo (nodo o texto si no existe)
    function resolver(texto) {
        if (/[:\x00-\x1f]/.test(texto)) return { err: E.NOMBRE };
        const partes = texto.split(/[\\/]/);
        let camino = /^[\\/]/.test(texto) ? [] : [...F.actual];
        const tramos = partes.filter(t => t && t !== '.');
        for (let i = 0; i < tramos.length; i++) {
            const t = tramos[i];
            if (t === '..') { camino.pop(); continue; }
            const padre = camino.length ? camino[camino.length - 1] : F.raiz;
            const n = padre.dir && padre.hijos.find(h => igual(h, t));
            if (!n) {
                const resto = tramos.slice(i + 1).filter(x => x !== '..');
                return resto.length ? { err: E.SINDIR } : { camino, falta: t, padre };
            }
            if (!n.dir && i < tramos.length - 1) return { err: E.SINDIR };
            camino = [...camino, n];
        }
        return { camino };
    }
    const nodoDe = r => r.camino.length ? r.camino[r.camino.length - 1] : F.raiz;
    const padreDe = r => r.camino.length > 1 ? r.camino[r.camino.length - 2] : F.raiz;

    function entrada(n, nombre) {
        const e = new Array(64).fill(0);
        const largo = n.datos ? n.datos.length : 0;
        e[0] = n.atrib;
        e[1] = largo & 255; e[2] = (largo >> 8) & 255; e[3] = (largo >> 16) & 255; e[4] = largo >>> 24;
        if (nombre === undefined) { e[5] = FECHA & 255; e[6] = FECHA >> 8; e[7] = HORA & 255; e[8] = HORA >> 8; }
        const c = nombre || n.corto, l = nombre || n.nombre;
        for (let i = 0; i < c.length; i++) e[9 + i] = c.charCodeAt(i);
        for (let i = 0; i < Math.min(l.length, 41); i++) e[22 + i] = l.charCodeAt(i);
        return e;
    }

    const comodin = m => new RegExp('^' + m.replace(/[.+^${}()|[\]\\]/g, '\\$&')
        .replace(/\*/g, '.*').replace(/\?/g, '.') + '$', 'i');
    const enUso = (r, n) => (r.camino.length === 1 && F.montadas.includes(n.corto))
        || (F.abierto && F.abierto.nodo === n)
        || F.actual.includes(n);
    const texto = (p, i, n) => String.fromCharCode(...p.slice(i, i + n));
    const z = s => [...new TextEncoder().encode(s), 0];

    function dirCmd(opciones, t) {
        let mascara = '*';
        const u = Math.max(t.lastIndexOf('\\'), t.lastIndexOf('/')) + 1;
        if (/[*?]/.test(t.slice(u))) { mascara = t.slice(u); t = t.slice(0, u); }
        if (mascara === '*.*') mascara = '*';
        const r = resolver(t);
        if (r.err || r.falta) return { err: r.err || E.SINDIR };
        const d = nodoDe(r);
        if (!d.dir) return { err: E.SINDIR };
        const re = comodin(mascara);
        const hijos = d.hijos.filter(h => (opciones & 1 || !(h.atrib & 6)) && (re.test(h.nombre) || re.test(h.corto)))
            .sort((a, b) => (b.dir - a.dir) || a.nombre.toUpperCase().localeCompare(b.nombre.toUpperCase()));
        F.tabla = hijos.map(h => entrada(h));
        if (opciones & 2 && r.camino.length) F.tabla.unshift(entrada({ atrib: 0x10 }, '..'));
        return { err: 0 };
    }

    function ejecutar() {
        const p = F.par;
        F.busy = 5;
        F.log.push(F.cmd.toString(16));
        switch (F.cmd) {
        case 0xC0: {                        // FSCD
            const r = resolver(texto(p, 1, p[0]));
            const err = r.err || (r.falta || !nodoDe(r).dir ? E.SINDIR : 0);
            if (!err) F.actual = r.camino;
            F.q = [err];
            break;
        }
        case 0xC1: F.q = z(ruta(F.actual)); break;
        case 0xC2: {                        // FSDIR
            F.tabla = [];
            const { err } = dirCmd(p[0], texto(p, 2, p[1]));
            F.q = [err, F.tabla.length & 255, F.tabla.length >> 8, 0];
            break;
        }
        case 0xC3: {                        // FSLIST
            const desde = p[0] | p[1] << 8, max = Math.min(p[2], 8);
            const parte = F.tabla.slice(desde, desde + max);
            F.q = [parte.length, ...parte.flat()];
            break;
        }
        case 0xC4: {                        // FSOPEN
            F.abierto = null;
            const r = resolver(texto(p, 2, p[1]));
            let err = r.err || 0;
            if (!err && p[0] === 0) {
                if (r.falta) err = E.NOEXISTE;
                else if (nodoDe(r).dir) err = E.ESDIR;
                else F.abierto = { nodo: nodoDe(r), pos: 0, escribe: false };
            } else if (!err) {
                const n = r.falta ? null : nodoDe(r);
                if (n && n.dir) err = E.ESDIR;
                else if (n && p[0] === 1) err = E.YAEXISTE;
                else if (n && n.atrib & 1) err = E.SOLOLECT;
                else if (n && enUso(r, n)) err = E.ENUSO;
                else if (n) { n.datos = new Uint8Array(0); F.abierto = { nodo: n, pos: 0, escribe: true }; }
                else {
                    const nuevo = nodo(r.falta, new Uint8Array(0));
                    r.padre.hijos.push(nuevo);
                    F.abierto = { nodo: nuevo, pos: 0, escribe: true };
                }
            }
            const t = F.abierto ? F.abierto.nodo.datos.length : 0;
            F.q = [err, t & 255, (t >> 8) & 255, (t >> 16) & 255, t >>> 24];
            break;
        }
        case 0xC5: {                        // FSREAD
            const a = F.abierto;
            if (!a || a.escribe) { F.q = [E.NOABIERTO, 0, 0]; break; }
            const max = Math.min(p[0] | p[1] << 8, 512);
            const out = [...a.nodo.datos.slice(a.pos, a.pos + max)];
            a.pos += out.length;
            F.q = [0, out.length & 255, out.length >> 8, ...out];
            break;
        }
        case 0xC6: {                        // FSWRITE: los datos llegan despues
            F.datos = { quedan: p[0] | p[1] << 8, bytes: [] };
            F.q = [];
            F.busy = 1e9;
            if (!F.datos.quedan) terminarEscritura();
            break;
        }
        case 0xC7: F.q = [0]; F.abierto = null; break;
        case 0xC8: {                        // FSDEL
            const r = resolver(texto(p, 1, p[0]));
            let err = r.err || (r.falta ? E.NOEXISTE : !r.camino.length ? E.ILEGAL : 0);
            const n = !err && nodoDe(r);
            if (!err) err = enUso(r, n) ? E.ENUSO : n.atrib & 1 ? E.SOLOLECT
                : n.dir && n.hijos.length ? E.NOVACIO : 0;
            if (!err) padreDe(r).hijos.splice(padreDe(r).hijos.indexOf(n), 1);
            F.q = [err];
            break;
        }
        case 0xC9: {                        // FSMKDIR
            const r = resolver(texto(p, 1, p[0]));
            const err = r.err || (r.falta ? 0 : E.YAEXISTE);
            if (!err) r.padre.hijos.push(nodo(r.falta));
            F.q = [err];
            break;
        }
        case 0xCA: {                        // FSREN
            const viejo = texto(p, 1, p[0]), nuevo = texto(p, 2 + p[0], p[1 + p[0]]);
            const r = resolver(viejo);
            let err = r.err || (r.falta ? E.NOEXISTE : !r.camino.length ? E.ILEGAL : 0);
            const n = !err && nodoDe(r);
            if (!err && enUso(r, n)) err = E.ENUSO;
            let d;
            if (!err) {
                d = /[\\/]/.test(nuevo) ? resolver(nuevo)
                    : { camino: r.camino.slice(0, -1), falta: nuevo, padre: padreDe(r) };
                if (/^\.\.?$|:/.test(nuevo)) err = E.NOMBRE;
                else if (d.err) err = d.err;
                else if (!d.falta) err = E.YAEXISTE;
                else if (d.camino.includes(n)) err = E.ILEGAL;
            }
            if (!err) {
                padreDe(r).hijos.splice(padreDe(r).hijos.indexOf(n), 1);
                const m = nodo(d.falta, n.datos);
                Object.assign(n, { nombre: m.nombre, corto: m.corto });
                d.padre.hijos.push(n);
            }
            F.q = [err];
            break;
        }
        case 0xCB: {                        // FSSTAT
            const r = resolver(texto(p, 1, p[0]));
            const err = r.err || (r.falta ? E.NOEXISTE : 0);
            F.q = [err, ...(err ? new Array(64).fill(0)
                : r.camino.length ? entrada(nodoDe(r)) : entrada({ atrib: 0x10 }, '\\'))];
            break;
        }
        case 0xCC: {                        // FSFREE
            const libres = 1234567, total = 7654321;
            const b = v => [v & 255, (v >> 8) & 255, (v >> 16) & 255, v >>> 24];
            F.q = [0, ...b(libres), ...b(total)];
            break;
        }
        default: F.q = [];
        }
    }

    function terminarEscritura() {
        const a = F.abierto, d = F.datos;
        F.datos = null;
        F.busy = 3;
        if (!a || !a.escribe) { F.q = [E.NOABIERTO]; return; }
        if (d.bytes.length > 512) { F.q = [E.ILEGAL]; return; }
        const nuevo = new Uint8Array(a.nodo.datos.length + d.bytes.length);
        nuevo.set(a.nodo.datos);
        nuevo.set(d.bytes, a.nodo.datos.length);
        a.nodo.datos = nuevo;
        F.q = [0];
    }

    function completo() {
        const p = F.par;
        switch (F.cmd) {
        case 0xC0: case 0xC8: case 0xC9: case 0xCB: return p.length >= 1 && p.length >= 1 + p[0];
        case 0xC2: case 0xC4: return p.length >= 2 && p.length >= 2 + p[1];
        case 0xC3: return p.length >= 3;
        case 0xC5: case 0xC6: return p.length >= 2;
        case 0xCA: return p.length >= 1 && p.length > 1 + p[0] && p.length >= 2 + p[0] + p[1 + p[0]];
        default: return true;
        }
    }

    F.archivo = t => { const r = resolver(t); return r.err || r.falta ? null : nodoDe(r).datos; };
    F.esperado = (nombre, n, semilla) => azar(n, semilla);

    const bus = WMSX.room.machine.bus;
    bus.connectOutputDevice(0x01, v => { F.cmd = v; F.par = []; F.q = []; F.datos = null; if (completo()) ejecutar(); });
    bus.connectOutputDevice(0x00, v => {
        if (F.datos) {
            F.datos.bytes.push(v);
            if (--F.datos.quedan === 0) terminarEscritura();
            return;
        }
        F.par.push(v);
        if (completo()) ejecutar();
    });
    bus.connectInputDevice(0x01, () => F.busy > 0 ? (F.busy--, 0xC0) : 0x40);
    bus.connectInputDevice(0x00, () => F.q.length ? F.q.shift() : 0xFF);
})();
