// Pico simulada con la SD para probar SDF.COM en WebMSX (ver tools/prepara_webmsx.py).
// Se pega en la consola despues de tools/webmsx_arnes.js: conecta los puertos
// 00h y 01h a un SDF-Pico de mentira que contesta CMD_SDFTEST, CMD_SDFINFO,
// CMD_SDFFILES, CMD_SDFMOUNT, CMD_SDFUMOUNT, CMD_SDFDRIVES, CMD_SDFNEW y el
// CMD_WRITE con el que SDF.COM formatea la imagen nueva, CMD_SDFBORRAR, y
// CMD_UPDATE y CMD_UPDPOLL de SDF UPDATE (docs/SD.md, docs/ACTUALIZAR.md).
//
//   boot(); typ("b:\r", 100); typ("sdf new prueba\r", 600)
//   sd.imagenes        nombre -> {kb, sistema: los sectores que escribio el CMD_WRITE}
//   sd.act             la actualizacion: versiones, falla (el resultado que
//                      termina la consulta, 0 si anda) y viejo (sin CMD_UPDATE)

(function () {
    const S = window.sd = {
        cmd: 0, par: [], q: [], busy: 0,
        imagenes: { 'JUEGOS.DSK': { kb: 720 }, 'DATOS.DSK': { kb: 720 }, 'VIEJO.DSK': { kb: 360 },
                    'RARO.DSK': { kb: 100 } },
        montadas: ['JUEGOS.DSK', ''], drives: 2,
        nueva: null, avisos: 0, escritura: null, log: [],
        datos: { red: 'MiCasa', camara: 'http://u:clave@cam/foto.jpg' },  // lo que borra 0xE7
        // 0xBE/0xBF: cada consulta del MSX avanza un paso (t)
        act: { instalada: '0.14', github: '0.15', falla: 0, detalle: 0, kb: 466,
               estado: 0, modo: 0, t: 0, ausente: 0, pedidos: [] },
        ausente: 0,     // consultas del estado que contestan FFh (la Pico reiniciandose)
    };

    // La respuesta de CMD_UPDPOLL en el paso t de la actualizacion
    function actPoll() {
        const A = S.act;
        const r = (estado, fase, kb, kt) => {
            const det = estado === 2 ? A.detalle : 0;
            const nueva = estado === 2 && A.github.localeCompare(A.instalada, undefined, { numeric: true }) > 0;
            return [estado, fase, det & 255, det >> 8, kb & 255, kb >> 8, kt & 255, kt >> 8,
                    nueva ? 1 : 0, ...z(A.instalada), ...z(estado === 2 ? A.github : '')];
        };
        if (A.estado !== 1)
            return r(A.estado, A.estado === 2 ? A.res : 0, 0, 0);
        const t = ++A.t;
        if (t < 3)
            return r(1, 1, 0, 0);                               // consultando
        if (A.modo === 0 || A.falla) {
            A.estado = 2;
            A.res = A.falla;
            return r(2, A.res, 0, 0);
        }
        if (t < 4)
            return r(1, 2, 0, 0);                               // la suma: sin total
        if (t < 12)
            return r(1, 2, Math.min(A.kb, (t - 3) * 64), A.kb); // bajando
        if (t < 13)
            return r(1, 3, A.kb, A.kb);                         // verificando
        if (t < 15)
            return r(1, 4, A.kb, A.kb);                         // grabando
        // se reinicia: unas consultas sin Pico, y arranca la nueva
        A.estado = 0;
        A.instalada = A.github;
        S.ausente = 4;
        return r(1, 4, A.kb, A.kb);
    }
    const z = s => [...new TextEncoder().encode(s), 0];
    const nombreDe = (p, i, n) => String.fromCharCode(...p.slice(i, i + n)).toUpperCase();
    const conDsk = n => n.includes('.') ? n : n + '.DSK';

    function ejecutar() {
        const p = S.par;
        S.busy = 5;
        switch (S.cmd) {
        case 0xD1: S.q = [8, ...z('SDF-Pico 0.9\r\nSD ok')]; break;
        case 0xE6: S.q = [S.drives, ...z(S.montadas[0]), ...z(S.montadas[1])]; break;
        case 0xE2: S.q = [...Object.keys(S.imagenes).flatMap(z), 0]; break;
        case 0xE1: {
            const n = conDsk(nombreDe(p, 2, p[1]));
            const i = S.imagenes[n];
            S.q = [p[0] > 1 ? 62 : !i ? 53 : i.kb !== 720 && i.kb !== 360 ? 61 : 0];
            if (!S.q[0]) S.montadas[p[0]] = n;
            break;
        }
        case 0xE3: S.q = [p[0] > 1 ? 62 : 0]; if (p[0] < 2) S.montadas[p[0]] = ''; break;
        case 0xE5: S.q = [p[0] < 1 || p[0] > 2 ? 5 : 0]; if (!S.q[0]) S.drives = p[0]; break;
        case 0xE4: {
            const n = conDsk(nombreDe(p, 2, p[1]));
            if (S.imagenes[n]) { S.q = [65]; break; }
            if (!/^[A-Z0-9_]{1,8}\.[A-Z0-9]{0,3}$/.test(n)) { S.q = [56]; break; }
            S.nueva = n;
            S.imagenes[n] = { kb: p[0] === 0xF8 ? 360 : 720, sistema: [] };
            S.avisos = 3;               // tres SDFNEW_BUSY y despues el 0
            S.q = [];
            S.busy = 2000;
            break;
        }
        case 0xE7:
            S.q = [p[0] === 0x5A ? 0 : 5];
            if (!S.q[0]) S.datos = { red: '', camara: '' };
            S.busy = 50;
            break;
        case 0xBE: {
            const A = S.act;
            if (A.viejo) { S.q = []; break; }   // un firmware sin CMD_UPDATE
            A.pedidos.push([...p]);
            if (A.estado === 1) S.q = [1];
            else if (p[0] > 1 || (p[0] === 1 && p[1] !== 0x5A)) S.q = [2];
            else { S.q = [0]; A.estado = 1; A.modo = p[0]; A.t = 0; }
            break;
        }
        case 0xBF: S.q = actPoll(); break;
        case 0xF0: {
            const n = p[1];
            S.escritura = { drive: p[0], quedan: n * 512, sector: [] };
            S.q = [p[0] === 2 && S.nueva ? 0 : 2];
            S.log.push('WRITE drive ' + p[0] + ' n=' + n + ' media ' + p[2].toString(16));
            break;
        }
        default: S.q = [];
        }
    }

    function params() {
        const p = S.par;
        switch (S.cmd) {
        case 0xE1: case 0xE4: return p.length >= 2 && p.length >= 2 + p[1];
        case 0xE3: case 0xE5: case 0xE7: return p.length >= 1;
        case 0xBE: return p.length >= 2;
        case 0xF0: return p.length >= 7;
        default: return true;
        }
    }

    const bus = WMSX.room.machine.bus;
    bus.connectOutputDevice(0x01, v => {
        S.cmd = v; S.par = []; S.q = []; S.escritura = null;
        if (params()) ejecutar();
    });
    bus.connectOutputDevice(0x00, v => {
        const e = S.escritura;
        if (e && S.q.length === 0 && e.quedan > 0) {     // datos del CMD_WRITE
            if (e.sector.length === 0) S.busy = 1e9;      // OCUPADO hasta tener el sector
            e.sector.push(v);
            e.quedan--;
            if (e.sector.length === 512) {
                S.imagenes[S.nueva].sistema.push(e.sector);
                e.sector = [];
                S.busy = 3;
            }
            return;
        }
        S.par.push(v);
        if (params()) ejecutar();
    });
    bus.connectInputDevice(0x01, () => {
        if (S.ausente > 0) {
            S.ausente--;
            return 0xFF;
        }
        if (S.busy > 0) {
            S.busy--;
            if (S.busy === 0 && S.cmd === 0xE4 && S.q.length === 0) {
                // un SDFNEW_BUSY por "segundo", y al final el resultado
                S.q = [S.avisos-- > 0 ? 0xFE : 0];
                if (S.q[0] === 0xFE) S.reponer = true;
            }
            return 0xC0;
        }
        return 0x40;
    });
    bus.connectInputDevice(0x00, () => {
        const v = S.q.length ? S.q.shift() : 0xFF;
        if (S.reponer) { S.reponer = false; S.busy = 2000; }
        return v;
    });
})();
