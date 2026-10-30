// Pico simulada con MQTT para probar MQTT.COM en WebMSX (ver tools/prepara_webmsx.py).
// Se pega en la consola despues de tools/webmsx_arnes.js: conecta los puertos
// 00h y 01h a un SDF-Pico de mentira que contesta CMD_MQTTCONN a CMD_MQTTVER
// (docs/MQTT.md) con un broker local: lo que se publica les llega a los
// suscriptos (con + y #). El broker "noexiste" no resuelve y "clave@..." da
// usuario o clave.
//
//   boot(); typ("b:\r", 100); typ("mqtt msx/chat\r", 600)
//   mqtt.llega("casa/temp", "21.5 °C")    un mensaje de afuera (en UTF-8)
//   mqtt.cortar()                          se cae la conexion (reintenta)
//   mqtt.publicados                        lo que publico el MSX

(function () {
    const utf8 = s => [...new TextEncoder().encode(s)];
    // De 80h a AFh, el juego del MSX es el de la pagina 437 (firmware/src/mqtt.c)
    const alto = 'ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»';
    const aMsx = bytes => {
        const s = new TextDecoder().decode(new Uint8Array(bytes));
        return [...s].map(ch => {
            const c = ch.codePointAt(0);
            if (c === 9 || c === 10 || c === 13) return 32;
            if (c < 32 || c === 127) return 46;
            if (c < 128) return c;
            const i = alto.indexOf(ch);
            return i >= 0 ? 0x80 + i : c === 0xB0 ? 0xA7 : 63;
        });
    };
    const deMsx = bytes => utf8(bytes.map(b => b < 0x80 ? String.fromCharCode(b)
        : b <= 0xAF ? alto[b - 0x80] : '?').join(''));
    const txt = a => String.fromCharCode(...a);

    const M = window.mqtt = {
        cmd: 0, par: [], q: [], busy: 0,
        estado: 0, motivo: 0, polls: 0, quiere: false,
        guardada: 'mqtt://usuario:secreta@broker.local', url: '',
        subs: [], cola: [], perdidos: 0, vistos: 0, enviando: 0,
        publicados: [], pedidos: [],
    };

    const coincide = (filtro, t) => {
        const f = filtro.split('/'), p = t.split('/');
        for (let i = 0; i < f.length; i++) {
            if (f[i] === '#') return true;
            if (i >= p.length || (f[i] !== '+' && f[i] !== p[i])) return false;
        }
        return f.length === p.length;
    };

    // Un mensaje que llega del broker (topico y texto en UTF-8)
    M.llega = (topico, texto) => {
        if (!(M.estado & 1) || !M.subs.some(s => coincide(s, topico))) return false;
        if (M.cola.length >= 20) { M.perdidos++; return false; }
        M.cola.push({ t: utf8(topico), x: typeof texto === 'string' ? utf8(texto) : texto });
        return true;
    };
    M.cortar = () => { M.estado = 2 | 4; M.motivo = 18; M.polls = 20; };

    function conectar(url) {
        M.url = url || M.guardada;
        M.subs = []; M.cola = [];
        M.quiere = true;
        if (!M.url) { M.estado = 4; M.motivo = 20; return; }
        M.estado = 2; M.motivo = 0; M.polls = 5;
    }

    function poll(modo) {
        if (M.estado & 2 && --M.polls <= 0) {
            const host = M.url.replace(/^mqtt:\/\//, '').replace(/^.*@/, '').split(/[:/]/)[0];
            if (host === 'noexiste') { M.estado = 2 | 4; M.motivo = 17; M.polls = 30; }
            else if (M.url.includes('clave@')) { M.estado = 4; M.motivo = 4; M.quiere = false; }
            else { M.estado = 1; M.motivo = 0; }
        }
        if (M.enviando) M.enviando--;
        let e = M.estado | (M.enviando ? 0x20 : 0);
        if (M.perdidos !== M.vistos) { e |= 0x10; M.vistos = M.perdidos; }
        const out = [e, M.motivo];
        const m = M.cola.shift();
        if (m) {
            out[0] |= 0x08;
            let t = m.t.slice(0, 127), x = m.x.slice(0, 255);
            const cortado = m.t.length > 127 || m.x.length > 255 ? 1 : 0;
            if (modo & 4) { t = aMsx(t); x = aMsx(x); }
            out.push(cortado, t.length, ...t, x.length, ...x);
        }
        return out;
    }

    function ejecutar() {
        M.busy = 5;
        const p = M.par;
        M.pedidos.push([M.cmd, ...p]);
        switch (M.cmd) {
        case 0xA8:                          // CONN
            conectar(txt(p.slice(1, 1 + p[0])));
            M.q = [0];
            break;
        case 0xA9:                          // CLOSE
            M.estado = 0; M.motivo = 0; M.subs = []; M.cola = []; M.quiere = false;
            M.q = [0];
            break;
        case 0xAA: {                        // SUB
            const t = txt(p.slice(2, 2 + p[1]));
            if (!t.length || t.length > 127) { M.q = [3]; break; }
            M.subs = M.subs.filter(s => s !== t);
            if (!(p[0] & 0x80)) M.subs.push(t);
            M.q = [0];
            break;
        }
        case 0xAB: {                        // PUB
            const lt = p[1], t = txt(p.slice(2, 2 + lt));
            const lx = p[2 + lt];
            let x = p.slice(3 + lt, 3 + lt + lx);
            if (!t.length || /[+#]/.test(t)) { M.q = [3]; break; }
            if (!(M.estado & 1)) { M.q = [1]; break; }
            if (p[0] & 4) x = deMsx(x);
            M.publicados.push({ topico: t, texto: new TextDecoder().decode(new Uint8Array(x)), flags: p[0] });
            M.enviando = 3;
            M.q = [0];
            setTimeout(() => M.llega(t, x), 0);     // el broker lo devuelve a los suscriptos
            break;
        }
        case 0xAC:                          // POLL
            M.q = poll(p[0]);
            break;
        case 0xAD:                          // URL
            M.guardada = txt(p.slice(1, 1 + p[0]));
            M.q = [0];
            break;
        case 0xAE:                          // VER
            M.q = [...utf8(M.guardada.replace(/:([^:@/]*)@/, ':****@')), 0];
            break;
        default:
            M.q = [];
        }
    }

    function completo() {
        const p = M.par, n = p.length;
        switch (M.cmd) {
        case 0xA8: case 0xAD: return n >= 1 && n >= 1 + p[0];
        case 0xAA: return n >= 2 && n >= 2 + p[1];
        case 0xAB: return n >= 3 && n >= 3 + p[1] && n >= 3 + p[1] + p[2 + p[1]];
        case 0xAC: return n >= 1;
        default: return true;
        }
    }

    const bus = WMSX.room.machine.bus;
    bus.connectOutputDevice(0x01, v => { M.cmd = v; M.par = []; M.q = []; if (completo()) ejecutar(); });
    bus.connectOutputDevice(0x00, v => { M.par.push(v); if (completo()) ejecutar(); });
    bus.connectInputDevice(0x01, () => M.busy > 0 ? (M.busy--, 0xC0) : 0x40);
    bus.connectInputDevice(0x00, () => M.q.length ? M.q.shift() : 0xFF);
})();
