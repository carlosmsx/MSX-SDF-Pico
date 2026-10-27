// Pico simulada para probar CAM.COM en WebMSX (ver tools/prepara_webmsx.py).
// Se pega en la consola despues de tools/webmsx_arnes.js (y de
// webmsx_pico_red.js, si se quiere leerArchivo): conecta los puertos 00h y
// 01h a un SDF-Pico de mentira que contesta CMD_CAMSHOT, CMD_CAMPOLL,
// CMD_CAMREAD, CMD_CAMURL y CMD_CAMVER (docs/CAMARA.md).
//
// La pantalla sale de /cam.bin (los 12 KB que arma firmware/src/sc2.c,
// compilado en la PC). Cada foto tarda 8 consultas. Segun la URL:
// "noexiste" no resuelve, "clave-mal" contesta 401, "grande" es muy grande.
//
//   await cam.cargar('/cam.bin'); boot(); typ("b:\r", 100); typ("cam\r", 2000)
//   cam.log        los pedidos que llegaron

(function () {
    const C = window.cam = {
        cmd: 0, par: [], q: [], busy: 0, log: [],
        url: '', estado: 0, polls: 0, res: 0, det: 0, w: 0, h: 0, ms: 0,
        pantalla: new Uint8Array(12288),
    };
    C.cargar = async ruta => {
        C.pantalla = new Uint8Array(await (await fetch(ruta)).arrayBuffer());
        return C.pantalla.length;
    };
    const txt = (p, i, n) => String.fromCharCode(...p.slice(i, i + n));
    const sinClave = u => u.replace(/^((?:http:\/\/)?[^\/@:]*:)[^\/@]*@/, '$1****@');

    function ejecutar() {
        const p = C.par, q = [];
        C.busy = 3;
        switch (C.cmd) {
        case 0xB9: {                        // CMD_CAMSHOT
            const u = txt(p, 1, p[0]) || C.url;
            C.log.push('SHOT ' + u);
            if (C.estado === 1) { q.push(1); break; }
            if (p[0] && !/^(http:\/\/)?[^\/]+/.test(u) || /^https:/.test(u)) { q.push(2); break; }
            C.estado = 1; C.polls = 8;
            C.det = 0; C.w = 704; C.h = 576; C.ms = 1234; C.res = 0;
            if (!u) { C.res = 8; C.w = C.h = 0; }
            else if (/noexiste/.test(u)) { C.res = 2; C.w = C.h = 0; }
            else if (/clave-mal/.test(u)) { C.res = 4; C.det = 401; C.w = C.h = 0; }
            else if (/grande/.test(u)) { C.res = 6; C.w = 8000; C.h = 6000; }
            q.push(0);
            break;
        }
        case 0xBA:                          // CMD_CAMPOLL
            if (C.estado === 1 && --C.polls <= 0) C.estado = 2;
            q.push(C.estado);
            if (C.estado === 2)
                q.push(C.res, C.det & 255, C.det >> 8, C.w & 255, C.w >> 8,
                       C.h & 255, C.h >> 8, C.ms & 255, C.ms >> 8);
            else
                q.push(0, 0, 0, 0, 0, 0, 0, 0, 0);
            break;
        case 0xBB:                          // CMD_CAMREAD
            if (p[0] < 48) q.push(...C.pantalla.slice(p[0] * 256, p[0] * 256 + 256));
            break;
        case 0xBC:                          // CMD_CAMURL
            C.url = txt(p, 1, p[0]);
            C.log.push('URL ' + C.url);
            q.push(0);
            break;
        case 0xBD:                          // CMD_CAMVER
            q.push(...new TextEncoder().encode(sinClave(C.url)), 0);
            break;
        }
        C.q = q;
    }

    function completo() {
        const p = C.par;
        if (C.cmd === 0xB9 || C.cmd === 0xBC) return p.length > 0 && p.length >= 1 + p[0];
        if (C.cmd === 0xBB) return p.length >= 1;
        return true;
    }

    const bus = WMSX.room.machine.bus;
    bus.connectOutputDevice(0x01, v => { C.cmd = v; C.par = []; C.q = []; if (completo()) ejecutar(); });
    bus.connectOutputDevice(0x00, v => { C.par.push(v); if (completo()) ejecutar(); });
    bus.connectInputDevice(0x01, () => C.busy > 0 ? (C.busy--, 0xC0) : 0x40);
    bus.connectInputDevice(0x00, () => C.q.length ? C.q.shift() : 0xFF);
})();
